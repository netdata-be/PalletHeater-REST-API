#pragma once
// Writing settings to the stove (energy saving, fan, flame, errors, display) and
// the combustion / pellet quality settings (/cq, /display, /api/burn).
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

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

const char* getCombustionQualityLabel(int val) { return pc::combustionQualityLabel(val); }
const char* getPelletQualityLabel(int val) { return pc::pelletQualityLabel(val); }
int combustionQualityFromLabel(const String& rawLabel) { return pc::combustionQualityFromLabel(rawLabel.c_str()); }
int pelletQualityFromLabel(const String& rawLabel) { return pc::pelletQualityFromLabel(rawLabel.c_str()); }

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
