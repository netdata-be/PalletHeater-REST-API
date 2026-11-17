#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ota.h>
#include <constants.h>
#include <setupWifi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <bleUart.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <auth.h>
#include <httpUpdate.h>
#include <metrics.h>

#include <helperFunctions.h>
#include "Stove.h"
#include <telnetMenu.h>
#include <keyWatch.h>
#include <Preferences.h>
#include <esp_bt.h>

#define FORMAT_SPIFFS_IF_FAILED true


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



// Read a register into a cached value. On a serial error the previous value is
// kept, so a failed read never shows up as -1 / 255 in the API, BLE or metrics.
template <typename T>
static bool readKeep(T& dst, byte type, byte addr, float scale = 1.0f)
{
    int v = stove.read(type, addr);
    if (v < 0) return false;
    dst = (T)(v * scale);
    return true;
}

// millis() of the last completed sensor refresh, exposed in /api/state
unsigned long lastFastStatesAt = 0;
unsigned long lastSlowStatesAt = 0;

void getAmbTemp() // Get room temperature
{
    if (!readKeep(ambTemp, ram, ambTempAddr, 0.5f)) log_e("Failed to get room temperature");
#if SENSOR_DEBUG
    Serial.print("T. amb. ");
    Serial.println(ambTemp);
#endif
}

void readFirmwareVersion(char* out, size_t len)
{
    int variant0 = stove.read(eeprom, fwVariantChar0Addr);
    int variant1 = stove.read(eeprom, fwVariantChar1Addr);
    int product  = stove.read(eeprom, fwProductAddr);
    int major    = stove.read(eeprom, fwMajorAddr);
    int minor    = stove.read(eeprom, fwMinorAddr);
    int patch    = stove.read(eeprom, fwPatchAddr);
    if (variant0 < 0 || variant1 < 0 || product < 0 || major < 0 || minor < 0 || patch < 0)
        return;  // keep the previous version string

    snprintf(out, len, "FW-%c%c-%02d-V-%02d-%02d-%02d",
             (char)variant0, (char)variant1, product, major, minor, patch);
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
    if (!readKeep(ambTempStove, ram, ambTempStoveAddr, 0.5f)) log_e("Failed to get stove temperature");
#if SENSOR_DEBUG
    Serial.print("T. amb. stove : ");
    Serial.println(ambTempStove);
#endif
}

void getAmbTempRemoteControl() // Get room temperature from stove sensor
{
    if (!readKeep(ambTempRemoteControl, ram, ambTempRemoteAddr, 0.5f)) log_e("Failed to get remote control temperature");
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

bool stoveBeepIsEnabled() // Get the beep setting
{
    // Use a local: this used to write into the global ambTemp, which made the
    // room temperature show the beep flag (1 = enabled -> "1 °C").
    int val = stove.read(eeprom, beepEnabledAddr);
    if (val < 0) return cachedBeepEnabled;  // keep last known value on a serial error
    return val != 0;
}

int arePalletsLow()
{
    readKeep(cachedPelletSensor, ram, palletSensorAddr);
    readKeep(cachedPelletCounter, ram, palletLevelAddr);
    readKeep(cachedPelletReserveOn, eeprom, pelletReserveEnabledAddr);
    return cachedPelletSensor == 5 && cachedPelletCounter >= 0 && cachedPelletCounter <= 1 ? 1 : 0;
}

void getTempSetpoint()
{
    readKeep(setPointTemp, eeprom, TempSetpointAddr);
#if SENSOR_DEBUG
    Serial.print("T. setpoint. ");
    Serial.println(setPointTemp);
#endif
}

void getFumeTemp() // Get flue gas temperature
{
    readKeep(fumesTemp, ram, fumesTempAddr);
#if SENSOR_DEBUG
    Serial.printf("T. fumes %d\n", fumesTemp);
#endif
}

// The stove stores its clock in EEPROM as BCD (e.g. 16 -> 0x16).
static void writeBcdToStove(int number, byte address)
{
    byte value = (byte)((number / 10) * 0x10 + (number % 10));
    stove.write(eeprom, address, value);
    delay(20);
}

// Write a local date/time to the stove clock. The stove has no DST support,
// so it simply gets the local wall-clock time.
void setStoveDateTimeTm(const struct tm& t)
{
    StoveBusLock lock;
    writeBcdToStove(t.tm_mday,        eepromDateDayAddr);
    writeBcdToStove(t.tm_mon + 1,     eepromDateMonthAddr);
    writeBcdToStove(t.tm_year % 100,  eepromDateYearAddr);
    writeBcdToStove(t.tm_hour,        eepromTimeHourAddr);
    writeBcdToStove(t.tm_min,         eepromTimeMinAddr);
    // Day of week: 1 = Monday ... 7 = Sunday (manual, CLOCK/CHRONO). This used to be
    // written with strftime("%W"), which is the week number, so chrono programs ran
    // on the wrong days.
    writeBcdToStove(t.tm_wday == 0 ? 7 : t.tm_wday, eepromTimeDOWAddr);
}

void setStoveDateTime()   // sync to NTP time
{
    getNTPtime(2);
    struct tm t;
    localtime_r(&now, &t);
    setStoveDateTimeTm(t);
}

void getRoomFanSpeed()
{
    readKeep(roomFanSpeed, eeprom, roomFanEepromAddr);
#if SENSOR_DEBUG
    Serial.printf("Fan speed: %d\n", roomFanSpeed);
#endif
}

void getFlamePower() // Get the flame power (1 - 4)
{
    readKeep(flamePower, eeprom, flamePowerEepromAddr);
}

void getEnergySaving() 
{
    // EEPROM holds the start offset as a positive number; the API exposes it negative
    readKeep(energySavingStart, eeprom, energySavingStartAddr, -1.0f);
    readKeep(energySavingStop,  eeprom, energySavingStopAddr);
}


int getErrorMemory(int pos);
bool getStoveDateTime();
static void stoveTimeTick();

// Re-read one timer program into the cache used by BLE and /metrics.
// Only updates the cache when all 7 registers were read successfully.
static bool refreshTimerCache(int i)
{
    byte base = timerBases[i];
    int v[7] = {
        stove.read(eeprom, base + TIMER_ONOFF_OFFSET),
        stove.read(eeprom, base + TIMER_ONTIME_OFFSET),
        stove.read(eeprom, base + TIMER_OFFTIME_OFFSET),
        stove.read(eeprom, base + TIMER_SETPOINT_OFFSET),
        stove.read(eeprom, base + TIMER_FLAME_OFFSET),
        stove.read(eeprom, base + TIMER_FAN_OFFSET),
        stove.read(eeprom, base + TIMER_DOW_OFFSET),
    };
    for (int k = 0; k < 7; k++) if (v[k] < 0) return false;
    cachedTimerEnabled[i]  = v[0];
    cachedTimerOnVal[i]    = v[1];
    cachedTimerOffVal[i]   = v[2];
    cachedTimerSetpoint[i] = v[3];
    cachedTimerFlame[i]    = v[4];
    cachedTimerFan[i]      = v[5];
    cachedTimerDowMask[i]  = v[6];
    return true;
}

// Fast-changing values — refreshed every 2 s
void getFastStates()
{
    if (stoveMutex) xSemaphoreTakeRecursive(stoveMutex, portMAX_DELAY);

    getStoveState();
    getFumeTemp();
    getFlamePower();
    getRoomFanSpeed();
    int rpm = getFumeFanRPM();
    if (rpm >= 0) cachedFumeFanRPM = rpm;
    readKeep(cachedCurrentError, ram, currentErrorAddr);
    readKeep(cachedFlameActual, ram, flamePowerActualAddr);
    readKeep(cachedRoomFanControl, ram, roomFanControlAddr);
    readKeep(cachedRoomFanRunning, ram, roomFanRunningAddr);
    readKeep(cachedHotForFan, ram, hotForFanAddr);
    readKeep(cachedBrazierCountdown, ram, brazierCountdownAddr);
    cachedRemoteControlLastSeen = getRemoteControlLastSeen();
    lastFastStatesAt = millis();

    if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
}

// Slow-changing values — refreshed every ~16 s
void getSlowStates()
{
    if (stoveMutex) xSemaphoreTakeRecursive(stoveMutex, portMAX_DELAY);

    getAmbTemp();
    getAmbTempStove();
    getAmbTempRemoteControl();
    {
        // INSTELL WIRELESS, bit 0 of RAM 0xB6: 1 = off, 0 = on.
        // On a read error keep the previous value.
        int sel = stove.read(ram, wirelessSensorAddr);
        if (sel >= 0) cachedWirelessSensor = (sel & 0x01) ? 0 : 1;
    }
    getTempSetpoint();
    getEnergySaving();
    yield();

    readKeep(cachedAugerSpeed, ram, augerSpeedAddr, 0.1f);
    readFirmwareVersion(cachedFirmwareVersion, sizeof(cachedFirmwareVersion));
    yield();

    cachedLowPallet      = arePalletsLow();
    cachedBeepEnabled    = stoveBeepIsEnabled();
    int starts = numberOfStarts();
    if (starts >= 0) cachedNumberOfStarts = starts;
    yield();

    readKeep(cachedHoursSinceLastService, eeprom, hoursSinceLastServiceAddr);
    readKeep(cachedBrazierInterval, eeprom, brazierIntervalAddr);
    {
        int lo = stove.read(eeprom, totalRuntimeAddrLow);
        int hi = stove.read(eeprom, totalRuntimeAddrHigh);
        if (lo >= 0 && hi >= 0) cachedTotalRuntime = lo | (hi << 8);
    }
    yield();

    readKeep(cachedCombustionQuality, eeprom, combustionQualityAddr);
    readKeep(cachedPelletQuality,     eeprom, pelletQualityAddr);
    yield();

    for (int i = 0; i < 5; i++) {
        int err = getErrorMemory(i);
        if (err >= 0) cachedErrors[i] = err;
        yield();
    }

    for (int i = 0; i < 6; i++) {
        refreshTimerCache(i);
        yield();
    }

    lastSlowStatesAt = millis();
    if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
}

// Full refresh — used at startup and for manual sync
void getStates()
{
    getFastStates();
    getSlowStates();
    getStoveDateTime();
}

static void sensorTask(void* pvParameters)
{
    const TickType_t delayTicks = pdMS_TO_TICKS(500);
    uint8_t slowCounter = 0;
    uint8_t bleFastCounter = 0;
    uint8_t bleSlowCounter = 0;
    for (;;)
    {
        unsigned long now = millis();
        if (otaActive)                       // firmware update running: stay off the bus and the CPU
        {
            vTaskDelay(delayTicks);
            continue;
        }
        if (keyWatchTick())                  // key watch running: only the watched addresses, as fast as possible
        {
            vTaskDelay(1);
            continue;
        }
        if (bleStoveUartInQuietWindow(now, 1500))
        {
            vTaskDelay(delayTicks);
            continue;
        }

        if (bleIsClientConnected())
        {
            if (bleConnectionAgeMs() < 5000)
            {
                vTaskDelay(delayTicks);
                continue;
            }

            if (++bleFastCounter >= 8) // every ~4 s while BLE is active
            {
                getFastStates();
                bleFastCounter = 0;
            }
            if (++bleSlowCounter >= 120) // every ~60 s while BLE is active
            {
                getSlowStates();
                bleSlowCounter = 0;
            }
            stoveTimeTick();
        }
        else
        {
            bleFastCounter = 0;
            bleSlowCounter = 0;
            getFastStates();
            if (++slowCounter >= 30) // every ~15 s
            {
                getSlowStates();
                slowCounter = 0;
            }
            stoveTimeTick();
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
            delay(20);
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

const char* getPelletQualityLabel(int val)
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

// Normalise a quality label: trim, and drop a leading '+' (or the space that
// an unencoded '+' in a query string decodes to). "+G" -> "G", "+1" -> "1".
static String normaliseQualityLabel(const String& in)
{
    String label = in;
    label.trim();
    if (label.startsWith("+")) label = label.substring(1);
    label.toUpperCase();
    return label;
}

int combustionQualityFromLabel(const String& rawLabel)
{
    String label = normaliseQualityLabel(rawLabel);
    if (label == "G")  return 76;
    if (label == "-3") return 2;
    if (label == "-2") return 3;
    if (label == "-1") return 4;
    if (label == "0")  return 5;
    if (label == "1")  return 6;
    if (label == "2")  return 7;
    if (label == "3")  return 8;
    return -1;
}

int pelletQualityFromLabel(const String& rawLabel)
{
    String label = normaliseQualityLabel(rawLabel);
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
                    if (stove.write(eeprom, combustionQualityAddr, val))
                    {
                        cachedCombustionQuality = val;
                        json["result"] = "success";
                    }
                    else
                    {
                        json["result"] = "failed - RS232 error";
                        http_code = 500;
                    }
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
                    if (stove.write(eeprom, pelletQualityAddr, val))
                    {
                        cachedPelletQuality = val;
                        json["result"] = "success";
                    }
                    else
                    {
                        json["result"] = "failed - RS232 error";
                        http_code = 500;
                    }
                }
                else
                {
                    json["result"] = "failed - pelletQuality must be one of: -5, -4, -3, -2, -1, 0, +1, +2, +3, +4, +5";
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
    const char* pqLabel = getPelletQualityLabel(pq);
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
        case 199: return "Pallet reserve";
        case 200: return "Multi Comfort Plus";
        case 235: return "WPS";
        case 236: return "Reset";
        case 248: return "Day of week";
        case 249: return "Hour";
        case 250: return "min";
        case 217: return "Instell wireless";
        case 251: return "Day";
        case 252: return "Month";
        case 253: return "year";
        default:
        {
            // CHRONO programs: parameter = EEPROM address + 1 of the program field
            static const char* const fields[8] = { "On/Off", "Start time", "Stop time", "Set point",
                                                   "Flame power", "Room fan", nullptr, "Week days" };
            static char chronoBuf[32];
            for (int i = 0; i < 6; i++) {
                int off = id - 1 - timerBases[i];
                if (off >= 0 && off <= 7 && fields[off]) {
                    snprintf(chronoBuf, sizeof(chronoBuf), "PR%d - %s", i + 1, fields[off]);
                    return chronoBuf;
                }
            }
        }
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
        case 46: return "Installer > Instell wireless";
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
        case 0x01: case 0x02: case 0x04: case 0x08: case 0x16: case 0x24: case 0x0A: case 0xA7: case 0x0D: case 0x18: case 0x19: case 0x1A: case 0xB6:
        case 0x27: case 0x36: case 0x81: case 0x82:
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
        case 0x04: if (!val) return "0 (too cold for the room fan)"; snprintf(buf, sizeof(buf), "%d (hot enough for the room fan; follows the flame level)", val); return buf;
        case 0x0D: snprintf(buf, sizeof(buf), "%.1f rpm (auger speed)", val / 10.0f);          return buf;
        case 0x18: { const char* p = getSelectedParameterName(val); return p ? p : ""; }
        case 0x19: snprintf(buf, sizeof(buf), "Menu edit value %d", val); return buf;
        case 0x08: {   // key pressed on the remote (verified 17/11/2026)
            if (!val) return "No key pressed";
            String k = "Key:";
            if (val & 1) k += " DOWN"; if (val & 2) k += " UP"; if (val & 4) k += " ESC"; if (val & 8) k += " OK";
            if (val & ~0x0F) k += " +?";
            return k;
        }
        case 0x16: snprintf(buf, sizeof(buf), "Key event count %d (1 = press, +1 per auto-repeat)", val); return buf;
        case 0x24: snprintf(buf, sizeof(buf), "Key release countdown %d", val); return buf;
        case 0xB6: return (val & 0x01) ? "Wireless temperature sensor off (INSTELL WIRELESS)" : "Wireless temperature sensor on (INSTELL WIRELESS)";
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
        case 0x37: if (!val) return "0 rpm (fume fan stopped)"; snprintf(buf, sizeof(buf), "%d rpm (fume fan)", (val + 25) * 10); return buf;
        case 0x36: if (!val) return "0 rpm (fume fan target, stopped)"; snprintf(buf, sizeof(buf), "%d rpm (fume fan target)", (val + 25) * 10); return buf;
        case 0x02: case 0x27: snprintf(buf, sizeof(buf), "%d (= fume temp - 30)", val); return buf;
        case 0x81: return "Clock: seconds since midnight / 2 (low byte, with 0x82)";
        case 0x82: return "Clock: seconds since midnight / 2 (high byte, with 0x81)";
        case 0x3E: snprintf(buf, sizeof(buf), "%d °C (fume temp)", val);      return buf;
        case 0x44: snprintf(buf, sizeof(buf), "%.1f °C (stove room temp)", val / 2.0f); return buf;
        case 0x48: if (!val) return "0 rpm (fume fan stopped)"; snprintf(buf, sizeof(buf), "%d rpm (fume fan)", (val + 25) * 10); return buf;
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
        case 0xD3: snprintf(buf, sizeof(buf), "%d (follows the actual flame power, min 1; not the fan)", val); return buf;
        case 0x0A: if (!val) return "Room fan power 0 (off)"; snprintf(buf, sizeof(buf), "Room fan power %d (speed x flame: 9/14/19 flame 1, 21/23/25 flame 2, 27 flame 3 speed 1)", val); return buf;
        case 0xA7: if (!val) return "Room fan drive (2) 0 (off)"; snprintf(buf, sizeof(buf), "Room fan drive (2) %d (~117 - 1.5 x power 0x0A)", val); return buf;
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
        case 0x04: snprintf(buf, sizeof(buf), "%.1f (auger value at ignition)", val / 10.0f); return buf;
        case 0x05: snprintf(buf, sizeof(buf), "%.1f (auger value at flame power 1)", val / 10.0f); return buf;
        case 0x06: snprintf(buf, sizeof(buf), "%.1f (auger value at flame power 2)", val / 10.0f); return buf;
        case 0x07: snprintf(buf, sizeof(buf), "%.1f (auger value at flame power 3)", val / 10.0f); return buf;
        case 0x08: snprintf(buf, sizeof(buf), "%.1f (auger value at flame power 4)", val / 10.0f); return buf;
        case 0x09: snprintf(buf, sizeof(buf), "%.1f (auger value level 5, unused)", val / 10.0f); return buf;
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
        case 0xBC: { const char* l = getPelletQualityLabel(val);     return l ? l : "Unknown pellet quality"; }
        case 0xC0: { const char* l = getCombustionQualityLabel(val); return l ? l : "Unknown combustion quality"; }
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

    // Stream the response in chunks: a full dump of both banks is ~45 KB, and
    // building it as one String can fail on heap fragmentation, which silently
    // drops parts of the output and produces truncated / invalid JSON.
    wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    wm.server->send(200, "application/json", "");
    String out;
    out.reserve(2048);
    auto flushOut = [&out](bool force) {
        if (force || out.length() > 1500) { wm.server->sendContent(out); out = ""; }
    };
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
            flushOut(false);
        }
        out += "}";
    }

    out += "}";
    flushOut(true);
    wm.server->sendContent("");  // terminate the chunked response
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

    const char* menuName = getRemoteMenuName(menuId);
    json["displayed_menu"] = menuName;
    json["diplayed_menu"]  = menuName;  // deprecated misspelled alias, kept for existing clients

    json["menuId"] = menuId;
    int paramId = stove.read(ram, selectedParamAddr);
    json["selected_parameter"] = (paramId >= 0) ? getSelectedParameterName(paramId) : "Unknown";
    if (paramId >= 0) json["parameterId"] = paramId;          // RAM 0x18, 0 = not editing
    int editVal = stove.read(ram, menuEditValueAddr);
    if (editVal >= 0) json["editValue"] = editVal;            // RAM 0x19: value the remote shows while editing

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

bool getStoveDateTime()
{
    if (stoveMutex) {
        xSemaphoreTakeRecursive(stoveMutex, portMAX_DELAY);
    }

    int cur_min = stove.read(ram, stoveTimeMinAddr);
    int cur_hour = stove.read(ram, stoveTimeHourAddr);
    int cur_sec = stove.read(ram, stoveTimeSecAddr);
    int cur_day = stove.read(ram, stoveTimeDayAddr);
    int cur_month = stove.read(ram, stoveTimeMonthAddr);
    int cur_year = stove.read(ram, stoveTimeYearAddr);

    if (stoveMutex) {
        xSemaphoreGiveRecursive(stoveMutex);
    }

    // Keep the previous (extrapolated) time when a read failed or returned nonsense
    if (cur_sec < 0 || cur_sec > 59 || cur_min < 0 || cur_min > 59 || cur_hour < 0 || cur_hour > 23 ||
        cur_day < 1 || cur_day > 31 || cur_month < 1 || cur_month > 12 || cur_year < 0 || cur_year > 99)
        return false;

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
        stoveTimeReadMs = millis();
        return true;
    }
    return false;
}

// The stove clock is read from the stove every 5 min once a valid time is known
// (in between stoveTimeNow() extrapolates it), and every 10 s until then.
static void stoveTimeTick()
{
    static unsigned long lastAttempt = 0;
    static bool known = false;
    unsigned long interval = known ? STOVE_TIME_READ_MS : STOVE_TIME_RETRY_MS;
    if (lastAttempt != 0 && millis() - lastAttempt < interval) return;
    lastAttempt = millis();
    if (lastAttempt == 0) lastAttempt = 1;
    known = getStoveDateTime();
}

void handleRouteIndex() {
    String ip    = WiFi.localIP().toString();
    String ssid  = WiFi.SSID();
    int32_t rssi = WiFi.RSSI();
    char page[1600];
    snprintf(page, sizeof(page),
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>PalletHeater</title>"
        "<style>body{font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px}"
        "a{display:block;margin:8px 0;padding:10px 14px;background:#f0f0f0;border-radius:6px;"
        "text-decoration:none;color:#222}a:hover{background:#ddd}"
        "table{border-collapse:collapse;width:100%%}"
        "td{padding:6px 8px;border-bottom:1px solid #eee}td:first-child{color:#666;width:120px}"
        "</style></head><body>"
        "<h2>PalletHeater</h2>"
        "<table>"
        "<tr><td>IP</td><td>%s</td></tr>"
        "<tr><td>SSID</td><td>%s</td></tr>"
        "<tr><td>WiFi RSSI</td><td>%d dBm</td></tr>"
        "<tr><td>Uptime</td><td>%s</td></tr>"
        "</table><br>"
        "<a href='/app' style='background:#e8590c;color:#fff;font-weight:600'>Open web app</a>"
        "<a href='/api'>API Documentation (Swagger)</a>"
        "<a href='/api/state'>Current stove state (JSON)</a>"
        "<a href='/api/health'>Health check</a>"
        "<a href='/restart'>Restart device</a>"
        "<p style='color:#888;font-size:13px'>To reconfigure WiFi, hold the reset button for 5 seconds.</p>"
        "</body></html>",
        ip.c_str(), ssid.c_str(), rssi, uptimeStr().c_str());
    wm.server->send(200, "text/html", page);
}

void handleRouteRestart() {
    wm.server->send(200, "text/plain", "Restarting...");
    delay(200);
    ESP.restart();
}

void handleRouteSwaggerUI() {
    wm.server->send(200, "text/html", swaggerUI);
}

void handleRouteWebApp() {
    size_t len = (webAppHTMLEnd - webAppHTML) - 1;  // strip embed_txtfiles NUL terminator
    wm.server->sendHeader("Cache-Control", "no-cache");
    wm.server->send_P(200, "text/html", webAppHTML, len);
}

void handleRouteSwaggerJSON() { 
    // Stream straight from flash; embed_txtfiles appends a NUL terminator, skip it.
    size_t len = (swaggerJSONEnd - swaggerJSON) - 1;
    wm.server->send_P(200, "application/json", swaggerJSON, len);
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
        // Optional datetime=YYYY-MM-DDTHH:MM[:SS] (local time, e.g. from the browser);
        // without it the stove is synced to NTP time.
        String dt = wm.server->arg("datetime");
        if (dt.length())
        {
            struct tm t = {};
            int Y, M, D, h, m, sec = 0;
            int n = sscanf(dt.c_str(), "%d-%d-%d%*c%d:%d:%d", &Y, &M, &D, &h, &m, &sec);
            if (n < 5 || Y < 2020 || Y > 2099 || M < 1 || M > 12 || D < 1 || D > 31 || h < 0 || h > 23 || m < 0 || m > 59)
            {
                wm.server->send(400, "application/json", "{\"result\":\"failed - datetime must be YYYY-MM-DDTHH:MM[:SS]\"}");
                return;
            }
            t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = m; t.tm_sec = sec; t.tm_isdst = -1;
            mktime(&t);   // normalises and fills in tm_wday
            setStoveDateTimeTm(t);
            json["result"] = "success Set stove time to " + dt;
        }
        else
        {
            setStoveDateTime();
            json["result"] = "success Synced NTP time to stove";
        }
        delay(200);
        getStoveDateTime();
    }
    
    if (wm.server->method() == HTTP_GET || wm.server->method() == HTTP_PATCH )
    {
        JsonObject time = json.createNestedObject("time");
        time_t st = stoveTimeNow(), nt = ntpTimeNow();
        strftime(time_output, 30, "%F %T", localtime(&st));
        time["stove"] = time_output;

        strftime(time_output, 30, "%F %T", localtime(&nt));
        time["ntp"] = time_output;

        time["delta"] = nt - st;
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
                    json["result"] = "failed - stop is out of range: Should be between 0 and 3";
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
        // Manual §20: energy saving only stops the stove when STOP is set;
        // START = 0 (OFF) means it does not restart automatically after stopping.
        obj_energysaving["enabled"] = energySavingStop != 0;
        if (energySavingStop != 0)  obj_energysaving["stopTemperature"]  = setPointTemp + energySavingStop;
        if (energySavingStart != 0) obj_energysaving["startTemperature"] = setPointTemp + energySavingStart;


    } else {
        wm.server->send(405);
        return;
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

void handleRouteSetpoint()
{
    StaticJsonDocument<128> json;
    if (wm.server->method() != HTTP_GET && wm.server->method() != HTTP_POST)
    {
        wm.server->send(405);
        return;
    }
    if (wm.server->method() == HTTP_POST)
    {
        String val = wm.server->arg("value");
        int sp = val.toInt();
        if (sp < 7 || sp > 30)
        {
            json["result"] = "failed - value must be 7-30";
            serializeJson(json, buffer);
            wm.server->send(400, "application/json", buffer);
            return;
        }
        bool ok = stove.write(eeprom, TempSetpointAddr, (byte)sp);
        if (!ok)
        {
            json["result"] = "failed - RS232 error";
            serializeJson(json, buffer);
            wm.server->send(500, "application/json", buffer);
            return;
        }
        setPointTemp = sp;
        json["result"] = "success";
    }
    getTempSetpoint();
    json["setpoint"] = (int)setPointTemp;
    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
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

// Returns false (and sets timer["error"]) when a register could not be read,
// so clients never see — or write back — half-read garbage.
static bool serializeTimerToJson(int idx, JsonObject timer)
{
    byte base = timerBases[idx];
    int enabled = stove.read(eeprom, base + TIMER_ONOFF_OFFSET);
    int onVal   = stove.read(eeprom, base + TIMER_ONTIME_OFFSET);
    int offVal  = stove.read(eeprom, base + TIMER_OFFTIME_OFFSET);
    int dow     = stove.read(eeprom, base + TIMER_DOW_OFFSET);
    int sp      = stove.read(eeprom, base + TIMER_SETPOINT_OFFSET);
    int fp      = stove.read(eeprom, base + TIMER_FLAME_OFFSET);
    int fan     = stove.read(eeprom, base + TIMER_FAN_OFFSET);
    if (enabled < 0 || onVal < 0 || offVal < 0 || dow < 0 || sp < 0 || fp < 0 || fan < 0)
    {
        timer["error"] = "Failed to read timer from stove - RS232 error";
        return false;
    }

    // Keep the BLE / metrics cache in sync with what we just read
    cachedTimerEnabled[idx]  = enabled;
    cachedTimerOnVal[idx]    = onVal;
    cachedTimerOffVal[idx]   = offVal;
    cachedTimerSetpoint[idx] = sp;
    cachedTimerFlame[idx]    = fp;
    cachedTimerFan[idx]      = fan;
    cachedTimerDowMask[idx]  = dow;

    timer["enabled"] = enabled > 0;
    // Always return the stored program, also when it is switched off: the stove keeps
    // it (the remote shows it in the CHRONO menu) and the web app needs it to edit.
    {
        timer["on"]  = timerValueToTimeStr(onVal);
        timer["off"] = timerValueToTimeStr(offVal);

        JsonObject enabledOn = timer.createNestedObject("enabled_on");
        enabledOn["monday"]    = (dow & 1)  != 0;
        enabledOn["tuesday"]   = (dow & 2)  != 0;
        enabledOn["wednesday"] = (dow & 4)  != 0;
        enabledOn["thursday"]  = (dow & 8)  != 0;
        enabledOn["friday"]    = (dow & 16) != 0;
        enabledOn["saturday"]  = (dow & 32) != 0;
        enabledOn["sunday"]    = (dow & 64) != 0;

        JsonObject settings = timer.createNestedObject("settings");
        settings["setpoint"]     = sp;
        settings["flame_power"]  = fp;
        settings["RoomFanSpeed"] = fan;
    }
    return true;
}

// Validate a timer object before anything is written, so a request is either
// applied completely or rejected with 400. Returns an empty string when valid.
static String validateTimerJson(const char* name, JsonObject timer)
{
    String prefix = String(name) + ": ";
    if (timer.isNull()) return prefix + "must be a JSON object";

    if (timer.containsKey("enabled") && !timer["enabled"].is<bool>())
        return prefix + "'enabled' must be true or false";

    const char* timeKeys[2] = {"on", "off"};
    for (const char* key : timeKeys)
    {
        if (!timer.containsKey(key)) continue;
        const char* str = timer[key].as<const char*>();
        if (!str || timeStrToTimerValue(str) < 0)
            return prefix + "'" + key + "' must be HH:MM on a 10 minute boundary or 'off'";
    }

    if (timer.containsKey("enabled_on"))
    {
        if (!timer["enabled_on"].is<JsonObject>()) return prefix + "'enabled_on' must be an object";
        for (JsonPair kv : timer["enabled_on"].as<JsonObject>())
        {
            String day = kv.key().c_str();
            if (day != "monday" && day != "tuesday" && day != "wednesday" && day != "thursday" &&
                day != "friday" && day != "saturday" && day != "sunday")
                return prefix + "unknown day '" + day + "' in 'enabled_on'";
            if (!kv.value().is<bool>()) return prefix + "'enabled_on." + day + "' must be true or false";
        }
    }

    if (timer.containsKey("settings"))
    {
        if (!timer["settings"].is<JsonObject>()) return prefix + "'settings' must be an object";
        JsonObject settings = timer["settings"].as<JsonObject>();
        struct { const char* key; int min; int max; } ranges[] = {
            { "setpoint",     7, 30 },
            { "flame_power",  1, 4  },
            { "RoomFanSpeed", 0, 3  },
        };
        for (auto& r : ranges)
        {
            if (!settings.containsKey(r.key)) continue;
            if (!settings[r.key].is<int>())
                return prefix + "'settings." + r.key + "' must be an integer";
            int v = settings[r.key].as<int>();
            if (v < r.min || v > r.max)
                return prefix + "'settings." + r.key + "' must be " + r.min + "-" + r.max;
        }
    }
    return "";
}

static void sendJsonError(int code, const String& msg)
{
    StaticJsonDocument<256> json;
    json["error"] = msg;
    String out;
    serializeJson(json, out);
    wm.server->send(code, "application/json", out);
}

// Partial write — only touches keys present in the JSON object.
// Call validateTimerJson() first; this function assumes valid input.
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
            if (fp >= 1 && fp <= 4) stove.write(eeprom, base + TIMER_FLAME_OFFSET, fp);
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
        if (timers.isNull()) { sendJsonError(400, "'timers' must be an object"); return; }

        // Reject unknown program names and validate everything before writing anything
        for (JsonPair kv : timers)
        {
            int idx = -1;
            for (int i = 0; i < 6; i++)
                if (strcmp(kv.key().c_str(), prNames[i]) == 0) idx = i;
            if (idx < 0) { sendJsonError(400, String("Unknown timer '") + kv.key().c_str() + "', expected PR1-PR6"); return; }
            String err = validateTimerJson(prNames[idx], kv.value().as<JsonObject>());
            if (err.length()) { sendJsonError(400, err); return; }
        }

        for (int i = 0; i < 6; i++)
            if (timers.containsKey(prNames[i]))
            {
                applyTimerJson(i, timers[prNames[i]].as<JsonObject>());
                refreshTimerCache(i);
            }

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
    if (!serializeTimerToJson(idx, json.createNestedObject(prNames[idx])))
        json["result"] = "success - but reading the timer back failed";
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
        bool ok = serializeTimerToJson(idx, json.createNestedObject(prNames[idx]));
        String out;
        serializeJson(json, out);
        wm.server->send(ok ? 200 : 500, "application/json", out);
        return;
    }

    // DELETE — disable the timer
    if (wm.server->method() == HTTP_DELETE)
    {
        stove.write(eeprom, timerBases[idx] + TIMER_ONOFF_OFFSET, 0);
        sendTimerResponse(idx);
        return;
    }

    // POST  — no body: enable the timer; with a JSON body: partial update (same as PATCH)
    // PATCH — partial update (only provided fields written)
    // Bodies are validated first; invalid input returns 400 and nothing is written.
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
        String err = validateTimerJson(prNames[idx], timer);
        if (err.length()) { sendJsonError(400, err); return; }
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
        return;
    }

    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
}

static bool isKnownErrorId(const String& arg, int& out)
{
    String a = arg; a.trim();
    if (a.length() == 0) return false;
    for (unsigned int i = 0; i < a.length(); i++) if (!isDigit(a[i])) return false;
    int v = a.toInt();
    for (int i = 0; i < errorTableSize; i++)
        if (errorTable[i].id == v) { out = v; return true; }
    return false;
}

void handleRouteErrors()
{
    StaticJsonDocument<450> json;
    json.clear();

    if (wm.server->method() == HTTP_POST)
    {
        // Validate every MemoryPos_N argument before writing anything
        for (uint8_t i = 0; i < wm.server->args(); i++)
        {
            if (!wm.server->argName(i).startsWith("MemoryPos_")) continue;
            int dummy;
            if (!isKnownErrorId(wm.server->arg(i), dummy))
            {
                StaticJsonDocument<192> err;
                err["result"] = "failed - " + wm.server->argName(i) + " must be a known error id (see /api docs), e.g. 0 to clear";
                serializeJson(err, buffer);
                wm.server->send(400, "application/json", buffer);
                return;
            }
        }
        for (uint8_t i = 0; i < wm.server->args(); i++)
        {
            if (wm.server->argName(i) == "MemoryPos_1")
            {
                int error = 0;
                isKnownErrorId(wm.server->arg(i), error);
                setError(0, error);
                json["action"] = "MemoryPos_1 set";
            }
            if (wm.server->argName(i) == "MemoryPos_2")
            {
                int error = 0;
                isKnownErrorId(wm.server->arg(i), error);
                setError(1, error);
                json["action"] = "MemoryPos_2 set";
            }
            if (wm.server->argName(i) == "MemoryPos_3")
            {
                int error = 0;
                isKnownErrorId(wm.server->arg(i), error);
                setError(2, error);
                json["action"] = "MemoryPos_3 set";
            }
            if (wm.server->argName(i) == "MemoryPos_4")
            {
                int error = 0;
                isKnownErrorId(wm.server->arg(i), error);
                setError(3, error);
                json["action"] = "MemoryPos_4 set";
            }
            if (wm.server->argName(i) == "MemoryPos_5")
            {
                int error = 0;
                isKnownErrorId(wm.server->arg(i), error);
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
            if (errors[i] >= 0) cachedErrors[i] = errors[i];
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
        return;
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
        // stove.read() returns -1 on a serial error; 0 is a valid value.
        if (stove.read(ram, ambTempAddr) >= 0)
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
        return;
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
        return;
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

    if (wm.server->method() != HTTP_GET && wm.server->method() != HTTP_POST)
    {
        wm.server->send(405);
        return;
    }

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

void getParams()
{
    // GET /set?type=ram|eeprom&int=<addr>  (legacy) — read a single register.
    // Also accepts address=<addr> in decimal or hex (0x..), like /api/dump.
    StaticJsonDocument<384> json;
    byte type = ram;
    String t = wm.server->arg("type");
    t.toLowerCase();
    if (t == "eeprom") type = eeprom;
    else if (t.length() && t != "ram") {
        wm.server->send(400, "application/json", "{\"error\":\"type must be ram or eeprom\"}");
        return;
    }

    String a = wm.server->hasArg("int") ? wm.server->arg("int") : wm.server->arg("address");
    a.trim();
    char* endp = nullptr;
    long addr = (a.startsWith("0x") || a.startsWith("0X")) ? strtol(a.c_str() + 2, &endp, 16) : strtol(a.c_str(), &endp, 10);
    if (a.length() == 0 || (endp && *endp != '\0') || addr < 0 || addr > 255) {
        wm.server->send(400, "application/json", "{\"error\":\"int (or address) must be 0-255, decimal or 0x hex\"}");
        return;
    }

    char key[8];
    snprintf(key, sizeof(key), "0x%02X", type);
    JsonObject typeObj = json.createNestedObject(key);
    snprintf(key, sizeof(key), "0x%02X", (unsigned)addr);
    JsonObject address = typeObj.createNestedObject(key);

    int value = stove.read(type, (byte)addr);
    address["Address_int"] = (int)addr;
    if (value < 0) {
        address["error"] = "Failed to get value RS232 error";
        String out; serializeJson(json, out);
        wm.server->send(500, "application/json", out);
        return;
    }
    address["Value_int"] = value;
    // Decoded text (same as /api/dump); previously this was the raw byte as a
    // character, which produced invalid JSON for values below 0x20.
    address["Value_txt"] = (type == eeprom) ? getEepromValueTxt((byte)addr, value) : getRamValueTxt((byte)addr, value);
    snprintf(key, sizeof(key), "0x%02X", value);
    address["Value_hex"] = key;

    String out; serializeJson(json, out);
    wm.server->send(200, "application/json", out);
}

void getStatus()
{
    if (wm.server->method() == HTTP_GET)
    {
    //telnet.println("Get status called");

    DynamicJsonDocument json(3072);   // grew with roomFan, pelletReserve, controller
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
    obj_flame["power"] = flamePower;                                   // setting
    if (cachedFlameActual >= 0) obj_flame["actual"] = cachedFlameActual; // what the stove runs at now
    json["bluetooth"] = bleEnabled;
    json["telnet"] = telnetEnabled;
    json["apiKeyRequired"] = apiKeyRequired;

    JsonObject obj_temperatures = json.createNestedObject("temperatures");
    obj_temperatures["room"] = ambTemp;
    obj_temperatures["stove"] = ambTempStove;
    obj_temperatures["remoteControl"] = ambTempRemoteControl;
    obj_temperatures["setPoint"] = setPointTemp;
    obj_temperatures["fume"] = fumesTemp;
    json["wirelessSensor"] = wirelessSensorStr();
    if (confirmedCurrentError == 0) {
        json["currentError"] = false;
    } else {
        JsonObject ce = json.createNestedObject("currentError");
        ce["id"]    = confirmedCurrentError;
        ce["error"] = getErrorCode(confirmedCurrentError);
        ce["desc"]  = getErrorDesc(confirmedCurrentError);
    }

    JsonObject obj_energysaving = json.createNestedObject("energySaving");
    obj_energysaving["start"] = energySavingStart;
    obj_energysaving["stop"] = energySavingStop;
    obj_energysaving["enabled"] = energySavingStop != 0;   // see manual §20
    if (energySavingStop != 0)  obj_energysaving["stopTemperature"]  = setPointTemp + energySavingStop;
    if (energySavingStart != 0) obj_energysaving["startTemperature"] = setPointTemp + energySavingStart;

    json["RoomFanSpeed"] = roomFanSpeed;
    {
        // What the room fan really does (the setting above is only the request)
        JsonObject rf = json.createNestedObject("roomFan");
        rf["speed"] = roomFanSpeed;
        int act = roomFanActual();
        if (act >= 0) rf["actual"] = act; else rf["actual"] = nullptr;
        int run = roomFanIsRunning();
        if (run >= 0) rf["running"] = run == 1; else rf["running"] = nullptr;
        const char* mode = roomFanMode();   // off / on (power from RAM 0x0A)
        if (mode) rf["mode"] = mode; else rf["mode"] = nullptr;
        if (cachedHotForFan >= 0) rf["hotEnough"] = cachedHotForFan != 0; else rf["hotEnough"] = nullptr;
        if (cachedRoomFanControl >= 0) rf["drive"] = cachedRoomFanControl; else rf["drive"] = nullptr;
    }
    {
        // Minutes to the next brazier cleaning: only counts while the stove is working
        // (state 4); null otherwise. 0 while the cleaning itself runs (state 5).
        JsonObject bc = json.createNestedObject("brazierCleaning");
        if (stoveState == 4 && cachedBrazierCountdown >= 0) bc["minutesLeft"] = cachedBrazierCountdown; else bc["minutesLeft"] = nullptr;
        bc["active"] = stoveState == 5;
        if (cachedBrazierInterval > 0) bc["interval"] = cachedBrazierInterval; else bc["interval"] = nullptr;
    }
    json["lowPalletAlarm"] = cachedLowPallet;
    {
        JsonObject pr = json.createNestedObject("pelletReserve");
        if (cachedPelletReserveOn >= 0) pr["enabled"] = cachedPelletReserveOn != 0; else pr["enabled"] = nullptr;
        if (cachedPelletSensor >= 0) pr["sensor"] = cachedPelletSensor; else pr["sensor"] = nullptr;     // RAM 0xA4 (5 = active)
        if (cachedPelletCounter >= 0) pr["counter"] = cachedPelletCounter; else pr["counter"] = nullptr; // RAM 0x89
    }
    json["stoveBeepEnabled"] = cachedBeepEnabled;
    json["numberOfStarts"] = cachedNumberOfStarts;
    json["hoursSinceLastService"] = cachedHoursSinceLastService;
    json["totalRuntime"] = cachedTotalRuntime;
    json["firmwareVersion"] = cachedFirmwareVersion;
    json["augerSpeed"] = serialized(String(cachedAugerSpeed, 1));
    JsonObject obj_burn = json.createNestedObject("burn");
    const char* cqLabel = getCombustionQualityLabel(cachedCombustionQuality);
    const char* pqLabel = getPelletQualityLabel(cachedPelletQuality);
    obj_burn["combustionQuality"] = cqLabel ? cqLabel : "Unknown";
    obj_burn["pelletQuality"]     = pqLabel ? pqLabel : "Unknown";
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
    time_t st = stoveTimeNow(), nt = ntpTimeNow();
    strftime(time_output, 30, "%F %T", localtime(&nt));
    time["ntp"] = time_output;
    strftime(time_output, 30, "%F %T", localtime(&st));
    time["stove"] = time_output;
    time["delta"] = nt - st;

    // How fresh is this cached data? Lets clients detect a stalled sensor task / bus.
    JsonObject obj_age = json.createNestedObject("dataAge");
    unsigned long nowMs = millis();
    if (lastFastStatesAt) obj_age["fastSeconds"] = (nowMs - lastFastStatesAt) / 1000; else obj_age["fastSeconds"] = nullptr;
    if (lastSlowStatesAt) obj_age["slowSeconds"] = (nowMs - lastSlowStatesAt) / 1000; else obj_age["slowSeconds"] = nullptr;

    JsonObject obj_serial = json.createNestedObject("serial");
    obj_serial["readErrors"]  = stove.readErrors;
    obj_serial["writeErrors"] = stove.writeErrors;
    obj_serial["retries"]     = stove.retries;

    JsonObject obj_uptime = json.createNestedObject("uptime");
    obj_uptime["seconds"] = millis() / 1000;
    obj_uptime["human"]   = uptimeStr();

    // Controller (ESP32) firmware build, from scripts/buildinfo.py
    JsonObject obj_ctrl = json.createNestedObject("controller");
    obj_ctrl["version"] = BUILD_VERSION;
    obj_ctrl["built"]   = BUILD_TIME;
    obj_ctrl["branch"]  = BUILD_BRANCH;
    obj_ctrl["env"]     = BUILD_ENV;

    if (json.overflowed()) log_e("getStatus: JSON document too small");
    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
    } else{
        wm.server->send(405);
    }
}

void setupTelnet()
{
    // Menu actions read/write the stove, so hold the bus lock while they run
    telnet.onConnect([](String ip) { StoveBusLock lock; telnetOnConnect(ip); });
    telnet.onInputReceived([](String str) { StoveBusLock lock; telnetHandleInput(str); });

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

// ─────────────────────────────────────────────
// History: 24 h ring buffer of the cached values, one sample every 2 min.
// Kept in RAM only (lost on reboot). Served on GET /api/history.
// ─────────────────────────────────────────────
struct HistSample {
    uint32_t t;          // unix time (UTC)
    int16_t  room10;     // °C x10
    int16_t  stove10;
    int16_t  remote10;
    uint8_t  setpoint;
    uint8_t  fume;
    int8_t   flame;
    int8_t   state;
    int8_t   fan;
    int8_t   flameActual;   // -1 = unknown (samples from older firmware)
    int16_t  auger10;       // auger speed rpm x10, -1 = unknown (older firmware)
    uint16_t fumeRpm;       // fume fan rpm, 0xFFFF = unknown (older firmware)
    int8_t   fanActual;     // speed the room fan really runs at (0..3), -1 = unknown
    uint8_t  fanPower;      // room fan power, RAM 0x0A (0 off, 21/23/25), 0xFF = unknown
};
// On-disk layout of format v5 (no fanPower), converted when loading
struct HistSampleV5 {
    uint32_t t;
    int16_t  room10, stove10, remote10;
    uint8_t  setpoint, fume;
    int8_t   flame, state, fan, flameActual;
    int16_t  auger10;
    uint16_t fumeRpm;
    int8_t   fanActual;
};
// On-disk layout of format v4 (no fanActual), converted when loading
struct HistSampleV4 {
    uint32_t t;
    int16_t  room10, stove10, remote10;
    uint8_t  setpoint, fume;
    int8_t   flame, state, fan, flameActual;
    int16_t  auger10;
    uint16_t fumeRpm;
};
// On-disk layout of format v3 (no fume fan rpm), converted when loading
struct HistSampleV3 {
    uint32_t t;
    int16_t  room10, stove10, remote10;
    uint8_t  setpoint, fume;
    int8_t   flame, state, fan, flameActual;
    int16_t  auger10;
};
// On-disk layout of format v1/v2 (16 bytes, no auger), converted when loading
struct HistSampleV2 {
    uint32_t t;
    int16_t  room10, stove10, remote10;
    uint8_t  setpoint, fume;
    int8_t   flame, state, fan, flameActual;
};
static const uint32_t HIST_INTERVAL_S = 120;
static const int      HIST_SIZE       = 720;   // 24 h
static HistSample hist[HIST_SIZE];
static int histHead = 0, histCount = 0;

// ── Persistence on SPIFFS ─────────────────────────────────────────────
// Every sample is appended (≈16 bytes per 2 min) to one of two files. When the
// active file holds 24 h of samples, the other file is truncated and becomes
// active, so the two files together always cover at least the last 24 h.
// Appends spread wear over the 128 KB partition (well under 1 erase per sector
// per day). Each file starts with a small header so a changed sample layout
// in a future firmware is detected and ignored instead of read as garbage.
static const char* HIST_FILES[2] = { "/hist_a.bin", "/hist_b.bin" };
static const uint8_t HIST_MAGIC[4] = { 'P', 'H', 'H', '6' };   // v2: + flameActual, v3: + auger10, v4: + fumeRpm, v5: + fanActual, v6: + fanPower
// v1 samples (PHH1) were 16 bytes without flameActual; the padding byte is now
// flameActual, so v1 records are read with flameActual forced to -1 (unknown).
static const size_t  HIST_HDR = sizeof(HIST_MAGIC) + 1;   // magic + sizeof(HistSample)
static int  histActiveFile = 0;
static int  histActiveCount = 0;                           // samples in the active file

static int histFileVersion[2] = { 0, 0 };

// Returns the file format version (1..6), or 0 when the header is not valid
static int histHeaderVersion(File& f)
{
    uint8_t hdr[HIST_HDR];
    if (f.read(hdr, HIST_HDR) != HIST_HDR) return 0;
    if (memcmp(hdr, HIST_MAGIC, 3) != 0) return 0;
    if (hdr[3] == '6' && hdr[4] == sizeof(HistSample))   return 6;
    if (hdr[3] == '5' && hdr[4] == sizeof(HistSampleV5)) return 5;
    if (hdr[3] == '4' && hdr[4] == sizeof(HistSampleV4)) return 4;
    if (hdr[3] == '3' && hdr[4] == sizeof(HistSampleV3)) return 3;
    if (hdr[3] == '2' && hdr[4] == sizeof(HistSampleV2)) return 2;
    if (hdr[3] == '1' && hdr[4] == sizeof(HistSampleV2)) return 1;
    return 0;
}
static size_t histRecordSize(int ver) { return ver >= 6 ? sizeof(HistSample) : ver == 5 ? sizeof(HistSampleV5) : ver == 4 ? sizeof(HistSampleV4) : ver == 3 ? sizeof(HistSampleV3) : sizeof(HistSampleV2); }

// Read one record of the given file version into the current layout
static bool histReadRecord(File& f, int ver, HistSample& out)
{
    if (ver >= 6) return f.read((uint8_t*)&out, sizeof(out)) == sizeof(out);
    if (ver == 5) {
        HistSampleV5 o;
        if (f.read((uint8_t*)&o, sizeof(o)) != sizeof(o)) return false;
        out = {};
        memcpy(&out, &o, sizeof(o));   // v5 is a prefix of the current layout
        out.fanPower = 0xFF;
        return true;
    }
    if (ver == 4) {
        HistSampleV4 o;
        if (f.read((uint8_t*)&o, sizeof(o)) != sizeof(o)) return false;
        out = {};
        memcpy(&out, &o, sizeof(o));   // v4 is a prefix of the current layout
        out.fanActual = -1;
        out.fanPower = 0xFF;
        return true;
    }
    if (ver == 3) {
        HistSampleV3 o;
        if (f.read((uint8_t*)&o, sizeof(o)) != sizeof(o)) return false;
        out = {};
        out.t = o.t; out.room10 = o.room10; out.stove10 = o.stove10; out.remote10 = o.remote10;
        out.setpoint = o.setpoint; out.fume = o.fume; out.flame = o.flame; out.state = o.state; out.fan = o.fan;
        out.flameActual = o.flameActual; out.auger10 = o.auger10; out.fumeRpm = 0xFFFF; out.fanActual = -1; out.fanPower = 0xFF;
        return true;
    }
    HistSampleV2 o;
    if (f.read((uint8_t*)&o, sizeof(o)) != sizeof(o)) return false;
    out = {};
    out.t = o.t; out.room10 = o.room10; out.stove10 = o.stove10; out.remote10 = o.remote10;
    out.setpoint = o.setpoint; out.fume = o.fume; out.flame = o.flame; out.state = o.state; out.fan = o.fan;
    out.flameActual = ver == 1 ? -1 : o.flameActual;
    out.auger10 = -1;
    out.fumeRpm = 0xFFFF;
    out.fanActual = -1;
    out.fanPower = 0xFF;
    return true;
}

static void histPush(const HistSample& s)
{
    hist[histHead] = s;
    histHead = (histHead + 1) % HIST_SIZE;
    if (histCount < HIST_SIZE) histCount++;
}

// Load both files at boot, oldest first, keeping only the last 24 h
static void historyLoad()
{
    if (!spiffsMounted) return;
    struct { int idx; uint32_t last; int count; } info[2] = { {0, 0, 0}, {1, 0, 0} };
    for (int i = 0; i < 2; i++) {
        File f = SPIFFS.open(HIST_FILES[i], FILE_READ);
        if (!f) continue;
        histFileVersion[i] = histHeaderVersion(f);
        if (!histFileVersion[i]) { f.close(); SPIFFS.remove(HIST_FILES[i]); continue; }
        size_t rs = histRecordSize(histFileVersion[i]);
        size_t n = (f.size() - HIST_HDR) / rs;
        info[i].count = (int)n;
        if (n) { f.seek(HIST_HDR + (n - 1) * rs); HistSample s; if (histReadRecord(f, histFileVersion[i], s)) info[i].last = s.t; }
        f.close();
    }
    int newer = info[1].last > info[0].last ? 1 : 0, older = 1 - newer;
    uint32_t cutoff = (uint32_t)time(nullptr) > 1700000000 ? (uint32_t)time(nullptr) - 24UL * 3600UL : 0;
    for (int k : { older, newer }) {
        File f = SPIFFS.open(HIST_FILES[k], FILE_READ);
        if (!f) continue;
        int ver = histHeaderVersion(f);
        if (!ver) { f.close(); continue; }
        HistSample s;
        while (histReadRecord(f, ver, s)) {
            if (s.t < 1700000000) continue;                 // never stored without NTP, but be safe
            if (cutoff && s.t < cutoff) continue;
            if (histCount && s.t <= hist[(histHead - 1 + HIST_SIZE) % HIST_SIZE].t) continue;  // keep it ordered
            histPush(s);
        }
        f.close();
    }
    histActiveFile  = newer;
    histActiveCount = info[newer].count;
    Serial.printf("History: loaded %d samples from SPIFFS\n", histCount);
}

static void historyAppend(const HistSample& s)
{
    if (!spiffsMounted) return;
    // After a format change: rewrite everything in RAM (already converted, includes s)
    // in the new format to the other file, so no history is lost on an upgrade.
    if (histActiveCount > 0 && histFileVersion[histActiveFile] != 6) {
        int target = 1 - histActiveFile;
        SPIFFS.remove(HIST_FILES[target]);
        File f = SPIFFS.open(HIST_FILES[target], FILE_WRITE);
        if (!f) return;
        f.write(HIST_MAGIC, sizeof(HIST_MAGIC)); uint8_t sz = sizeof(HistSample); f.write(&sz, 1);
        int start = (histHead - histCount + HIST_SIZE) % HIST_SIZE;
        for (int i = 0; i < histCount; i++) f.write((const uint8_t*)&hist[(start + i) % HIST_SIZE], sizeof(HistSample));
        f.close();
        SPIFFS.remove(HIST_FILES[histActiveFile]);   // its samples are in the new file now
        histFileVersion[histActiveFile] = 0;
        histFileVersion[target] = 6;
        histActiveFile = target;
        histActiveCount = histCount;
        return;
    }
    if (histActiveCount >= HIST_SIZE) {
        histActiveFile = 1 - histActiveFile;
        histActiveCount = 0;
        SPIFFS.remove(HIST_FILES[histActiveFile]);
    }
    bool fresh = !SPIFFS.exists(HIST_FILES[histActiveFile]);
    File f = SPIFFS.open(HIST_FILES[histActiveFile], fresh ? FILE_WRITE : FILE_APPEND);
    if (!f) return;
    if (fresh) { f.write(HIST_MAGIC, sizeof(HIST_MAGIC)); uint8_t sz = sizeof(HistSample); f.write(&sz, 1); histFileVersion[histActiveFile] = 6; }
    if (f.write((const uint8_t*)&s, sizeof(s)) == sizeof(s)) histActiveCount++;
    f.close();
}

static void historyTick()
{
    static unsigned long lastMs = 0;
    time_t t = time(nullptr);
    if (t < 1700000000 || lastFastStatesAt == 0 || lastSlowStatesAt == 0) return;  // no NTP / no data yet
    if (histCount && millis() - lastMs < HIST_INTERVAL_S * 1000UL) return;
    lastMs = millis();
    // Loaded history may already contain a sample from just before a restart
    if (histCount && (uint32_t)t - hist[(histHead - 1 + HIST_SIZE) % HIST_SIZE].t < HIST_INTERVAL_S / 2) return;
    HistSample s = {};
    s.t        = (uint32_t)t;
    s.room10   = (int16_t)(ambTemp * 10);
    s.stove10  = (int16_t)(ambTempStove * 10);
    s.remote10 = (int16_t)(ambTempRemoteControl * 10);
    s.setpoint = (uint8_t)setPointTemp;
    s.fume     = fumesTemp;
    s.flame    = (int8_t)flamePower;
    s.state    = (int8_t)stoveState;
    s.fan      = (int8_t)roomFanSpeed;
    s.flameActual = (int8_t)cachedFlameActual;
    s.auger10 = (int16_t)(cachedAugerSpeed * 10 + 0.5f);
    s.fumeRpm = (uint16_t)constrain(cachedFumeFanRPM, 0, 0xFFFE);
    s.fanActual = (int8_t)roomFanActual();
    s.fanPower = cachedRoomFanControl < 0 ? 0xFF : (uint8_t)constrain(cachedRoomFanControl, 0, 254);
    histPush(s);
    historyAppend(s);
}

void handleRouteHistory()
{
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    int hours = wm.server->hasArg("hours") ? wm.server->arg("hours").toInt() : 24;
    if (hours < 1 || hours > 24) hours = 24;
    uint32_t since = (uint32_t)time(nullptr) - (uint32_t)hours * 3600UL;

    wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    wm.server->send(200, "application/json", "");
    String out;
    out.reserve(1800);
    out += "{\"interval\":"; out += HIST_INTERVAL_S;
    out += ",\"fields\":[\"t\",\"room\",\"stove\",\"remote\",\"setpoint\",\"fume\",\"flame\",\"state\",\"fan\",\"flameActual\",\"auger\",\"fumeFan\",\"fanActual\",\"fanPower\"],\"samples\":[";
    bool first = true;
    int start = (histHead - histCount + HIST_SIZE) % HIST_SIZE;
    for (int i = 0; i < histCount; i++) {
        const HistSample& s = hist[(start + i) % HIST_SIZE];
        if (s.t < since) continue;
        char row[144];
        char act[6], aug[10], rpm[8], fa[6], fp[6];
        if (s.fanPower == 0xFF) strcpy(fp, "null"); else snprintf(fp, sizeof(fp), "%u", s.fanPower);
        if (s.fanActual < 0) strcpy(fa, "null"); else snprintf(fa, sizeof(fa), "%d", s.fanActual);
        if (s.flameActual < 0) strcpy(act, "null"); else snprintf(act, sizeof(act), "%d", s.flameActual);
        if (s.fumeRpm == 0xFFFF) strcpy(rpm, "null"); else snprintf(rpm, sizeof(rpm), "%u", s.fumeRpm);
        if (s.auger10 < 0) strcpy(aug, "null"); else snprintf(aug, sizeof(aug), "%.1f", s.auger10 / 10.0f);
        snprintf(row, sizeof(row), "%s[%lu,%.1f,%.1f,%.1f,%u,%u,%d,%d,%d,%s,%s,%s,%s,%s]", first ? "" : ",",
                 (unsigned long)s.t, s.room10 / 10.0f, s.stove10 / 10.0f, s.remote10 / 10.0f,
                 s.setpoint, s.fume, s.flame, s.state, s.fan, act, aug, rpm, fa, fp);
        out += row;
        first = false;
        if (out.length() > 1500) { wm.server->sendContent(out); out = ""; }
    }
    out += "]}";
    wm.server->sendContent(out);
    wm.server->sendContent("");
}

// ---------- stove state change and error log ----------
// Besides state changes, every change of the active error (RAM 0x33) is logged:
// kind EVT_ERROR with the raw error value (0 = error cleared). The stove itself does
// not store when an error happened, so this is the only source of error times.
// Every change of the stove state (RAM 0x21, refreshed ~1 s by the sensor task) is
// recorded with its exact time. The last EVT_SIZE changes are kept in RAM and
// persisted in /events.bin (one small append per change, so very few flash writes;
// the file is compacted when it reaches 2 * EVT_SIZE records).
enum : uint8_t { EVT_STATE = 0, EVT_ERROR = 1, EVT_PELLET = 2 };   // EVT_PELLET: code = RAM address, from/to = values
struct __attribute__((packed)) StateEvent { uint32_t t; uint8_t kind; int8_t from; int8_t to; uint8_t code; };
struct __attribute__((packed)) StateEventV1 { uint32_t t; int8_t from; int8_t to; };   // PHE1 file layout
static const int      EVT_SIZE = 200;
static StateEvent     evts[EVT_SIZE];
static int            evtHead = 0, evtCount = 0, evtFileCount = 0;
static const char*    EVT_FILE = "/events.bin";
static const uint8_t  EVT_MAGIC[4] = { 'P', 'H', 'E', '2' };   // v2: + kind, code
static const size_t   EVT_HDR = sizeof(EVT_MAGIC) + 1;
static int            evtLastState = -1;   // last known state (from the log or the bus)
static int            evtLastError = 0;    // last logged error value (0 = none)
static int            evtLastPellet[2] = { -1, -1 };   // last logged RAM 0xA4 / 0x89

static void evtPush(const StateEvent& e)
{
    evts[evtHead] = e;
    evtHead = (evtHead + 1) % EVT_SIZE;
    if (evtCount < EVT_SIZE) evtCount++;
}

static void evtWriteAll()
{
    File f = SPIFFS.open(EVT_FILE, FILE_WRITE);
    if (!f) return;
    uint8_t sz = sizeof(StateEvent);
    f.write(EVT_MAGIC, sizeof(EVT_MAGIC)); f.write(&sz, 1);
    int start = (evtHead - evtCount + EVT_SIZE) % EVT_SIZE;
    for (int i = 0; i < evtCount; i++) f.write((const uint8_t*)&evts[(start + i) % EVT_SIZE], sizeof(StateEvent));
    f.close();
    evtFileCount = evtCount;
}

static void eventsLoad()
{
    if (!spiffsMounted) return;
    File f = SPIFFS.open(EVT_FILE, FILE_READ);
    if (!f) return;
    uint8_t hdr[EVT_HDR];
    int ver = 0;
    if (f.read(hdr, EVT_HDR) == EVT_HDR && memcmp(hdr, EVT_MAGIC, 3) == 0) {
        if (hdr[3] == '2' && hdr[4] == sizeof(StateEvent))   ver = 2;
        if (hdr[3] == '1' && hdr[4] == sizeof(StateEventV1)) ver = 1;
    }
    if (!ver) { f.close(); SPIFFS.remove(EVT_FILE); return; }
    evtFileCount = 0;
    if (ver == 2) {
        StateEvent e;
        while (f.read((uint8_t*)&e, sizeof(e)) == sizeof(e)) { evtPush(e); evtFileCount++; }
    } else {
        StateEventV1 o;
        while (f.read((uint8_t*)&o, sizeof(o)) == sizeof(o)) { StateEvent e = { o.t, EVT_STATE, o.from, o.to, 0 }; evtPush(e); }
    }
    f.close();
    // Drop short pre-warnings logged by older firmware (error cleared again within
    // ERROR_CONFIRM_MS, see updateConfirmedError), together with their "cleared" entry
    bool dropped = false;
    {
        static StateEvent keep[EVT_SIZE];
        int n = 0, start = (evtHead - evtCount + EVT_SIZE) % EVT_SIZE;
        bool skipClear = false;
        for (int i = 0; i < evtCount; i++) {
            const StateEvent& e = evts[(start + i) % EVT_SIZE];
            if (e.kind == EVT_ERROR && e.code != 0) {
                uint32_t clearedAt = 0;   // next error entry, if it clears this one
                for (int j = i + 1; j < evtCount; j++) {
                    const StateEvent& n2 = evts[(start + j) % EVT_SIZE];
                    if (n2.kind == EVT_ERROR) { if (n2.code == 0) clearedAt = n2.t; break; }
                }
                if (clearedAt && clearedAt - e.t < ERROR_CONFIRM_MS / 1000) { skipClear = true; dropped = true; continue; }
            }
            if (e.kind == EVT_ERROR && e.code == 0 && skipClear) { skipClear = false; continue; }
            keep[n++] = e;
        }
        if (dropped) { evtHead = 0; evtCount = 0; for (int i = 0; i < n; i++) evtPush(keep[i]); }
    }
    if (ver == 1 || dropped) evtWriteAll();   // convert / rewrite the file
    bool gotState = false, gotError = false;
    for (int i = 0; i < evtCount && !(gotState && gotError); i++) {   // newest state and error, walking back
        const StateEvent& e = evts[(evtHead - 1 - i + 2 * EVT_SIZE) % EVT_SIZE];
        if (e.kind == EVT_STATE && !gotState) { evtLastState = e.to; gotState = true; }
        if (e.kind == EVT_ERROR && !gotError) { evtLastError = e.code; gotError = true; }
        if (e.kind == EVT_PELLET) {
            int k = e.code == palletSensorAddr ? 0 : e.code == palletLevelAddr ? 1 : -1;
            if (k >= 0 && evtLastPellet[k] == -1) evtLastPellet[k] = e.to;
        }
    }
}

static void evtStore(const StateEvent& e);
static void eventsTick()
{
    if (lastFastStatesAt == 0) return;                 // no data from the stove yet
    updateConfirmedError(stoveState);
    time_t t = time(nullptr);
    if (t < 1700000000) return;                        // no valid time yet: wait, changes are logged once NTP is in
    int s = stoveState;
    if (s >= 0 && s <= 20 && s != evtLastState) {
        StateEvent e = { (uint32_t)t, EVT_STATE, (int8_t)evtLastState, (int8_t)s, 0 };
        evtLastState = s;
        evtStore(e);
    }
    const int pv[2] = { cachedPelletSensor, cachedPelletCounter };
    const uint8_t pa[2] = { palletSensorAddr, palletLevelAddr };
    for (int k = 0; k < 2; k++) {
        if (pv[k] < 0 || pv[k] > 127 || pv[k] == evtLastPellet[k]) continue;
        StateEvent e = { (uint32_t)t, EVT_PELLET, (int8_t)evtLastPellet[k], (int8_t)pv[k], pa[k] };
        evtLastPellet[k] = pv[k];
        evtStore(e);
    }
    int err = confirmedCurrentError;   // debounced: short pre-warnings are not logged
    if (err >= 0 && err <= 255 && err != evtLastError) {
        StateEvent e = { (uint32_t)t, EVT_ERROR, -1, -1, (uint8_t)err };
        evtLastError = err;
        evtStore(e);
    }
}

static void evtStore(const StateEvent& e)
{
    evtPush(e);
    if (!spiffsMounted) return;
    if (evtFileCount >= 2 * EVT_SIZE || !SPIFFS.exists(EVT_FILE)) { evtWriteAll(); return; }
    File f = SPIFFS.open(EVT_FILE, FILE_APPEND);
    if (!f) return;
    f.write((const uint8_t*)&e, sizeof(e));
    f.close();
    evtFileCount++;
}

// GET /api/events[?hours=1..720] -> { events: [ {t, type:"state", from, to} | {t, type:"error", id, error}, ... ] } (oldest first)
void handleRouteEvents()
{
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    uint32_t since = 0;
    if (wm.server->hasArg("hours")) {
        int hours = wm.server->arg("hours").toInt();
        if (hours < 1 || hours > 720) { wm.server->send(400, "application/json", "{\"result\":\"failed - hours must be 1..720\"}"); return; }
        since = (uint32_t)time(nullptr) - (uint32_t)hours * 3600UL;
    }
    wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    wm.server->send(200, "application/json", "");
    String out = "{\"events\":[";
    bool first = true;
    int start = (evtHead - evtCount + EVT_SIZE) % EVT_SIZE;
    for (int i = 0; i < evtCount; i++) {
        const StateEvent& e = evts[(start + i) % EVT_SIZE];
        if (e.t < since) continue;
        char row[96], from[6];
        if (e.kind == EVT_PELLET) {
            if (e.from < 0) strcpy(from, "null"); else snprintf(from, sizeof(from), "%d", e.from);
            snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"pellet\",\"register\":\"0x%02X\",\"from\":%s,\"to\":%d}",
                     first ? "" : ",", (unsigned long)e.t, e.code, from, e.to);
        } else if (e.kind == EVT_ERROR) {
            if (e.code == 0) snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"error\",\"id\":0,\"error\":null}", first ? "" : ",", (unsigned long)e.t);
            else snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"error\",\"id\":%u,\"error\":\"%s\"}", first ? "" : ",",
                          (unsigned long)e.t, e.code, getErrorCode(e.code).c_str());
        } else {
            if (e.from < 0) strcpy(from, "null"); else snprintf(from, sizeof(from), "%d", e.from);
            snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"state\",\"from\":%s,\"to\":%d}", first ? "" : ",", (unsigned long)e.t, from, e.to);
        }
        out += row; first = false;
        if (out.length() > 1500) { wm.server->sendContent(out); out = ""; }
    }
    out += "]}";
    wm.server->sendContent(out);
    wm.server->sendContent("");
}

// GET  /api/bluetooth                     -> { enabled, active, restartRequired }
// POST /api/bluetooth?enabled=true|false[&restart=true]
// The setting is stored in NVS and applied at the next boot (BLE is started or
// skipped in setup()), so a change requires a restart of the controller.
void handleRouteBluetooth()
{
    Preferences prefs;
    if (wm.server->method() == HTTP_POST)
    {
        String v = wm.server->arg("enabled");
        if (v != "true" && v != "false" && v != "1" && v != "0") {
            wm.server->send(400, "application/json", "{\"result\":\"failed - expected enabled=true/false\"}");
            return;
        }
        prefs.begin("ble", false);
        prefs.putBool("enabled", v == "true" || v == "1");
        prefs.end();
    }
    else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }

    prefs.begin("ble", true);
    bool saved = prefs.getBool("enabled", true);
    prefs.end();
    bool restart = wm.server->method() == HTTP_POST && wm.server->arg("restart") == "true" && saved != bleEnabled;

    StaticJsonDocument<192> json;
    if (wm.server->method() == HTTP_POST) json["result"] = restart ? "success - restarting" : "success";
    json["enabled"] = saved;
    json["active"] = bleEnabled;
    json["restartRequired"] = saved != bleEnabled;
    String out; serializeJson(json, out);
    wm.server->send(200, "application/json", out);
    if (restart) { delay(300); ESP.restart(); }
}

// GET  /api/telnet                   -> { enabled }
// POST /api/telnet?enabled=true|false  (applied immediately, stored in NVS)
void setupTelnet();
void handleRouteTelnet()
{
    if (wm.server->method() == HTTP_POST)
    {
        String v = wm.server->arg("enabled");
        if (v != "true" && v != "false" && v != "1" && v != "0") {
            wm.server->send(400, "application/json", "{\"result\":\"failed - expected enabled=true/false\"}");
            return;
        }
        bool on = (v == "true" || v == "1");
        Preferences prefs;
        prefs.begin("telnet", false);
        prefs.putBool("enabled", on);
        prefs.end();
        if (on && !telnetEnabled) { telnetEnabled = true; setupTelnet(); }
        else if (!on && telnetEnabled) { telnet.stop(); telnetEnabled = false; Serial.println("- Telnet: stopped"); }
    }
    else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    StaticJsonDocument<96> json;
    if (wm.server->method() == HTTP_POST) json["result"] = "success";
    json["enabled"] = telnetEnabled;
    String out; serializeJson(json, out);
    wm.server->send(200, "application/json", out);
}

// Serialise all stove bus access: HTTP handlers run on the loop task while the
// sensor task polls the stove from the other core. Without the lock both talk
// on the bus at the same time and reads return garbage.
static std::function<void()> withStoveLock(void (*fn)())
{
    return [fn]() {
        if (!stoveMutex) { fn(); return; }
        if (xSemaphoreTakeRecursive(stoveMutex, pdMS_TO_TICKS(5000)) != pdTRUE)
        {
            wm.server->send(503, "application/json", "{\"error\":\"Stove bus busy, try again\"}");
            return;
        }
        fn();
        xSemaphoreGiveRecursive(stoveMutex);
    };
}

void setup()
{
    esp_log_level_set("*", ESP_LOG_WARN);
    pinMode(ENABLE_RX, OUTPUT);
    digitalWrite(ENABLE_RX, HIGH); // The led of the optocoupler is off

    Serial.begin(115200);
    
    spiffsMounted = SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED); // Mount SPIFFS
    if (spiffsMounted)
    {
        Serial.println("SPIFFS system mounted with success");
    }
    else
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
    }
    setup_wifi();
    MDNS.begin("palletheater");
    MDNS.addService("http", "tcp", 80);
    authInit();
    {
        Preferences prefs;
        prefs.begin("ble", true);
        bleEnabled = prefs.getBool("enabled", true);
        prefs.end();
        prefs.begin("telnet", true);
        telnetEnabled = prefs.getBool("enabled", true);
        prefs.end();
    }
    if (bleEnabled) {
        setupBLE();
    } else {
        // BLE skipped completely: release the BT controller memory and let WiFi
        // use the radio without the BLE/WiFi coexistence modem sleep.
        esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
        WiFi.setSleep(false);
        Serial.println("Bluetooth disabled (setting) - BLE not started");
    }
    httpRouteMetrics();
    enableOTA();
    if (spiffsMounted) {
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
    } else {
        Serial.println("SPIFFS unavailable, using defaults");
    }

    resetButton.begin();
    resetButton.onPressedFor(5000, fullReset);
    resetButton.onPressed(buttonPressed);

    Serial.println("Ready");

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    onboardLED.on();

    wm.server->on("/on", authed(withStoveLock(setStoveOn)));
    wm.server->on("/off", authed(withStoveLock(setStoveOff)));
    
    wm.server->on("/fan", authed(withStoveLock(handleRouteFanspeed)));
    wm.server->on("/error", authed(withStoveLock(handleRouteSetError)));

    wm.server->on("/cq", authed(withStoveLock(handleRouteCQ)));
    wm.server->on("/display", authed(withStoveLock(handleRouteDisplay)));
    wm.server->on("/flame", authed(withStoveLock(handleRouteFlamePower)));

    wm.server->on("/state", authed(getStatus));
    wm.server->on("/get", authed(withStoveLock(handleRouteDump)));
    wm.server->on("/set", authed(withStoveLock(getParams)));

    wm.server->on("/api/flame", authed(withStoveLock(handleRouteFlamePower)));
    wm.server->on("/api/fan", authed(withStoveLock(handleRouteFanspeed)));
    wm.server->on("/api/power", authed(withStoveLock(handleRoutePower)));
    wm.server->on("/api/energysaving", authed(withStoveLock(handleRouteEnergysaving)));
    wm.server->on("/api/errors", authed(withStoveLock(handleRouteErrors)));
    wm.server->on("/api/state", authed(getStatus));
    wm.server->on("/api/health", authed(withStoveLock(handleRouteHealth)));
    wm.server->on("/api/time", authed(withStoveLock(handleRouteTime)));
    wm.server->on("/api/remote", authed(withStoveLock(handleRouteRemote)));
    wm.server->on("/api/burn", authed(withStoveLock(handleRouteBurn)));
    wm.server->on("/api/beep", authed(withStoveLock(handleRouteBeep)));
    wm.server->on("/api/setpoint", authed(withStoveLock(handleRouteSetpoint)));
    wm.server->on("/api/timers",     authed(withStoveLock(handleRouteTimers)));
    wm.server->on("/api/timers/pr1", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/timers/pr2", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/timers/pr3", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/timers/pr4", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/timers/pr5", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/timers/pr6", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/dump", authed(withStoveLock(handleRouteDump)));
    wm.server->on("/api/keywatch", authed(handleRouteKeyWatch));   // takes the stove lock itself, in the sensor task
    wm.server->on("/api", handleRouteSwaggerUI);
    wm.server->on("/api-specs.json", handleRouteSwaggerJSON);
    wm.server->on("/app", HTTP_GET, handleRouteWebApp);
    wm.server->on("/api/history", authed(handleRouteHistory));
    wm.server->on("/api/events", authed(handleRouteEvents));
    wm.server->on("/api/auth", handleRouteAuth);   // public: does its own check
    wm.server->on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);   // does its own check
    wm.server->on("/api/telnet", authed(handleRouteTelnet));
    wm.server->on("/api/bluetooth", authed(handleRouteBluetooth));
    wm.server->on("/", handleRouteIndex);
    wm.server->on("/restart", authed(handleRouteRestart));
    if (wifiConnected && wm.server) {
        authCollectHeaders();
        wm.server->begin();
        Serial.println("REST server started");
    }

    configTzTime(defaultTimezone, ntpServer); // sets TZ and starts NTP sync
    getNTPtime(2);
    eventsLoad();    // restore the stove state change log
    historyLoad();   // restore the persisted history (uses NTP time to drop samples older than 24 h)

    if (isConnected() && telnetEnabled)
    {
        setupTelnet();
    }
    stoveMutex = xSemaphoreCreateRecursiveMutex();
    xTaskCreatePinnedToCore(sensorTask, "SensorTask", 8192, NULL, 1, NULL, 0);
    getStates();
}

void loop()
{
    if (telnetEnabled) {
        telnet.loop();
        telnetMenuLoop();
    }
    if (bleEnabled) bleLoop();
    esp_task_wdt_reset();
    // wm.process() only calls handleClient() while the config portal is active.
    // Once connected to WiFi the portal is deactivated, so we must call it
    // explicitly to keep the REST server responsive.
    if (wifiConnected && wm.server) {
        wm.server->handleClient();
    }
    wm.process();
    resetButton.read();
    ArduinoOTA.handle();

    unsigned long currentMillis = millis();
    if (previousMillisNTP > currentMillis) { previousMillisNTP = 0; }

    historyTick();
    eventsTick();

    // Sensor cache is refreshed by a background task on the second core.

    // NTP sync + WiFi health check — every 60s
    if (currentMillis - previousMillisNTP >= 60000)
    {
        // A WiFi reconnect scans the shared radio and usually drops the BLE link,
        // so normally we wait while a BLE client is connected. But never longer
        // than WIFI_FORCE_RECONNECT_MS: otherwise a long BLE session keeps the
        // device unreachable over the network indefinitely.
        static const unsigned long WIFI_FORCE_RECONNECT_MS = 5UL * 60UL * 1000UL;
        static unsigned long wifiDownSince = 0;

        if (WiFi.status() != WL_CONNECTED)
        {
            if (wifiDownSince == 0) wifiDownSince = currentMillis;
            bool force = currentMillis - wifiDownSince >= WIFI_FORCE_RECONNECT_MS;

            if (!bleOwnsRadio() || force)
            {
                onboardLED.off();
                onboardLED.flash();
                if (bleOwnsRadio())
                {
                    Serial.println("WiFi down for 5 min while BLE is active - forcing reconnect (BLE may drop)");
                    wifiDownSince = currentMillis;  // next forced attempt after another 5 min
                }
                else
                {
                    Serial.println("Reconnecting to WiFi...");
                }
                WiFi.disconnect(false);  // reset state machine, keep credentials
                WiFi.begin();            // reconnect with stored credentials
            }
        }
        else
        {
            wifiDownSince = 0;
            getNTPtime(2);      // snapshot of the system clock + DST flag (SNTP syncs it in the background)
            onboardLED.on();
        }
        previousMillisNTP = currentMillis;
    }
}
