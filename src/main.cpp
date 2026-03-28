#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ota.h>
#include <constants.h>
#include <setupWifi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <metrics.h>

#include <helperFunctions.h>
#include "Stove.h"

#define FORMAT_SPIFFS_IF_FAILED true


bool getNTPtime(int sec)
{

    {
        uint32_t start = millis();
        do
        {
            time(&now);
            localtime_r(&now, &timeinfo);
#if SENSOR_DEBUG
            Serial.print(".");
#endif
            delay(10);
        } while (((millis() - start) <= (1000 * sec)) && (timeinfo.tm_year < (2016 - 1900)));
        if (timeinfo.tm_year <= (2016 - 1900))
            return false; // the NTP call was not successful
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

int getFumeFanRPM()
{
    int rpm = stove.read(ram, 0x37);
    if (rpm > 0)
    {
        rpm = (rpm + 25) * 10;
    }
    return rpm;
}

int getRemoteControlLastSeen()
{
    int ttl = stove.read(ram, 0x2e);
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
    ambTempStove = stove.read(ram, 0x44);
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
    ambTempRemoteControl = stove.read(ram, 0x8f);
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
    return ( stove.read(eeprom, 0xef) << 8 ) + stove.read(eeprom, 0xee);
}

bool stoveBeepIsEnabled() // Get room temperature
{
    ambTemp = stove.read(eeprom, 0x4b);
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
    int val = stove.read(ram, 0xa4);
    if (val == 5)
    {
        val = stove.read(ram, 0x89);
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

void writeDatepartToStove(char format[3], byte address)
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
}

void setStoveDateTime()
{
    getNTPtime(2);
    writeDatepartToStove("%d", 0xfb); // Write NTP date part: Day
    writeDatepartToStove("%m", 0xfc); // Write NTP date part: Month
    writeDatepartToStove("%y", 0xfd); // Write NTP date part: Year
    writeDatepartToStove("%H", 0xf9); // Write NTP date part: Hour
    writeDatepartToStove("%M", 0xfa); // Write NTP date part: Minute
    writeDatepartToStove("%W", 0xf8); // Write NTP date part: Day of Week
}

void getRoomFanSpeed()
{
    roomFanSpeed = stove.read(eeprom, 0x81);
#if SENSOR_DEBUG
    Serial.printf("Fan speed: %d\n", roomFanSpeed);
#endif
}

void getFlamePower() // Get the flame power (0, 1, 2, 3, 4, 5)
{
    flamePower = stove.read(eeprom, 0x7f);
}

void getEnergySaving() 
{
    energySavingStart = stove.read(eeprom, 0x91)*-1;
    energySavingStop = stove.read(eeprom, 0x90);
}


int getErrorMemory(int pos);
void getStoveDateTime();

void getStates() // Refreshes the full sensor cache — called from loop(), not from HTTP handlers
{
    getStoveState();
    getAmbTemp();
    getAmbTempStove();
    getAmbTempRemoteControl();
    getFumeTemp();
    getFlamePower();
    getRoomFanSpeed();
    getTempSetpoint();
    getEnergySaving();
    cachedLowPallet          = arePalletsLow();
    cachedBeepEnabled        = stoveBeepIsEnabled();
    cachedNumberOfStarts     = numberOfStarts();
    cachedFumeFanRPM         = getFumeFanRPM();
    cachedRemoteControlLastSeen = getRemoteControlLastSeen();
    for (int i = 0; i < 5; i++) {
        cachedErrors[i] = getErrorMemory(i);
    }
    getStoveDateTime(); // updates stoveTime global
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
    stove.write(ram, 0x1a, menu);
    return true;
}

bool setEnergySavingStart(int start)
{
    if ((start >= 0) && (start <= 3))
    {
        if (stove.write(eeprom, 0x91, start) ) {
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
        if (stove.write(eeprom, 0x90, stop) ) {
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
        if (stove.write(eeprom, 0x81, speed) ) {
            return stove.write(ram, 0x19, speed);
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
    stove.write(eeprom, 0xc0, speed);
}

int readErrorMemory(int pos)
{
    switch (pos)
    {
    case 0 ... 4:
        pos = 0xe0 + pos;
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
        pos = 0xe0 + pos;
        error = stove.read(eeprom, byte(pos));
        break;
    }
    return error;
}

bool setFlamePower(int power)
{
    if ((power >= 1) && (power <= 4))
    {
        return stove.write(eeprom, 0x7f, power);
    }
    else
    {
        log_e("Flamepower not in range, it should be between 0-3\n");
        return false;
    }
}

void setError(int pos, int error)
{
    int address = 0xe0 + pos;
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

void handleRouteRemote()
{
    StaticJsonDocument<200> json;
    json.clear();

    if (wm.server->method() != HTTP_GET)
    {
        wm.server->send(405);
        return;
    }

    int menuId = stove.read(ram, 0x1A);
    if (menuId < 0)
    {
        json["error"] = "Failed to read remote menu - RS232 error";
        serializeJson(json, buffer);
        wm.server->send(500, "application/json", buffer);
        return;
    }

    json["diplayed_menu"] = getRemoteMenuName(menuId);

    int paramId = stove.read(ram, 0x18);
    if (paramId < 0)
    {
        json["selected_parameter"] = "Unknown";
    }
    else
    {
        switch (paramId)
        {
            case 0:   json["selected_parameter"] = nullptr;                 break;
            case 71:  json["selected_parameter"] = "Multi Comfort";         break;
            case 76:  json["selected_parameter"] = "Beep";                  break;
            case 79:  json["selected_parameter"] = "Display settings";      break;
            case 80:  json["selected_parameter"] = "Language";              break;
            case 123: json["selected_parameter"] = "State Stove";           break;
            case 128: json["selected_parameter"] = "Flame power";           break;
            case 130: json["selected_parameter"] = "MultiFuoco";            break;
            case 145: json["selected_parameter"] = "Energy Saving - Stop";  break;
            case 146: json["selected_parameter"] = "Energy Saving - Start"; break;
            case 193: json["selected_parameter"] = "Combustion quality";    break;
            case 198: json["selected_parameter"] = "Pallet quality";        break;
            case 200: json["selected_parameter"] = "Multi Comfort Plus";    break;
            case 235: json["selected_parameter"] = "WPS";                   break;
            case 236: json["selected_parameter"] = "Reset";                 break;
            case 248: json["selected_parameter"] = "Day of week";           break;
            case 249: json["selected_parameter"] = "Hour";                  break;
            case 250: json["selected_parameter"] = "min";                   break;
            case 251: json["selected_parameter"] = "Day";                   break;
            case 252: json["selected_parameter"] = "Month";                 break;
            case 253: json["selected_parameter"] = "year";                  break;
            default:
                snprintf(buffer, sizeof(buffer), "Unknown (%d)", paramId);
                json["selected_parameter"] = buffer;
                break;
        }
    }

    int langId = stove.read(eeprom, 0x4F);
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
    int cur_min = stove.read(ram, 0x7d);
    int cur_hour = stove.read(ram, 0x7c);
    int cur_sec = stove.read(ram, 0x7a);
    int cur_day = stove.read(ram, 0x7e);
    int cur_month = stove.read(ram, 0x7f);
    int cur_year = stove.read(ram, 0x80);

    sprintf(buffer, "20%i-%02d-%02d %02d:%02d:%02d", cur_year, cur_month, cur_day, cur_hour, cur_min, cur_sec);

    strptime(buffer, "%Y-%m-%d %H:%M:%S", &tm);
    // Take the current NTP based DST setting in order to interprete the stove it's Time
    // The stove does not support DST so a time change is required
    tm.tm_isdst = timeinfo.tm_isdst;
    stoveTime = mktime(&tm);
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
        json["result"] = "success Synced NTP time to stove";
    }
    
    if (wm.server->method() == HTTP_GET || wm.server->method() == HTTP_PATCH )
    {
        JsonObject time = json.createNestedObject("time");
        getStoveDateTime();
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
        JsonObject errorbank1 = json.createNestedObject("MemoryPos_1");
        int error = getErrorMemory(0);
        errorbank1["id"] = error;
        errorbank1["error"] = getErrorCode(error);
        errorbank1["desc"] = getErrorDesc(error);

        JsonObject errorbank2 = json.createNestedObject("MemoryPos_2");
        error = getErrorMemory(1);
        errorbank2["id"] = error;
        errorbank2["error"] = getErrorCode(error);
        errorbank2["desc"] = getErrorDesc(error);

        JsonObject errorbank3 = json.createNestedObject("MemoryPos_3");
        error = getErrorMemory(2);
        errorbank3["id"] = error;
        errorbank3["error"] = getErrorCode(error);
        errorbank3["desc"] = getErrorDesc(error);

        JsonObject errorbank4 = json.createNestedObject("MemoryPos_4");
        error = getErrorMemory(3);
        errorbank4["id"] = error;
        errorbank4["error"] = getErrorCode(error);
        errorbank4["desc"] = getErrorDesc(error);

        JsonObject errorbank5 = json.createNestedObject("MemoryPos_5");
        error = getErrorMemory(4);
        errorbank5["id"] = error;
        errorbank5["error"] = getErrorCode(error);
        errorbank5["desc"] = getErrorDesc(error);
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

void handleRoutePower()
{
    StaticJsonDocument<350> json;
    json.clear();
    int http_code;
    http_code=200;

    getStoveState();
    

    if (wm.server->method() == HTTP_POST)
    {
        for (uint8_t i = 0; i < wm.server->args(); i++)
        {
            if (wm.server->argName(i) == "state")
            {
                if (  wm.server->arg(i) == "off" ) 
                {
                    String message;
                    switch (stoveState)
                    {
                        case OFF:
                        case ALARM_STATE:
                        case IGNITION_FAILURE:
                        case STANDBY:
                            message = "Stove already off";
                            http_code=202;
                        break;
                        case STARTING:
                        case PALLET_LOADING:
                            if (stove.write(ram, stoveStateAddr, 0))
                            {
                                message = "Powered off sended - 0";
                            }
                            else
                            {
                                message = "FAILED to send Power off - 0";
                                http_code=500;
                            }
                        break;
                        case IGNITION:
                        case WORKING:
                        case ALARM:
                            if (stove.write(ram, stoveStateAddr, 6))
                            {
                                message = "Powered off sended - 6";
                            }
                            else
                            {
                                message = "FAILED to send Power off - 6";
                                http_code=500;
                            }
                        break;
                        case BRAZIER_CLEANING:
                            message = "Stove already shutting down - Brazier cleaning";
                            http_code=202;
                        break;
                        case FINAL_CLEANING:
                            message = "Stove already shutting down - Final cleaning";
                            http_code=202;
                        break;
                        case UNKNOWN:
                            StoveStateStr = "Unkown [RS232 Serial Error]";
                            http_code=500;
                        break;

                    }
                    json["result"] = message;
                } else if (wm.server->arg(i) == "on")
                {
                    String message;
                    if (stoveState == 0)
                    {
                        if (stove.write(ram, stoveStateAddr, 1))
                        {
                            message = "Powered on sended";
                        } 
                        else
                        {
                            message = "FAILED to send Power on";
                            http_code=500;
                        }
                    } 
                    else
                    {
                        message = "Stove already on";
                        Serial.print("Stove already on\n");
                        http_code=202;
                    }
                    json["result"] = message;
                } else 
                {
                    json["result"] = "Unknown state argument";
                    http_code=400;
                }
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
    String message;
    int http_code;
    http_code=200;
    StaticJsonDocument<250> json;
    json.clear();
    getStoveState();
    if (stoveState == 0)
    {
        if (stove.write(ram, stoveStateAddr, 1))
        {
            message = "Powered on sended";
        }
        else
        {
            message = "FAILED to send Power on";
            http_code = 500;
        }
    }
    else
    {
        message = "Stove already on";
        http_code = 202;
        Serial.print("Stove already on\n");
    }

    json["result"] = message;

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
    String message;
    getStoveState();
    Serial.printf("  StoveState == %i\n", stoveState);
 
    switch (stoveState)
    {
    case OFF:
    case ALARM_STATE:
    case IGNITION_FAILURE:
    case STANDBY:
        message = "Stove already off";
        break;
    case STARTING:
    case PALLET_LOADING:
        if (stove.write(ram, stoveStateAddr, 0))
        {
            message = "Powered off sended - 0";
        }
        else
        {
            message = "FAILED to send Power off - 0";
        }
        break;
    case IGNITION:
    case WORKING:
    case ALARM:
        if (stove.write(ram, stoveStateAddr, 6))
        {
            message = "Powered off sended - 6";
        }
        else
        {
            message = "FAILED to send Power off - 6";
        }
        break;
    case BRAZIER_CLEANING:
        message = "Stove already shutting down - Brazier cleaning";
        break;
    case FINAL_CLEANING:
        message = "Stove already shutting down - Final cleaning";
        break;
    case UNKNOWN:
        StoveStateStr = "Unkown [RS232 Serial Error]";
        break;
    }

    StaticJsonDocument<250> json;
    json.clear();

    json["result"] = message;
    getStoveState();
    JsonObject obj_stove = json.createNestedObject("stoveState");
    obj_stove["stoveState"] = stoveState;
    obj_stove["state"] = StoveStateStr;
    obj_stove["poweredOn"] = StoveIsOn;

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void storeAllRamValues()
{
    telnet.println("Getting all values : ");
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
                    sprintf(buffer, "0x%02X   - 0x%02X\n       + 0x%02X", byte(i), storedRam[byte(i)], value);
                    telnet.print(buffer);
                }
            }
            storedRam[byte(i)] = value;
        }
    }
    storedRam[265] = 1;
    telnet.println("[Done]");
}

void getParams()
{
    // storeAllRamValues();
    StaticJsonDocument<350> json;
    json.clear();
    char data[2];

    data[0] = ram;

    // json["test"] = (wm.server->method() == HTTP_GET) ? "GET" : "POST";

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

    // Serial.printf("send[0] = 0x%02X\n", data[0]);
    // Serial.printf("send[1] = 0x%02X\n", data[1]);

    sprintf(buffer, "0x%02X", data[0]);
    JsonObject type = json.createNestedObject(buffer);

    sprintf(buffer, "0x%02X", data[1]);
    JsonObject address = type.createNestedObject(buffer);

    char value = stove.read(data[0], data[1]);
    if (value >= 0)
    {
        address["Address_int"] = int(data[1]);
        address["Value_int"] = int(value);
        address["Value_txt"] = String(value);
        sprintf(buffer, "0x%02X", value);
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
    telnet.println("Get status called");

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
    json["fumeFanRPM"] = cachedFumeFanRPM;
    json["RemoteControlLastSeen"] = cachedRemoteControlLastSeen;

    JsonObject obj_errors = json.createNestedObject("previousErrors");
    const char* memKeys[5] = {"MemoryPos_1","MemoryPos_2","MemoryPos_3","MemoryPos_4","MemoryPos_5"};
    for (int i = 0; i < 5; i++) {
        JsonObject eb = obj_errors.createNestedObject(memKeys[i]);
        eb["id"]    = cachedErrors[i];
        eb["error"] = getErrorCode(cachedErrors[i]);
        eb["desc"]  = getErrorDesc(cachedErrors[i]);
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

void onTelnetConnect(String ip)
{
    Serial.print("- Telnet: ");
    Serial.print(ip);
    Serial.println(" connected to Stove");
    telnet.println("\nWelcome " + telnet.getIP());
    telnet.println("(Use ^] + q  to disconnect.)");
}

void setupTelnet()
{
    // passing on functions for various telnet events
    telnet.onConnect(onTelnetConnect);
    //   telnet.onConnectionAttempt(onTelnetConnectionAttempt);
    //   telnet.onReconnect(onTelnetReconnect);
    //   telnet.onDisconnect(onTelnetDisconnect);

    // passing a lambda function
    telnet.onInputReceived([](String str)
                           {
    // checks for a certain command
    if (str == "ping") {
      telnet.println("> pong");
      Serial.println("- Telnet: pong");
    } });

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
    Serial.println("Reading values");
    // int line = 0;
    while (configFile.available())
    {
        String hydroString = configFile.readStringUntil('\n');

        // mqtt_port = portString.c_str();
        // mqtt_port.trim();
        // int_mqtt_port = mqtt_port.toInt();

        hydro_mode = hydroString.c_str();
        hydro_mode.trim();
        int_hydro_mode = hydro_mode.toInt();
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
    wm.server->on("/get", getParams);
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
    wm.server->on("/api", handleRouteSwaggerUI);    
    wm.server->on("/api-specs.json", handleRouteSwaggerJSON);   

    configTzTime(defaultTimezone, ntpServer); // sets TZ and starts NTP sync
    getNTPtime(2);

    if (isConnected())
    {
        setupTelnet();
    }
    getStates();
}

void loop()
{
    telnet.loop();
    esp_task_wdt_reset();
    wm.process();
    resetButton.read();
    ArduinoOTA.handle();

    unsigned long currentMillis = millis();
    if (previousMillis > currentMillis)    { previousMillis = 0; }
    if (previousMillisNTP > currentMillis) { previousMillisNTP = 0; }

    // Sensor cache refresh — only while WiFi is up; previousMillis reset AFTER
    // getStates() returns so the ~1.4s UART cycle does not immediately re-trigger.
    if (currentMillis - previousMillis >= 2000)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            getStates();
        }
        previousMillis = millis(); // reset after blocking call, not before
    }

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