#pragma once
// HTTP routes: room fan, error memory, health, flame power, power on/off and the
// legacy single register read (/set).
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

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
