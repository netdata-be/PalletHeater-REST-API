#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ota.h>
#include <constants.h>
#include <setupWifi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <metrics.h>

#include <helperFunctions.h>
#include "Stove.h"
#include <telnetMenu.h>

#define FORMAT_SPIFFS_IF_FAILED true

static SemaphoreHandle_t stoveMutex = NULL;

bool getNTPtime(int sec)
{
    {
        uint32_t start = millis();
        struct tm ntp_tm = {0};
        do
        {
            time(&now);
            localtime_r(&now, &ntp_tm);
#if SENSOR_DEBUG
            Serial.print(".");
#endif
            delay(10);
        } while (((millis() - start) <= (1000 * sec)) && (ntp_tm.tm_year < (2016 - 1900)));
        if (ntp_tm.tm_year <= (2016 - 1900))
            return false; // the NTP call was not successful

        currentDst = ntp_tm.tm_isdst;
#if SENSOR_DEBUG
        Serial.print("now ");
        Serial.println(now);
        char time_output[30];
        strftime(time_output, 30, "%a  %d-%m-%y %T", localtime(&now));
        Serial.println(time_output);
        Serial.println();
#endif
    }
    return true;
}



void getAmbTemp() // Get room temperature
{
    ambTemp = stove.read(ram, ambTempAddr);
    if (ambTemp >= 0)
    {
        ambTemp = (float)ambTemp / 2;
    }
    else
    {
        log_e("Failed to get value");
    }
#if SENSOR_DEBUG
    Serial.print("T. amb. ");
    Serial.println(ambTemp);
#endif
}

void readFirmwareVersion(char* out, size_t len)
{
    char variant0 = stove.read(eeprom, fwVariantChar0Addr);
    char variant1 = stove.read(eeprom, fwVariantChar1Addr);
    int  product  = stove.read(eeprom, fwProductAddr);
    int  major    = stove.read(eeprom, fwMajorAddr);
    int  minor    = stove.read(eeprom, fwMinorAddr);
    int  patch    = stove.read(eeprom, fwPatchAddr);

    snprintf(out, len, "FW-%c%c-%02d-V-%02d-%02d-%02d",
             variant0, variant1, product, major, minor, patch);
}

int getFumeFanRPM()
{
    int rpm = stove.read(ram, fumeFanRPMAddr);
    if (rpm > 0)
    {
        rpm = (rpm + 25) * 10;
    }
    return rpm;
}

int getRemoteControlLastSeen()
{
    int ttl = stove.read(ram, remoteControlTTLAddr);
    if (ttl > 0)
    {
        ttl = 240 - ttl;
    }
    else
    {
        ttl = -1;
    }
    return ttl;
}

void getAmbTempStove() // Get room temperature from stove sensor
{
    ambTempStove = stove.read(ram, ambTempStoveAddr);
    if (ambTempStove >= 0)
    {
        ambTempStove = (float)ambTempStove / 2;
    }
    else
    {
        log_e("Failed to get value");
    }
#if SENSOR_DEBUG
    Serial.print("T. amb. stove : ");
    Serial.println(ambTempStove);
#endif
}

void getAmbTempRemoteControl() // Get room temperature from stove sensor
{
    ambTempRemoteControl = stove.read(ram, ambTempRemoteAddr);
    if (ambTempRemoteControl >= 0)
    {
        ambTempRemoteControl = (float)ambTempRemoteControl / 2;
    }
    else
    {
        log_e("Failed to get value");
    }
#if SENSOR_DEBUG
    Serial.print("T. amb. remote control : ");
    Serial.println(ambTempRemoteControl);
#endif
}

int numberOfStarts()
{
    int hi = stove.read(eeprom, numberOfStartsHiAddr);
    int lo = stove.read(eeprom, numberOfStartsLoAddr);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 8) + lo;
}

bool stoveBeepIsEnabled() // Get room temperature
{
    ambTemp = stove.read(eeprom, beepEnabledAddr);
    if (ambTemp == 0)
    {
        return false;
    }
    else
    {
        return true;
    }
}

int arePalletsLow()
{
    int val = stove.read(ram, palletSensorAddr);
    if (val == 5)
    {
        val = stove.read(ram, palletLevelAddr);
        if (val <= 1) {
            return 1;
        }
        else {
            return 0;
        }
    }
    else
    {
        return 0;
    }
}

void getTempSetpoint()
{
    setPointTemp = (float)stove.read(eeprom, TempSetpointAddr);
#if SENSOR_DEBUG
    Serial.print("T. setpoint. ");
    Serial.println(setPointTemp);
#endif
}

void getFumeTemp() // Get flue gas temperature
{
    fumesTemp = stove.read(ram, fumesTempAddr);
#if SENSOR_DEBUG
    Serial.printf("T. fumes %d\n", fumesTemp);
#endif
}

void writeDatepartToStove(const char* format, byte address)
{
    char time_output[30];
    int number;
    int mostSigDigit;
    int leastSigDigit;
    byte value;

    strftime(time_output, 30, format, localtime(&now));
    number = atoi(time_output);
    mostSigDigit = number / 10;
    leastSigDigit = number % 10;

    value = mostSigDigit * 0x10 + leastSigDigit;

    stove.write(eeprom, address, value);
    delay(20);
}

void setStoveDateTime()
{
    if (stoveMutex) {
        xSemaphoreTake(stoveMutex, portMAX_DELAY);
    }

    getNTPtime(2);
    writeDatepartToStove("%d", eepromDateDayAddr);
    writeDatepartToStove("%m", eepromDateMonthAddr);
    writeDatepartToStove("%y", eepromDateYearAddr);
    writeDatepartToStove("%H", eepromTimeHourAddr);
    writeDatepartToStove("%M", eepromTimeMinAddr);
    writeDatepartToStove("%W", eepromTimeDOWAddr);

    if (stoveMutex) {
        xSemaphoreGive(stoveMutex);
    }
}

void getRoomFanSpeed()
{
    roomFanSpeed = stove.read(eeprom, roomFanEepromAddr);
#if SENSOR_DEBUG
    Serial.printf("Fan speed: %d\n", roomFanSpeed);
#endif
}

void getFlamePower() // Get the flame power (0, 1, 2, 3, 4, 5)
{
    flamePower = stove.read(eeprom, flamePowerEepromAddr);
}

void getEnergySaving() 
{
    energySavingStart = stove.read(eeprom, energySavingStartAddr)*-1;
    energySavingStop = stove.read(eeprom, energySavingStopAddr);
}


int getErrorMemory(int pos);
void getStoveDateTime();

// Fast-changing values — refreshed every 2 s
void getFastStates()
{
    if (stoveMutex) xSemaphoreTake(stoveMutex, portMAX_DELAY);

    getStoveState();
    getFumeTemp();
    getFlamePower();
    getRoomFanSpeed();
    cachedFumeFanRPM            = getFumeFanRPM();
    cachedRemoteControlLastSeen = getRemoteControlLastSeen();

    if (stoveMutex) xSemaphoreGive(stoveMutex);
}

// Slow-changing values — refreshed every ~16 s
void getSlowStates()
{
    if (stoveMutex) xSemaphoreTake(stoveMutex, portMAX_DELAY);

    getAmbTemp();
    getAmbTempStove();
    getAmbTempRemoteControl();
    getTempSetpoint();
    getEnergySaving();
    yield();

    cachedAugerSpeed = stove.read(ram, augerSpeedAddr) / 10.0f;
    readFirmwareVersion(cachedFirmwareVersion, sizeof(cachedFirmwareVersion));
    yield();

    cachedLowPallet      = arePalletsLow();
    cachedBeepEnabled    = stoveBeepIsEnabled();
    cachedNumberOfStarts = numberOfStarts();
    yield();

    cachedHoursSinceLastService = stove.read(eeprom, hoursSinceLastServiceAddr);
    cachedTotalRuntime          = stove.read(eeprom, totalRuntimeAddrLow) | (stove.read(eeprom, totalRuntimeAddrHigh) << 8);
    yield();

    cachedCombustionQuality = stove.read(eeprom, combustionQualityAddr);
    cachedPelletQuality     = stove.read(eeprom, pelletQualityAddr);
    yield();

    for (int i = 0; i < 5; i++) {
        cachedErrors[i] = getErrorMemory(i);
        yield();
    }

    for (int i = 0; i < 6; i++) {
        byte base = timerBases[i];
        cachedTimerEnabled[i]  = stove.read(eeprom, base + TIMER_ONOFF_OFFSET);
        cachedTimerOnVal[i]    = stove.read(eeprom, base + TIMER_ONTIME_OFFSET);
        cachedTimerOffVal[i]   = stove.read(eeprom, base + TIMER_OFFTIME_OFFSET);
        cachedTimerSetpoint[i] = stove.read(eeprom, base + TIMER_SETPOINT_OFFSET);
        cachedTimerFlame[i]    = stove.read(eeprom, base + TIMER_FLAME_OFFSET);
        cachedTimerFan[i]      = stove.read(eeprom, base + TIMER_FAN_OFFSET);
        cachedTimerDowMask[i]  = stove.read(eeprom, base + TIMER_DOW_OFFSET);
        yield();
    }

    if (stoveMutex) xSemaphoreGive(stoveMutex);

    getStoveDateTime();
}

// Full refresh — used at startup and for manual sync
void getStates()
{
    getFastStates();
    getSlowStates();
}

static void sensorTask(void* pvParameters)
{
    const TickType_t delayTicks = pdMS_TO_TICKS(500);
    uint8_t slowCounter = 0;
    for (;;)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            getFastStates();
            if (++slowCounter >= 30) // every ~15 s
            {
                getSlowStates();
                slowCounter = 0;
            }
        }
        vTaskDelay(delayTicks);
    }
}

void fullReset()
{
    onboardLED.off();
    onboardLED.flash(20, 50, 50, 50, 50);
    Serial.println("Resetting…");
    wm.resetSettings();
    SPIFFS.format();
    Serial.println("[DONE]");
    onboardLED.flash();
    ESP.restart();
}

void buttonPressed()
{
    led.toggle();
    Serial.println("Button has been pressed");
}

bool setDisplay(int menu)
{
    stove.write(ram, displayMenuAddr, menu);
    return true;
}

bool setEnergySavingStart(int start)
{
    if ((start >= 0) && (start <= 3))
    {
        if (stove.write(eeprom, energySavingStartAddr, start) ) {
            delay(20); // delay to ensure the stove has time to process the first write before we read back the value
            return true;
        } else {
            return false;
        }
    }
    else
    {
        log_e("Start not in range, it should be between 0 and 3\n");
        return false;
    }
}

bool setEnergySavingStop(int stop)
{
    if ((stop >= 0) && (stop <= 3))
    {
        if (stove.write(eeprom, energySavingStopAddr, stop) ) {
            delay(20); // delay to ensure the stove has time to process the first write before we read back the value
            return true;
        } else {
            return false;
        }
    }
    else
    {
        log_e("Stop not in range, it should be between 0 and 3\n");
        return false;
    }
}

bool setRoomFanSpeed(int speed)
{
    if ((speed >= 0) && (speed <= 3))
    {
        if (stove.write(eeprom, roomFanEepromAddr, speed) ) {
            return stove.write(ram, roomFanAddr, speed);
        } else {
            return false;
        }
    }
    else
    {
        log_e("Speed not in range, it should be between 0-3\n");
        return false;
    }
}

void setCombustionQuality(int speed)
{
    stove.write(eeprom, combustionQualityAddr, speed);
}

int readErrorMemory(int pos)
{
    switch (pos)
    {
    case 0 ... 4:
        pos = errorMemoryBaseAddr + pos;
        return stove.read(eeprom, byte(pos));
        break;
    }
    return -1;
}

int getErrorMemory(int pos)
{
    int error = -1;
    switch (pos)
    {
    case 0 ... 4:
        pos = errorMemoryBaseAddr + pos;
        error = stove.read(eeprom, byte(pos));
        break;
    }
    return error;
}

bool setFlamePower(int power)
{
    if ((power >= 1) && (power <= 4))
    {
        return stove.write(eeprom, flamePowerEepromAddr, power);
    }
    else
    {
        log_e("Flamepower not in range, it should be between 0-3\n");
        return false;
    }
}

void setError(int pos, int error)
{
    int address = errorMemoryBaseAddr + pos;
    stove.write(eeprom, address, error);
}

void handleRouteCQ()
{
    StaticJsonDocument<350> json;
    json.clear();

    for (uint8_t i = 0; i < wm.server->args(); i++)
    {
        if (wm.server->argName(i) == "s")
        {
            int speed = wm.server->arg(i).toInt();
            setCombustionQuality(speed);
        }
    }

    json["action"] = "ok";

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void handleRouteDisplay()
{
    StaticJsonDocument<350> json;
    json.clear();

    for (uint8_t i = 0; i < wm.server->args(); i++)
    {
        if (wm.server->argName(i) == "menu")
        {
            int menu = wm.server->arg(i).toInt();
            setDisplay(menu);
        }
    }

    json["action"] = "ok";

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

const char* getCombustionQualityLabel(int val)
{
    switch (val)
    {
        case 76: return "+G";
        case 2:  return "-3";
        case 3:  return "-2";
        case 4:  return "-1";
        case 5:  return "0";
        case 6:  return "1";
        case 7:  return "2";
        case 8:  return "3";
        default: return nullptr;
    }
}

const char* getPelletQualityFromLabel(int val)
{
    switch (val)
    {
        case 0:  return "-5";
        case 1:  return "-4";
        case 2:  return "-3";
        case 3:  return "-2";
        case 4:  return "-1";
        case 5:  return "0";
        case 6:  return "1";
        case 7:  return "2";
        case 8:  return "3";
        case 9:  return "4";
        case 10: return "5";
        default: return nullptr;
    }
}

int combustionQualityFromLabel(const String& label)
{
    if (label == "+G") return 76;
    if (label == "-3") return 2;
    if (label == "-2") return 3;
    if (label == "-1") return 4;
    if (label == "0")  return 5;
    if (label == "1")  return 6;
    if (label == "2")  return 7;
    if (label == "3")  return 8;
    return -1;
}

int pelletQualityFromLabel(const String& label)
{
    if (label == "-5") return 0;
    if (label == "-4") return 1;
    if (label == "-3") return 2;
    if (label == "-2") return 3;
    if (label == "-1") return 4;
    if (label == "0")  return 5;
    if (label == "1")  return 6;
    if (label == "2")  return 7;
    if (label == "3")  return 8;
    if (label == "4")  return 9;
    if (label == "5")  return 10;
    return -1;
}

void handleRouteBurn()
{
    StaticJsonDocument<200> json;
    int http_code = 200;
    json.clear();

    if (wm.server->method() == HTTP_POST)
    {
        for (uint8_t i = 0; i < wm.server->args(); i++)
        {
            if (wm.server->argName(i) == "combustionQuality")
            {
                int val = combustionQualityFromLabel(wm.server->arg(i));
                if (val != -1)
                {
                    setCombustionQuality(val);
                    cachedCombustionQuality = val;
                    json["result"] = "success";
                }
                else
                {
                    json["result"] = "failed - combustionQuality must be one of: +G, -3, -2, -1, 0, +1, +2, +3";
                    http_code = 400;
                }
            }
            if (wm.server->argName(i) == "pelletQuality")
            {
                int val = pelletQualityFromLabel(wm.server->arg(i));
                if (val != -1)
                {
                    stove.write(eeprom, pelletQualityAddr, val);
                    cachedPelletQuality = val;
                    json["result"] = "success";
                }
                else
                {
                    json["result"] = "failed - pelletQuality must be one of: +G, -3, -2, -1, 0, +1, +2, +3";
                    http_code = 400;
                }
            }
        }
    }
    else if (wm.server->method() != HTTP_GET)
    {
        wm.server->send(405);
        return;
    }

    int cq = (wm.server->method() == HTTP_GET) ? stove.read(eeprom, combustionQualityAddr) : cachedCombustionQuality;
    const char* cqLabel = getCombustionQualityLabel(cq);
    json["combustionQuality"] = cqLabel ? cqLabel : "Unknown";

    int pq = (wm.server->method() == HTTP_GET) ? stove.read(eeprom, pelletQualityAddr) : cachedPelletQuality;
    const char* pqLabel = getPelletQualityFromLabel(pq);
    json["pelletQuality"] = pqLabel ? pqLabel : "Unknown";

    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
}

const char* getSelectedParameterName(int id)
{
    switch (id)
    {
        case 0:   return nullptr;
        case 71:  return "Multi Comfort";
        case 76:  return "Beep";
        case 79:  return "Display settings";
        case 80:  return "Language";
        case 123: return "State Stove";
        case 128: return "Flame power";
        case 130: return "MultiFuoco";
        case 145: return "Energy Saving - Stop";
        case 146: return "Energy Saving - Start";
        case 193: return "Combustion quality";
        case 198: return "Pallet quality";
        case 200: return "Multi Comfort Plus";
        case 235: return "WPS";
        case 236: return "Reset";
        case 248: return "Day of week";
        case 249: return "Hour";
        case 250: return "min";
        case 251: return "Day";
        case 252: return "Month";
        case 253: return "year";
        default:
            static char unknownParamBuf[16];
            snprintf(unknownParamBuf, sizeof(unknownParamBuf), "(%d)", id);
            return unknownParamBuf;
    }
}

const char* getRemoteMenuName(int id)
{
    switch (id)
    {
        case 0:  return "Main screen";
        case 1:  return "Multifuoco";
        case 2:  return "Chrono";
        case 3:  return "Language";
        case 5:  return "Clock";
        case 7:  return "Multicomfort";
        case 8:  return "Multi Comfort Plus";
        case 9:  return "Energy Saving";
        case 11: return "Beep";
        case 12: return "Display";
        case 13: return "Installer";
        case 39: return "Installer > Fill Auger";
        case 40: return "Installer > Pallet reserve";
        case 41: return "Installer > Pallet contact";
        case 42: return "Installer > External Thermo";
        case 43: return "Installer > Memory";
        case 51: return "Installer > Memory > Total Hours";
        case 52: return "Installer > Memory > Interval hours";
        case 53: return "Installer > Memory > Amount of starts";
        case 54: return "Installer > Memory > Error Memory";
        case 44: return "Installer > Pallet quality";
        case 45: return "Installer > Combustion quality";
        case 46: return "Installer > Setting wireless";
        case 47: return "Installer > Link wireless";
        case 49: return "Installer > Wifi";
        case 64: return "Installer > Wifi > WPS";
        case 65: return "Installer > Wifi > Wifi Reset";
        case 14: return "State Stove";
        case 50: return "Installer > Parameters";
        case 19: return "Installer > Parameters > Factory Reset";
        case 20: return "Installer > Parameters > Databank";
        case 21: return "Installer > Parameters > Smoke Ext Settings";
        case 22: return "Installer > Parameters > Load Settings";
        case 23: return "Installer > Parameters > MultiFUOCO v1";
        case 26: return "Installer > Parameters > Reset Alarm";
        case 25: return "Installer > Parameters > Reset Parthour";
        case 27: return "Installer > Parameters > Encoder";
        case 15: return "exit";
        default:
            static char unknownMenuBuf[16];
            snprintf(unknownMenuBuf, sizeof(unknownMenuBuf), "(%d)", id);
            return unknownMenuBuf;
    }
}

static bool isKnownRamAddr(byte addr)
{
    switch (addr) {
        case 0x01: case 0x04: case 0x0D: case 0x18: case 0x1A:
        case 0x21: case 0x31: case 0x32: case 0x33: case 0x34:
        case 0x37: case 0x3E: case 0x44: case 0x48: case 0x4D:
        case 0x50: case 0x7A: case 0x7B: case 0x7C: case 0x7D:
        case 0x7E: case 0x7F: case 0x80: case 0x8F: case 0xD0:
        case 0xD3: case 0xDF:
            return true;
        default: return false;
    }
}

static bool isKnownEepromAddr(byte addr)
{
    switch (addr) {
        case 0x02: case 0x04: case 0x05: case 0x06: case 0x07:
        case 0x08: case 0x09: case 0x48: case 0x49: case 0x4B:
        case 0x4F: case 0x7D: case 0x7F: case 0x81: case 0x8A:
        case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
        case 0x90: case 0x91: case 0xBC: case 0xC0: case 0xC4:
        case 0xC6: case 0xE0: case 0xE1: case 0xE2: case 0xE3:
        case 0xE4: case 0xE7: case 0xEA: case 0xEB: case 0xEC:
        case 0xEE: case 0xEF:
            return true;
        default: return false;
    }
}

static String getRamValueTxt(byte addr, int val)
{
    if (val < 0) return "read error";
    char buf[64];
    switch (addr) {
        case 0x01: snprintf(buf, sizeof(buf), "%.1f °C (ambient temp)", val / 2.0f);          return buf;
        case 0x04: return val ? "Yes (hot enough for fan)" : "No";
        case 0x0D: snprintf(buf, sizeof(buf), "%.1f rpm (auger speed)", val / 10.0f);          return buf;
        case 0x18: { const char* p = getSelectedParameterName(val); return p ? p : ""; }
        case 0x1A: return getRemoteMenuName(val);
        case 0x21:
            switch (val) {
                case 0:  return "Off";
                case 1:  return "Starting";
                case 2:  return "Pellet loading";
                case 3:  return "Ignition";
                case 4:  return "Working";
                case 5:  return "Brazier cleaning";
                case 6:  return "Final cleaning";
                case 7:  return "Standby";
                case 8:  return "Alarm State";
                case 9:  return "Ignition failure";
                case 10: return "Alarm";
                default: snprintf(buf, sizeof(buf), "Unknown state (%d)", val); return buf;
            }
        case 0x31: snprintf(buf, sizeof(buf), "%d sec (countdown)", val);    return buf;
        case 0x32: snprintf(buf, sizeof(buf), "%d min (brazier cleaning countdown)", val); return buf;
        case 0x33: snprintf(buf, sizeof(buf), "%s - %s", getErrorCode(val).c_str(), getErrorDesc(val).c_str()); return buf;
        case 0x34: snprintf(buf, sizeof(buf), "Flame power %d", val);        return buf;
        case 0x37: snprintf(buf, sizeof(buf), "%d rpm (fume fan)", (val + 25) * 10); return buf;
        case 0x3E: snprintf(buf, sizeof(buf), "%d °C (fume temp)", val);      return buf;
        case 0x44: snprintf(buf, sizeof(buf), "%.1f °C (stove room temp)", val / 2.0f); return buf;
        case 0x48: snprintf(buf, sizeof(buf), "%d rpm (fume fan)", (val + 25) * 10); return buf;
        case 0x4D: return val ? "1 (flip)" : "0 (flop)";
        case 0x50: snprintf(buf, sizeof(buf), "%d min remaining (low pallet)", val); return buf;
        case 0x7A: snprintf(buf, sizeof(buf), "%d sec", val);  return buf;
        case 0x7B: {
            const char* days[] = {"", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
            return (val >= 1 && val <= 7) ? days[val] : "?";
        }
        case 0x7C: snprintf(buf, sizeof(buf), "%02d:xx (hour)", val);  return buf;
        case 0x7D: snprintf(buf, sizeof(buf), "xx:%02d (min)", val);   return buf;
        case 0x7E: snprintf(buf, sizeof(buf), "Day %d", val);          return buf;
        case 0x7F: snprintf(buf, sizeof(buf), "Month %d", val);        return buf;
        case 0x80: snprintf(buf, sizeof(buf), "Year 20%02d", val);     return buf;
        case 0x8F: snprintf(buf, sizeof(buf), "%.1f °C (remote control temp)", val / 2.0f); return buf;
        case 0xD0: snprintf(buf, sizeof(buf), "%.1f °C (stove room temp)", val / 2.0f); return buf;
        case 0xD3: return val ? "Running" : "Stopped";
        case 0xDF: snprintf(buf, sizeof(buf), "%.1f rpm (feed rate)", val / 10.0f); return buf;
        default:   return "";
    }
}

static String getEepromValueTxt(byte addr, int val)
{
    if (val < 0) return "read error";
    char buf[64];
    switch (addr) {
        case 0x02: snprintf(buf, sizeof(buf), "%d min (brazier cleaning interval)", val); return buf;
        case 0x04: snprintf(buf, sizeof(buf), "%.1f kg/h (pellets on ignition)", val / 10.0f); return buf;
        case 0x05: snprintf(buf, sizeof(buf), "%.1f kg/h (pellets level 1)", val / 10.0f);    return buf;
        case 0x06: snprintf(buf, sizeof(buf), "%.1f kg/h (pellets level 2)", val / 10.0f);    return buf;
        case 0x07: snprintf(buf, sizeof(buf), "%.1f kg/h (pellets level 3)", val / 10.0f);    return buf;
        case 0x08: snprintf(buf, sizeof(buf), "%.1f kg/h (pellets level 4)", val / 10.0f);    return buf;
        case 0x09: snprintf(buf, sizeof(buf), "%.1f kg/h (pellets level 5)", val / 10.0f);    return buf;
        case 0x48: return val ? "Ext thermostat: Stop" : "Ext thermostat: Modulate";
        case 0x49: return val ? "Ext thermostat: N.C." : "Ext thermostat: N.O.";
        case 0x4B: return val ? "Beep enabled" : "Beep disabled";
        case 0x4F: {
            const char* langs[] = {"", "ENG", "FRA", "DEU", "NLD", "POR", "SPA", "HRV", "DAN"};
            return (val >= 1 && val <= 8) ? langs[val] : "Unknown language";
        }
        case 0x7D: snprintf(buf, sizeof(buf), "%d °C (setpoint room temp)", val); return buf;
        case 0x7F: snprintf(buf, sizeof(buf), "Flame power %d", val);             return buf;
        case 0x81: snprintf(buf, sizeof(buf), "Room fan speed %d", val);           return buf;
        case 0x90: snprintf(buf, sizeof(buf), "+%d °C (energy saving stop offset)", val);  return buf;
        case 0x91: snprintf(buf, sizeof(buf), "-%d °C (energy saving start offset)", val); return buf;
        case 0x8A: snprintf(buf, sizeof(buf), "%c (FW variant char 0)", (char)val); return buf;
        case 0x8B: snprintf(buf, sizeof(buf), "%c (FW variant char 1)", (char)val); return buf;
        case 0x8C: snprintf(buf, sizeof(buf), "FW product %d", val);  return buf;
        case 0x8D: snprintf(buf, sizeof(buf), "FW major %d", val);    return buf;
        case 0x8E: snprintf(buf, sizeof(buf), "FW minor %d", val);    return buf;
        case 0x8F: snprintf(buf, sizeof(buf), "FW patch %d", val);    return buf;
        case 0xBC: return getCombustionQualityLabel(val);  // pellet quality shares same labels
        case 0xC0: return getCombustionQualityLabel(val);
        case 0xC4: return val ? "Pallet contact N.C." : "Pallet contact N.O.";
        case 0xC6: return val ? "Low pallet monitoring ON" : "Low pallet monitoring OFF";
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4:
            snprintf(buf, sizeof(buf), "%s - %s", getErrorCode(val).c_str(), getErrorDesc(val).c_str());
            return buf;
        case 0xE7: snprintf(buf, sizeof(buf), "%d hours since last service", val); return buf;
        case 0xEA: snprintf(buf, sizeof(buf), "%d (total runtime low byte)", val); return buf;
        case 0xEB: snprintf(buf, sizeof(buf), "%d (total runtime high byte)", val); return buf;
        case 0xEC: snprintf(buf, sizeof(buf), "%d working hours", val); return buf;
        case 0xEE: snprintf(buf, sizeof(buf), "%d (starts low byte)", val);  return buf;
        case 0xEF: snprintf(buf, sizeof(buf), "%d (starts high byte)", val); return buf;
        default:   return "";
    }
}

static String jsonEscape(const String& s)
{
    String out;
    out.reserve(s.length() + 4);
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s[i];
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    return out;
}

static void parseAddressList(const String& param, bool out[256])
{
    if (param.length() == 0) {
        for (int i = 0; i < 256; i++) out[i] = true;
        return;
    }
    memset(out, 0, 256 * sizeof(bool));
    String csv = param + ",";
    int start = 0;
    for (unsigned int i = 0; i < csv.length(); i++) {
        if (csv[i] == ',') {
            String tok = csv.substring(start, i);
            tok.trim();
            if (tok.length() > 0) {
                int addr = (tok.startsWith("0x") || tok.startsWith("0X"))
                           ? (int)strtol(tok.c_str(), nullptr, 16)
                           : tok.toInt();
                if (addr >= 0 && addr <= 255) out[addr] = true;
            }
            start = i + 1;
        }
    }
}

void handleRouteDump()
{
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }

    // --- register param ---
    String regParam = wm.server->arg("register");
    if (regParam.length() == 0) regParam = "ram";
    regParam.toLowerCase();
    bool doRam         = regParam.indexOf("all") >= 0 || regParam.indexOf("ram")    >= 0;
    bool doEeprom      = regParam.indexOf("all") >= 0 || regParam.indexOf("eeprom") >= 0;
    bool filterUnknown = regParam.indexOf("unknown") >= 0;

    // --- address param ---
    bool addrs[256];
    parseAddressList(wm.server->arg("address"), addrs);

    struct RegDef {
        bool        enabled;
        byte        type;
        const char* label;
        String      (*txtFn)(byte, int);
        bool        (*isKnownFn)(byte);
    } regs[] = {
        { doRam,    ram,    "RAM(0x00)",    getRamValueTxt,    isKnownRamAddr    },
        { doEeprom, eeprom, "EEPROM(0x20)", getEepromValueTxt, isKnownEepromAddr },
    };

    String out;
    out.reserve(24000);
    out += "{";
    bool firstReg = true;

    for (auto& r : regs) {
        if (!r.enabled) continue;

        if (!firstReg) out += ",";
        firstReg = false;
        out += "\"";
        out += r.label;
        out += "\":{";

        bool firstAddr = true;
        for (int addr = 0; addr <= 0xFF; addr++) {
            if (!addrs[addr]) continue;
            if (filterUnknown && r.isKnownFn((byte)addr)) continue;
            esp_task_wdt_reset();
            int val      = stove.read(r.type, (byte)addr);
            uint8_t uval = (val >= 0) ? (uint8_t)val : 0;
            String  txt  = jsonEscape(r.txtFn((byte)addr, val));

            char header[72];
            snprintf(header, sizeof(header),
                "%s\"0x%02X\":{\"Address_int\":%d,\"Value_int\":%d,\"Value_txt\":\"",
                firstAddr ? "" : ",",
                addr, addr, (val >= 0 ? val : -1));
            out += header;
            out += txt;

            char footer[24];
            snprintf(footer, sizeof(footer), "\",\"Value_hex\":\"0x%02X\"}", uval);
            out += footer;
            firstAddr = false;
        }
        out += "}";
    }

    out += "}";
    wm.server->send(200, "application/json", out);
}

void handleRouteRemote()
{
    StaticJsonDocument<200> json;
    json.clear();

    if (wm.server->method() != HTTP_GET)
    {
        wm.server->send(405);
        return;
    }

    int menuId = stove.read(ram, displayMenuAddr);
    if (menuId < 0)
    {
        json["error"] = "Failed to read remote menu - RS232 error";
        serializeJson(json, buffer);
        wm.server->send(500, "application/json", buffer);
        return;
    }

    json["diplayed_menu"] = getRemoteMenuName(menuId);

    int paramId = stove.read(ram, selectedParamAddr);
    json["selected_parameter"] = (paramId >= 0) ? getSelectedParameterName(paramId) : "Unknown";

    int langId = stove.read(eeprom, languageAddr);
    if (langId < 0)
    {
        json["language"] = "Unknown";
    }
    else
    {
        switch (langId)
        {
            case 1: json["language"] = "ENG"; break;
            case 2: json["language"] = "FRA"; break;
            case 3: json["language"] = "DEU"; break;
            case 4: json["language"] = "NLD"; break;
            case 5: json["language"] = "POR"; break;
            case 6: json["language"] = "SPA"; break;
            case 7: json["language"] = "HRV"; break;
            case 8: json["language"] = "DAN"; break;
            default: json["language"] = "Unknown"; break;
        }
    }

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void getStoveDateTime()
{
    if (stoveMutex) {
        xSemaphoreTake(stoveMutex, portMAX_DELAY);
    }

    int cur_min = stove.read(ram, stoveTimeMinAddr);
    int cur_hour = stove.read(ram, stoveTimeHourAddr);
    int cur_sec = stove.read(ram, stoveTimeSecAddr);
    int cur_day = stove.read(ram, stoveTimeDayAddr);
    int cur_month = stove.read(ram, stoveTimeMonthAddr);
    int cur_year = stove.read(ram, stoveTimeYearAddr);

    if (stoveMutex) {
        xSemaphoreGive(stoveMutex);
    }

    char stoveTimeBuffer[32];
    struct tm stove_tm = {0};

    snprintf(stoveTimeBuffer, sizeof(stoveTimeBuffer), "20%i-%02d-%02d %02d:%02d:%02d",
             cur_year, cur_month, cur_day, cur_hour, cur_min, cur_sec);

    strptime(stoveTimeBuffer, "%Y-%m-%d %H:%M:%S", &stove_tm);
    // Take the current NTP based DST setting in order to interprete the stove it's Time
    // The stove does not support DST so a time change is required
    stove_tm.tm_isdst = currentDst;
    time_t newStoveTime = mktime(&stove_tm);
    if (newStoveTime != (time_t)-1) {
        stoveTime = newStoveTime;
    }
}

void handleRouteSwaggerUI() { 
    wm.server->send(200, "text/html", swaggerUI);
}

void handleRouteSwaggerJSON() { 
    wm.server->send(200, "application/json", swaggerJSON);
}

void handleRouteTime()
{
    StaticJsonDocument<350> json;
    char time_output[30];
    int http_code;
    http_code=200;

    json.clear();

    if (wm.server->method() == HTTP_PATCH) 
    {
        setStoveDateTime();
        delay(200);
        getStoveDateTime();
        json["result"] = "success Synced NTP time to stove";
    }
    
    if (wm.server->method() == HTTP_GET || wm.server->method() == HTTP_PATCH )
    {
        JsonObject time = json.createNestedObject("time");
        strftime(time_output, 30, "%F %T", localtime(&stoveTime));
        time["stove"] = time_output;

        strftime(time_output, 30, "%F %T", localtime(&now));
        time["ntp"] = time_output;

        time["delta"] = now - stoveTime;
    } else
    {
        http_code=405;
    }


    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
}

void handleRouteEnergysaving()
{
    StaticJsonDocument<350> json;
    int http_code;
    http_code=200;
    json.clear();

    if (wm.server->method() == HTTP_POST)
    {
        for (uint8_t i = 0; i < wm.server->args(); i++)
        {
            if (wm.server->argName(i) == "start")
            {
                if (  wm.server->arg(i) == "0" ||  wm.server->arg(i) == "-1" ||  wm.server->arg(i) == "-2" ||  wm.server->arg(i) == "-3") 
                {
                    int start = wm.server->arg(i).toInt() * -1;
                    if ( setEnergySavingStart(start) )
                    {
                        json["result"] = "success";
                    } else 
                    {
                        json["result"] = "Failed - RS2232 error...";
                        http_code=500;
                    }
                } else
                {
                    json["result"] = "failed - start is out of range: Should be between -3 and 0";
                    http_code=400;
                }
            }
            if (wm.server->argName(i) == "stop")
            {
                if (  wm.server->arg(i) == "0" ||  wm.server->arg(i) == "1" ||  wm.server->arg(i) == "2" ||  wm.server->arg(i) == "3") 
                {
                    int stop = wm.server->arg(i).toInt();
                    if ( setEnergySavingStop(stop) )
                    {
                        json["result"] = "success";
                    } else 
                    {
                        json["result"] = "Failed - RS2232 error...";
                        http_code=500;
                    }
                } else
                {
                    json["result"] = "failed - start is out of range: Should be between -3 and 0";
                    http_code=400;
                }
            }
        }
    }
    
    if (wm.server->method() == HTTP_GET || wm.server->method() == HTTP_POST)
    {
        delay(20);
        getEnergySaving();
        JsonObject obj_energysaving = json.createNestedObject("energySaving");
        obj_energysaving["start"] = energySavingStart;
        obj_energysaving["stop"] = energySavingStop;
        obj_energysaving["enabled"] = (energySavingStart != 0 || energySavingStop != 0);
        if (energySavingStart != 0 || energySavingStop != 0) {
            obj_energysaving["startTemperature"] = setPointTemp + energySavingStart;
            obj_energysaving["stopTemperature"] = setPointTemp + energySavingStop;
        }


    } else {
        wm.server->send(405);
    }

    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
}

void handleRouteBeep()
{
    StaticJsonDocument<128> json;

    if (wm.server->method() == HTTP_POST)
    {
        String val = wm.server->arg("enabled");
        if (val != "true" && val != "false" && val != "1" && val != "0")
        {
            json["result"] = "failed - expected enabled=true/false";
            serializeJson(json, buffer);
            wm.server->send(400, "application/json", buffer);
            return;
        }
        bool enable = (val == "true" || val == "1");
        bool ok = stove.write(eeprom, beepEnabledAddr, enable ? 1 : 0);
        if (!ok)
        {
            json["result"] = "failed - RS232 error";
            serializeJson(json, buffer);
            wm.server->send(500, "application/json", buffer);
            return;
        }
        cachedBeepEnabled = enable;
        json["result"]  = "success";
        json["enabled"] = cachedBeepEnabled;
        serializeJson(json, buffer);
        wm.server->send(200, "application/json", buffer);
        return;
    }

    if (wm.server->method() == HTTP_GET)
    {
        cachedBeepEnabled = stoveBeepIsEnabled();
        json["enabled"] = cachedBeepEnabled;
        serializeJson(json, buffer);
        wm.server->send(200, "application/json", buffer);
        return;
    }

    wm.server->send(405);
}

static int timeStrToTimerValue(const char* str)
{
    if (strcmp(str, "off") == 0) return 144;
    int h = 0, m = 0;
    if (sscanf(str, "%d:%d", &h, &m) != 2) return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
    int totalMins = h * 60 + m;
    if (totalMins % 10 != 0) return -1;
    return totalMins / 10;
}

// ─────────────────────────────────────────────
// Timer helpers shared by /api/timers and /api/timers/prN
// ─────────────────────────────────────────────

static int timerIndexFromUri(const String& uri)
{
    const char* prNames[6] = {"pr1", "pr2", "pr3", "pr4", "pr5", "pr6"};
    String lower = uri;
    lower.toLowerCase();
    for (int i = 0; i < 6; i++)
        if (lower.endsWith(prNames[i])) return i;
    return -1;
}

static void serializeTimerToJson(int idx, JsonObject timer)
{
    byte base    = timerBases[idx];
    int  enabled = stove.read(eeprom, base + TIMER_ONOFF_OFFSET);
    timer["enabled"] = enabled > 0;
    if (enabled > 0)
    {
        timer["on"]  = timerValueToTimeStr(stove.read(eeprom, base + TIMER_ONTIME_OFFSET));
        timer["off"] = timerValueToTimeStr(stove.read(eeprom, base + TIMER_OFFTIME_OFFSET));

        int dow = stove.read(eeprom, base + TIMER_DOW_OFFSET);
        JsonObject enabledOn = timer.createNestedObject("enabled_on");
        enabledOn["monday"]    = (dow & 1)  != 0;
        enabledOn["tuesday"]   = (dow & 2)  != 0;
        enabledOn["wednesday"] = (dow & 4)  != 0;
        enabledOn["thursday"]  = (dow & 8)  != 0;
        enabledOn["friday"]    = (dow & 16) != 0;
        enabledOn["saturday"]  = (dow & 32) != 0;
        enabledOn["sunday"]    = (dow & 64) != 0;

        JsonObject settings = timer.createNestedObject("settings");
        settings["setpoint"]     = stove.read(eeprom, base + TIMER_SETPOINT_OFFSET);
        settings["flame_power"]  = stove.read(eeprom, base + TIMER_FLAME_OFFSET);
        settings["RoomFanSpeed"] = stove.read(eeprom, base + TIMER_FAN_OFFSET);
    }
}

// Partial write — only touches keys present in the JSON object
static void applyTimerJson(int idx, JsonObject timer)
{
    byte base = timerBases[idx];

    if (timer.containsKey("enabled"))
        stove.write(eeprom, base + TIMER_ONOFF_OFFSET, (timer["enabled"] | false) ? 1 : 0);

    if (timer.containsKey("on"))
    {
        int val = timeStrToTimerValue(timer["on"].as<const char*>());
        if (val >= 0) stove.write(eeprom, base + TIMER_ONTIME_OFFSET, val);
    }

    if (timer.containsKey("off"))
    {
        int val = timeStrToTimerValue(timer["off"].as<const char*>());
        if (val >= 0) stove.write(eeprom, base + TIMER_OFFTIME_OFFSET, val);
    }

    if (timer.containsKey("enabled_on"))
    {
        JsonObject dow = timer["enabled_on"].as<JsonObject>();
        byte dowVal = 0;
        if (dow["monday"]    | false) dowVal |= 1;
        if (dow["tuesday"]   | false) dowVal |= 2;
        if (dow["wednesday"] | false) dowVal |= 4;
        if (dow["thursday"]  | false) dowVal |= 8;
        if (dow["friday"]    | false) dowVal |= 16;
        if (dow["saturday"]  | false) dowVal |= 32;
        if (dow["sunday"]    | false) dowVal |= 64;
        stove.write(eeprom, base + TIMER_DOW_OFFSET, dowVal);
    }

    if (timer.containsKey("settings"))
    {
        JsonObject settings = timer["settings"].as<JsonObject>();
        if (settings.containsKey("setpoint"))
        {
            int sp = settings["setpoint"].as<int>();
            if (sp >= 7 && sp <= 30) stove.write(eeprom, base + TIMER_SETPOINT_OFFSET, sp);
        }
        if (settings.containsKey("flame_power"))
        {
            int fp = settings["flame_power"].as<int>();
            if (fp >= 1 && fp <= 5) stove.write(eeprom, base + TIMER_FLAME_OFFSET, fp);
        }
        if (settings.containsKey("RoomFanSpeed"))
        {
            int fs = settings["RoomFanSpeed"].as<int>();
            if (fs >= 0 && fs <= 3) stove.write(eeprom, base + TIMER_FAN_OFFSET, fs);
        }
    }
}

// ─────────────────────────────────────────────
// GET/POST  /api/timers  — all timers
// ─────────────────────────────────────────────

void handleRouteTimers()
{
    const char* prNames[6] = {"PR1", "PR2", "PR3", "PR4", "PR5", "PR6"};

    if (wm.server->method() == HTTP_GET)
    {
        DynamicJsonDocument json(2048);
        JsonObject timers = json.createNestedObject("timers");
        for (int i = 0; i < 6; i++)
            serializeTimerToJson(i, timers.createNestedObject(prNames[i]));
        serializeJson(json, buffer);
        wm.server->send(200, "application/json", buffer);
        return;
    }

    if (wm.server->method() == HTTP_POST)
    {
        String body = wm.server->arg("plain");
        if (body.length() == 0) { wm.server->send(400, "application/json", "{\"error\":\"Empty body\"}"); return; }
        DynamicJsonDocument input(2048);
        if (deserializeJson(input, body)) { wm.server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
        if (!input.containsKey("timers")) { wm.server->send(400, "application/json", "{\"error\":\"Missing 'timers' key\"}"); return; }

        JsonObject timers = input["timers"].as<JsonObject>();
        for (int i = 0; i < 6; i++)
            if (timers.containsKey(prNames[i]))
                applyTimerJson(i, timers[prNames[i]].as<JsonObject>());

        wm.server->send(200, "application/json", "{\"result\":\"success\"}");
        return;
    }

    wm.server->send(405, "application/json", "{\"error\":\"Method not allowed\"}");
}

// ─────────────────────────────────────────────
// GET / POST / PATCH / DELETE  /api/timers/prN
// ─────────────────────────────────────────────

static void sendTimerResponse(int idx)
{
    const char* prNames[6] = {"PR1", "PR2", "PR3", "PR4", "PR5", "PR6"};
    DynamicJsonDocument json(512);
    json["result"] = "success";
    serializeTimerToJson(idx, json.createNestedObject(prNames[idx]));
    String out;
    serializeJson(json, out);
    wm.server->send(200, "application/json", out);
}

void handleRouteTimer()
{
    int idx = timerIndexFromUri(wm.server->uri());
    if (idx < 0) { wm.server->send(404, "application/json", "{\"error\":\"Timer not found\"}"); return; }

    const char* prNames[6] = {"PR1", "PR2", "PR3", "PR4", "PR5", "PR6"};

    // GET — return this timer's current config
    if (wm.server->method() == HTTP_GET)
    {
        DynamicJsonDocument json(512);
        serializeTimerToJson(idx, json.createNestedObject(prNames[idx]));
        String out;
        serializeJson(json, out);
        wm.server->send(200, "application/json", out);
        return;
    }

    // DELETE — disable the timer
    if (wm.server->method() == HTTP_DELETE)
    {
        stove.write(eeprom, timerBases[idx] + TIMER_ONOFF_OFFSET, 0);
        sendTimerResponse(idx);
        return;
    }

    // POST — full replace (all fields required)
    // PATCH — partial update (only provided fields written)
    if (wm.server->method() == HTTP_POST || wm.server->method() == HTTP_PATCH)
    {
        String body = wm.server->arg("plain");

        // Empty body on POST = enable the timer without touching other settings
        if (body.length() == 0)
        {
            if (wm.server->method() == HTTP_POST)
            {
                stove.write(eeprom, timerBases[idx] + TIMER_ONOFF_OFFSET, 1);
                sendTimerResponse(idx);
            }
            else
            {
                wm.server->send(400, "application/json", "{\"error\":\"Empty body\"}");
            }
            return;
        }

        DynamicJsonDocument input(512);
        if (deserializeJson(input, body)) { wm.server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }

        JsonObject timer = input.as<JsonObject>();
        applyTimerJson(idx, timer);
        sendTimerResponse(idx);
        return;
    }

    wm.server->send(405, "application/json", "{\"error\":\"Method not allowed\"}");
}

void handleRouteFanspeed()
{
    StaticJsonDocument<350> json;
    int http_code;
    http_code=200;
    json.clear();

    if (wm.server->method() == HTTP_POST)
    {
        for (uint8_t i = 0; i < wm.server->args(); i++)
        {
            if (wm.server->argName(i) == "speed")
            {
                if (  wm.server->arg(i) == "0" ||  wm.server->arg(i) == "1" ||  wm.server->arg(i) == "2" ||  wm.server->arg(i) == "3") 
                {
                    int speed = wm.server->arg(i).toInt();
                    if ( setRoomFanSpeed(speed) )
                    {
                        json["result"] = "success";
                    } else 
                    {
                        json["result"] = "Failed - RS2232 error...";
                        http_code=500;
                    }
                } else
                {
                    json["result"] = "failed - Speed is out of range: Should be between 0 and 3";
                    http_code=400;
                }
            }
        }
    }
    
    if (wm.server->method() == HTTP_GET || wm.server->method() == HTTP_POST)
    {
        getRoomFanSpeed();
        json["speed"] = roomFanSpeed;
    } else {
        wm.server->send(405);
    }

    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
}

void handleRouteErrors()
{
    StaticJsonDocument<450> json;
    json.clear();

    if (wm.server->method() == HTTP_POST)
    {
        for (uint8_t i = 0; i < wm.server->args(); i++)
        {
            if (wm.server->argName(i) == "MemoryPos_1")
            {
                int error = wm.server->arg(i).toInt();
                setError(0, error);
                json["action"] = "MemoryPos_1 set";
            }
            if (wm.server->argName(i) == "MemoryPos_2")
            {
                int error = wm.server->arg(i).toInt();
                setError(1, error);
                json["action"] = "MemoryPos_2 set";
            }
            if (wm.server->argName(i) == "MemoryPos_3")
            {
                int error = wm.server->arg(i).toInt();
                setError(2, error);
                json["action"] = "MemoryPos_3 set";
            }
            if (wm.server->argName(i) == "MemoryPos_4")
            {
                int error = wm.server->arg(i).toInt();
                setError(3, error);
                json["action"] = "MemoryPos_4 set";
            }
            if (wm.server->argName(i) == "MemoryPos_5")
            {
                int error = wm.server->arg(i).toInt();
                setError(4, error);
                json["action"] = "MemoryPos_5 set";
            }
        }
    }
    
    if (wm.server->method() == HTTP_GET || wm.server->method() == HTTP_POST)
    {
        const char* memKeys[5] = {"MemoryPos_1","MemoryPos_2","MemoryPos_3","MemoryPos_4","MemoryPos_5"};
        int errors[5];
        bool hasErrors = false;
        for (int i = 0; i < 5; i++) {
            errors[i] = getErrorMemory(i);
            if (errors[i] != 0) hasErrors = true;
        }
        if (!hasErrors) {
            json["previousErrors"] = false;
        } else {
            JsonObject obj_errors = json.createNestedObject("previousErrors");
            for (int i = 0; i < 5; i++) {
                if (errors[i] != 0) {
                    JsonObject eb = obj_errors.createNestedObject(memKeys[i]);
                    eb["id"]    = errors[i];
                    eb["error"] = getErrorCode(errors[i]);
                    eb["desc"]  = getErrorDesc(errors[i]);
                }
            }
        }
    } else
    {
        wm.server->send(405);
    }

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void handleRouteSetError()
{
    StaticJsonDocument<350> json;
    json.clear();

    for (uint8_t i = 0; i < wm.server->args(); i++)
    {
        if (wm.server->argName(i) == "0")
        {
            int error = wm.server->arg(i).toInt();
            setError(0, error);
        }
        if (wm.server->argName(i) == "1")
        {
            int error = wm.server->arg(i).toInt();
            setError(1, error);
        }
        if (wm.server->argName(i) == "2")
        {
            int error = wm.server->arg(i).toInt();
            setError(2, error);
        }
        if (wm.server->argName(i) == "3")
        {
            int error = wm.server->arg(i).toInt();
            setError(3, error);
        }
        if (wm.server->argName(i) == "4")
        {
            int error = wm.server->arg(i).toInt();
            setError(4, error);
        }
    }

    json["action"] = "ok";

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void handleRouteHealth()
{
    StaticJsonDocument<350> json;
    json.clear();
    int http_code;
    if (wm.server->method() == HTTP_GET)
    {
        if (stove.read(ram, ambTempAddr)) 
        {
            json["health"] = "Healthy";
            http_code=200;

        } else 
        {
            json["health"] = "Failed to talk to Stove";
            http_code=500;
        }
    } else {
        wm.server->send(405);
    }

    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
}

void handleRouteFlamePower()
{
    StaticJsonDocument<350> json;
    int http_code;
    http_code=200;
    json.clear();


    if (wm.server->method() == HTTP_POST)
    {
        for (uint8_t i = 0; i < wm.server->args(); i++)
        {
            if (wm.server->argName(i) == "power")
            {
                if (  wm.server->arg(i) == "1" ||  wm.server->arg(i) == "2" ||  wm.server->arg(i) == "3" ||  wm.server->arg(i) == "4") 
                {
                    int power = wm.server->arg(i).toInt();
                    if (setFlamePower(power))
                    {
                        json["result"] = "success";
                        http_code=200;
                    } else
                    {
                        json["result"] = "Failed - RS2232 error...";
                        http_code=500;
                    }
                } else
                {
                    json["result"] = "failed - power is out of range: Should be between 1 and 4";
                    http_code=400;
                }
            }
        }
    }
    
    if (wm.server->method() == HTTP_GET || wm.server->method() == HTTP_POST)
    {
        getFlamePower();
        json["power"] = flamePower;
    } else
    {
        wm.server->send(405);
    }

    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
}

// Core power helpers — return the result message and set http_code by reference.
// Caller must have called getStoveState() beforehand.

String doStoveOff(int &http_code)
{
    http_code = 200;
    switch (stoveState)
    {
    case OFF:
    case ALARM_STATE:
    case IGNITION_FAILURE:
    case STANDBY:
        http_code = 202;
        return "Stove already off";
    case STARTING:
    case PALLET_LOADING:
        if (stove.write(ram, stoveStateAddr, 0)) return "Powered off sended - 0";
        http_code = 500;
        return "FAILED to send Power off - 0";
    case IGNITION:
    case WORKING:
    case ALARM:
        if (stove.write(ram, stoveStateAddr, 6)) return "Powered off sended - 6";
        http_code = 500;
        return "FAILED to send Power off - 6";
    case BRAZIER_CLEANING:
        http_code = 202;
        return "Stove already shutting down - Brazier cleaning";
    case FINAL_CLEANING:
        http_code = 202;
        return "Stove already shutting down - Final cleaning";
    default:
        http_code = 500;
        return "Unknown state [RS232 Serial Error]";
    }
}

String doStoveOn(int &http_code)
{
    http_code = 200;
    if (stoveState == OFF)
    {
        if (stove.write(ram, stoveStateAddr, 1)) return "Powered on sended";
        http_code = 500;
        return "FAILED to send Power on";
    }
    http_code = 202;
    return "Stove already on";
}

void handleRoutePower()
{
    StaticJsonDocument<350> json;
    json.clear();
    int http_code = 200;

    getStoveState();

    if (wm.server->method() == HTTP_POST)
    {
        for (uint8_t i = 0; i < wm.server->args(); i++)
        {
            if (wm.server->argName(i) == "state")
            {
                String message;
                if (wm.server->arg(i) == "off")
                {
                    message = doStoveOff(http_code);
                }
                else if (wm.server->arg(i) == "on")
                {
                    message = doStoveOn(http_code);
                }
                else
                {
                    message = "Unknown state argument";
                    http_code = 400;
                }
                json["result"] = message;
            }
        }
    }
    getStoveState();
    json["stoveState"] = stoveState;
    json["state"] = StoveStateStr;
    json["poweredOn"] = StoveIsOn;

    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
}

void setStoveOn()
{
    int http_code = 200;
    StaticJsonDocument<250> json;
    json.clear();
    getStoveState();

    json["result"] = doStoveOn(http_code);

    getStoveState();
    JsonObject obj_stove = json.createNestedObject("stoveState");
    obj_stove["stoveState"] = stoveState;
    obj_stove["state"] = StoveStateStr;
    obj_stove["poweredOn"] = StoveIsOn;

    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
}

void setStoveOff()
{
    int http_code = 200;
    StaticJsonDocument<250> json;
    json.clear();
    getStoveState();

    json["result"] = doStoveOff(http_code);

    getStoveState();
    JsonObject obj_stove = json.createNestedObject("stoveState");
    obj_stove["stoveState"] = stoveState;
    obj_stove["state"] = StoveStateStr;
    obj_stove["poweredOn"] = StoveIsOn;

    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
}

void storeAllRamValues()
{
    //telnet.println("Getting all values : ");
    for (int i = 0; i <= 255; i++)
    {
        esp_task_wdt_reset();
        char value = stove.read(ram, byte(i));
        if (value >= 0)
        {
             telnet.print(".");
            if (storedRam[265] == 1)
            {
                if (value != storedRam[byte(i)])
                {
                    snprintf(buffer, sizeof(buffer), "0x%02X   - 0x%02X\n       + 0x%02X", byte(i), storedRam[byte(i)], value);
                    //telnet.print(buffer);
                }
            }
            storedRam[byte(i)] = value;
        }
    }
    storedRam[265] = 1;
    //telnet.println("[Done]");
}

void getParams()
{
    StaticJsonDocument<350> json;
    json.clear();
    char data[2];

    data[0] = ram;

    for (uint8_t i = 0; i < wm.server->args(); i++)
    {
        if (wm.server->argName(i) == "type")
        {
            if (wm.server->arg(i) == "ram")
            {
                data[0] = ram;
            }
            if (wm.server->arg(i) == "eeprom")
            {
                data[0] = eeprom;
            }
        }

        if (wm.server->argName(i) == "int")
        {
            data[1] = byte(wm.server->arg(i).toInt());
        }
    }

    snprintf(buffer, sizeof(buffer), "0x%02X", data[0]);
    JsonObject type = json.createNestedObject(buffer);

    snprintf(buffer, sizeof(buffer), "0x%02X", data[1]);
    JsonObject address = type.createNestedObject(buffer);

    char value = stove.read(data[0], data[1]);
    if (value >= 0)
    {
        address["Address_int"] = int(data[1]);
        address["Value_int"] = int(value);
        address["Value_txt"] = String(value);
        snprintf(buffer, sizeof(buffer), "0x%02X", value);
        address["Value_hex"] = buffer;
    }
    else
    {
        address["error"] = "Failed to get value RS232 error";
        log_e("Failed to get value");
    }

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void getStatus()
{
    if (wm.server->method() == HTTP_GET)
    {
    //telnet.println("Get status called");

    DynamicJsonDocument json(2048);
    json.clear();

    JsonObject obj_stove = json.createNestedObject("stoveState");
    obj_stove["stoveState"] = stoveState;
    obj_stove["state"] = StoveStateStr;
    obj_stove["poweredOn"] = StoveIsOn;

    if (int_hydro_mode == 1)
    {
        JsonObject hydro = json.createNestedObject("hydro");
        hydro["Pressure"] = waterPres;
        hydro["Temp"] = waterTemp;
    }

    JsonObject obj_flame = json.createNestedObject("flame");
    obj_flame["power"] = flamePower;

    JsonObject obj_temperatures = json.createNestedObject("temperatures");
    obj_temperatures["room"] = ambTemp;
    obj_temperatures["stove"] = ambTempStove;
    obj_temperatures["remoteControl"] = ambTempRemoteControl;
    obj_temperatures["setPoint"] = setPointTemp;
    obj_temperatures["fume"] = fumesTemp;

    JsonObject obj_energysaving = json.createNestedObject("energySaving");
    obj_energysaving["start"] = energySavingStart;
    obj_energysaving["stop"] = energySavingStop;
    obj_energysaving["enabled"] = (energySavingStart != 0 || energySavingStop != 0);
    if (energySavingStart != 0 || energySavingStop != 0) {
        obj_energysaving["startTemperature"] = setPointTemp + energySavingStart;
        obj_energysaving["stopTemperature"] = setPointTemp + energySavingStop;
    }

    json["RoomFanSpeed"] = roomFanSpeed;
    json["lowPalletAlarm"] = cachedLowPallet;
    json["stoveBeepEnabled"] = cachedBeepEnabled;
    json["numberOfStarts"] = cachedNumberOfStarts;
    json["hoursSinceLastService"] = cachedHoursSinceLastService;
    json["totalRuntime"] = cachedTotalRuntime;
    json["firmwareVersion"] = cachedFirmwareVersion;
    json["augerSpeed"] = serialized(String(cachedAugerSpeed, 1));
    JsonObject obj_burn = json.createNestedObject("burn");
    obj_burn["combustionQuality"] = getCombustionQualityLabel(cachedCombustionQuality);
    obj_burn["pelletQuality"] = getCombustionQualityLabel(cachedPelletQuality);
    json["fumeFanRPM"] = cachedFumeFanRPM;
    json["RemoteControlLastSeen"] = cachedRemoteControlLastSeen;

    const char* memKeys[5] = {"MemoryPos_1","MemoryPos_2","MemoryPos_3","MemoryPos_4","MemoryPos_5"};
    bool hasErrors = false;
    for (int i = 0; i < 5; i++) {
        if (cachedErrors[i] != 0) { hasErrors = true; break; }
    }
    if (!hasErrors) {
        json["previousErrors"] = false;
    } else {
        JsonObject obj_errors = json.createNestedObject("previousErrors");
        for (int i = 0; i < 5; i++) {
            if (cachedErrors[i] != 0) {
                JsonObject eb = obj_errors.createNestedObject(memKeys[i]);
                eb["id"]    = cachedErrors[i];
                eb["error"] = getErrorCode(cachedErrors[i]);
                eb["desc"]  = getErrorDesc(cachedErrors[i]);
            }
        }
    }

    char time_output[30];
    JsonObject time = json.createNestedObject("time");
    strftime(time_output, 30, "%F %T", localtime(&now));
    time["ntp"] = time_output;
    strftime(time_output, 30, "%F %T", localtime(&stoveTime));
    time["stove"] = time_output;
    time["delta"] = now - stoveTime;

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
    } else{
        wm.server->send(405);
    }
}

void setupTelnet()
{
    telnet.onConnect([](String ip) { telnetOnConnect(ip); });
    telnet.onInputReceived([](String str) { telnetHandleInput(str); });

    Serial.print("- Telnet: ");
    if (telnet.begin())
    {
        Serial.println("running");
    }
    else
    {
        Serial.println("error.");
    }
}

void setup()
{
    esp_log_level_set("*", ESP_LOG_DEBUG);
    pinMode(ENABLE_RX, OUTPUT);
    digitalWrite(ENABLE_RX, HIGH); // The led of the optocoupler is off

    Serial.begin(115200);
    
    if (SPIFFS.begin()) // Mount SPIFFS
    {
        Serial.println("SPIFFS system mounted with success");
    }
    else
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
    }
    setup_wifi();
    httpRouteMetrics();
    enableOTA();
    File configFile = SPIFFS.open("/config.txt", "r");
    if (configFile) {
        Serial.println("Reading values");
        while (configFile.available())
        {
            String hydroString = configFile.readStringUntil('\n');
            hydro_mode = hydroString.c_str();
            hydro_mode.trim();
            int_hydro_mode = hydro_mode.toInt();
        }
        configFile.close();
    } else {
        Serial.println("Config file not found, using defaults");
    }

    resetButton.begin();
    resetButton.onPressedFor(5000, fullReset);
    resetButton.onPressed(buttonPressed);

    Serial.println("Ready");

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    onboardLED.on();

    wm.server->on("/on", setStoveOn);
    wm.server->on("/off", setStoveOff);
    
    wm.server->on("/fan", handleRouteFanspeed);
    wm.server->on("/error", handleRouteSetError);

    wm.server->on("/cq", handleRouteCQ);
    wm.server->on("/display", handleRouteDisplay);
    wm.server->on("/flame", handleRouteFlamePower);

    wm.server->on("/state", getStatus);
    wm.server->on("/get", handleRouteDump);
    wm.server->on("/set", getParams);

    wm.server->on("/api/flame", handleRouteFlamePower);
    wm.server->on("/api/fan", handleRouteFanspeed);
    wm.server->on("/api/power", handleRoutePower);
    wm.server->on("/api/energysaving", handleRouteEnergysaving);
    wm.server->on("/api/errors", handleRouteErrors);
    wm.server->on("/api/state", getStatus);
    wm.server->on("/api/health", handleRouteHealth);
    wm.server->on("/api/time", handleRouteTime);
    wm.server->on("/api/remote", handleRouteRemote);
    wm.server->on("/api/burn", handleRouteBurn);
    wm.server->on("/api/beep", handleRouteBeep);
    wm.server->on("/api/timers",     handleRouteTimers);
    wm.server->on("/api/timers/pr1", handleRouteTimer);
    wm.server->on("/api/timers/pr2", handleRouteTimer);
    wm.server->on("/api/timers/pr3", handleRouteTimer);
    wm.server->on("/api/timers/pr4", handleRouteTimer);
    wm.server->on("/api/timers/pr5", handleRouteTimer);
    wm.server->on("/api/timers/pr6", handleRouteTimer);
    wm.server->on("/api/dump", handleRouteDump);
    wm.server->on("/api", handleRouteSwaggerUI);    
    wm.server->on("/api-specs.json", handleRouteSwaggerJSON);   

    configTzTime(defaultTimezone, ntpServer); // sets TZ and starts NTP sync
    getNTPtime(2);

    if (isConnected())
    {
        setupTelnet();
    }
    stoveMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(sensorTask, "SensorTask", 8192, NULL, 1, NULL, 0);
    getStates();
}

void loop()
{
    telnet.loop();
    telnetMenuLoop();
    esp_task_wdt_reset();
    wm.process();
    resetButton.read();
    ArduinoOTA.handle();

    unsigned long currentMillis = millis();
    if (previousMillisNTP > currentMillis) { previousMillisNTP = 0; }

    // Sensor cache is refreshed by a background task on the second core.

    // NTP sync + WiFi health check — every 60s
    if (currentMillis - previousMillisNTP >= 60000)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            onboardLED.off();
            onboardLED.flash();
            Serial.println("Reconnecting to WiFi...");
            WiFi.disconnect(false);  // reset state machine, keep credentials
            WiFi.begin();            // reconnect with stored credentials
        }
        else
        {
            getNTPtime(2);
            getStoveDateTime(); // 6 UART reads moved here — runs every 60s instead of every cycle
            onboardLED.on();
        }
        previousMillisNTP = currentMillis;
    }
}