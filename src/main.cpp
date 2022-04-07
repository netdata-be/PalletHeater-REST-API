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

#define FORMAT_SPIFFS_IF_FAILED true

bool isConnected()
{
    return (WiFi.status() == WL_CONNECTED);
}

byte readFromStove(byte type, byte address)
{
    byte message[2] = {type, address};

    Serial.printf("Serial >>> { 0x%02X, 0x%02X }\n", message[0], message[1]);

    StoveSerial.flush();
    StoveSerial.write(message, sizeof(message));

    // Let's check the reply of the stove
    // We expect a 2 byte answer back but since we see our own sended data as echo back we need to read 4 bytes
    char stoveRxData[4];
    if (StoveSerial.readBytes(stoveRxData, 4) == 4)
    {
        Serial.printf("Serial <<< { 0x%02X, 0x%02X, 0x%02X, 0x%02X }\n", stoveRxData[0], stoveRxData[1], stoveRxData[2], stoveRxData[3]);

        byte checksum = stoveRxData[2];
        byte val = stoveRxData[3];
        byte address_response = checksum - val - type;
        Serial.printf("Serial === %i\n", int(val));
        if (address_response == address)
        {
            Serial.printf("Serial === %i\n", int(val));
            delay(10);
            return val;
        }
        else
        {
            log_e("Checksum does not match");
            return -1;
        }
    }
}

bool writeToStove(byte type, byte address, byte value)
{

    const byte writeByte = 0x80;
    uint8_t checksum_calc = (writeByte + type + address + value) & 0xFF;
    byte message[4] = {writeByte + type, address, value, checksum_calc};

    Serial.printf("Sending to Stove = { 0x%02X, 0x%02X, 0x%02X, 0x%02X}\n", message[0], message[1], message[2], message[3]);

    StoveSerial.write(message, sizeof(message));

    // Let's check the reply of the stove
    // We expect a 2 byte answer back but since we see our own sended data as echo back we need to read 6 bytes
    char stoveRxData[6];
    if (StoveSerial.readBytes(stoveRxData, 6) != 0)
    {
        Serial.printf("Received from Stove = { 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}\n", stoveRxData[0], stoveRxData[1], stoveRxData[2], stoveRxData[3], stoveRxData[4], stoveRxData[5]);
        // Bytes 0-3 are just echo from our TX line
        byte checksum = stoveRxData[4];
        byte value_response = stoveRxData[5];
        Serial.printf("Received back: checksum=0x%02X Value=0x%02X\n", checksum, value_response);

        uint8_t address_response = (checksum - writeByte - type - value_response) & 0xFF;

        if ((address_response == address) & (value_response == value))
        {
            Serial.printf("  [SUCESS]\n");
            return true;
        }
        else
        {
            Serial.printf("  [FAIL]\n");
            return false;
        }
    }
}

void getStoveState() // Get detailed stove state
{
    stoveState = readFromStove(ram, stoveStateAddr);

    switch (stoveState)
    {
    case 0:
        StoveStateStr = "Off";
        StoveIsOn = false;
        break;
    case 1:
        StoveStateStr = "Starting";
        StoveIsOn = true;
        break;
    case 2:
        StoveStateStr = "Pellet loading"; // Phase I
        StoveIsOn = true;
        break;
    case 3:
        StoveStateStr = "Ignition"; // Phase II
        StoveIsOn = true;
        break;
    case 4:
        StoveStateStr = "Working";
        StoveIsOn = true;
        break;
    case 5:
        StoveStateStr = "Brazier cleaning";
        break;
    case 6:
        StoveStateStr = "Final cleaning"; // Shut Down
        StoveIsOn = false;
        break;
    case 7:
        StoveStateStr = "Standby";
        StoveIsOn = false;
        break;
    case 8:
        StoveStateStr = "Alarm State";
        break;
    case 9:
        StoveStateStr = "Ignition failure";
        StoveIsOn = false;
        break;
    case 10:
        StoveStateStr = "Alarm";
        StoveIsOn = false;
        break;
    case -1:
        StoveStateStr = "Unkown [RS232 Serial Error]";
        StoveIsOn = false;
        break;
    }
    Serial.printf("Stove %s : %s\n", StoveIsOn ? "ON" : "OFF", StoveStateStr.c_str());
}

void getAmbTemp() // Get room temperature
{
    ambTemp = readFromStove(ram, ambTempAddr);
    if (ambTemp >= 0)
    {
        ambTemp = (float)ambTemp / 2;
    }
    else
    {
        log_e("Failed to get value");
    }
    Serial.print("T. amb. ");
    Serial.println(ambTemp);
}

int getFumeFanRPM()
{
    int rpm = readFromStove(ram, 0x37);
    if (rpm > 0)
    {
        rpm = (rpm + 25) * 10;
    }
    return rpm;
}

int getRemoteControlLastSeen()
{
    int ttl = readFromStove(ram, 0x2e);
    if (ttl > 0)
    {
        ttl = 240 - ttl;
    } else 
    {
        ttl = -1;
    }
    return ttl;
}

void getAmbTempStove() // Get room temperature from stove sensor
{
    ambTempStove = readFromStove(ram, 0x44);
    if (ambTempStove >= 0)
    {
        ambTempStove = (float)ambTempStove / 2;
    }
    else
    {
        log_e("Failed to get value");
    }
    Serial.print("T. amb. stove : ");
    Serial.println(ambTempStove);
}

void getAmbTempRemoteControl() // Get room temperature from stove sensor
{
    ambTempRemoteControl = readFromStove(ram, 0x8f);
    if (ambTempRemoteControl >= 0)
    {
        ambTempRemoteControl = (float)ambTempRemoteControl / 2;
    }
    else
    {
        log_e("Failed to get value");
    }
    Serial.print("T. amb. remote control : ");
    Serial.println(ambTempRemoteControl);
}

int numberOfStarts()
{
    return readFromStove(eeprom, 0xee);
}

bool stoveBeepIsEnabled() // Get room temperature
{
    ambTemp = readFromStove(eeprom, 0x4b);
    if (ambTemp = 0)
    {
        return false;
    }
    else
    {
        return true;
    }
}

int arePalletsLow() // Get room temperature
{
    int val = readFromStove(ram, 0xa4);
    if (val = 0)
    {
        return 0;
    }
    else
    {
        return val;
    }
}

void getTempSetpoint()
{
    setPointTemp = (float)readFromStove(eeprom, TempSetpointAddr);
    Serial.print("T. setpoint. ");
    Serial.println(setPointTemp);
}

void getFumeTemp() // Get flue gas temperature
{
    fumesTemp = readFromStove(ram, fumesTempAddr);
    Serial.printf("T. fumes %d\n", fumesTemp);
}

void setStoveDateTime()
{
    char time_output[30];

    strftime(time_output, 30, "%d", localtime(&now));
    writeToStove(ram, 0x7e, byte(atoi(time_output)));

    strftime(time_output, 30, "%m", localtime(&now));
    writeToStove(ram, 0x7f, atoi(time_output));

    strftime(time_output, 30, "%y", localtime(&now));
    writeToStove(ram, 0x80, atoi(time_output));

    strftime(time_output, 30, "%H", localtime(&now));
    writeToStove(ram, 0x7c, byte(atoi(time_output)));

    strftime(time_output, 30, "%M", localtime(&now));
    writeToStove(ram, 0x7d, atoi(time_output));

    strftime(time_output, 30, "%S", localtime(&now));
    writeToStove(ram, 0x7a, atoi(time_output));

    strftime(time_output, 30, "%W", localtime(&now));
    writeToStove(ram, 0x7b, atoi(time_output));
}

void getStoveDateTime()
{
    int cur_min = readFromStove(ram, 0x7d);
    int cur_hour = readFromStove(ram, 0x7c);
    int cur_sec = readFromStove(ram, 0x7a);
    int cur_day = readFromStove(ram, 0x7e);
    int cur_month = readFromStove(ram, 0x7f);
    int cur_year = readFromStove(ram, 0x80);

    sprintf(buffer, "20%i-%02d-%02d %02d:%02d:%02d", cur_year, cur_month, cur_day, cur_hour, cur_min, cur_sec);
    // return buffer;
    strptime(buffer, "%Y-%m-%d %H:%M:%S", &tm);
    // Take the current NTP based DST setting in order to interprete the stove it's Time
    // The stove does not support DST so a time change is required
    tm.tm_isdst = timeinfo.tm_isdst;
    stoveTime = mktime(&tm);
}

void getRoomFanSpeed()
{
    roomFanSpeed = readFromStove(eeprom, 0x81);
    Serial.printf("Fan speed: %d\n", roomFanSpeed);
}

void getFlamePower() // Get the flame power (0, 1, 2, 3, 4, 5)
{
    // flamePower = readFromStove(ram, flamePowerAddr);
    flamePower = readFromStove(eeprom, 0x7f);
}

void getStates() // Calls all the get…() functions
{
    getStoveState();
    getAmbTemp();
    getAmbTempStove();
    getAmbTempRemoteControl();
    getFumeTemp();
    getFlamePower();
    getRoomFanSpeed();
    getTempSetpoint();
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

void setRoomFanSpeed(int speed)
{
    if ((speed >= 0) & (speed <= 3))
    {
        writeToStove(eeprom, 0x81, speed); // Write to EEPROM, this triggers the fan adjustment
        writeToStove(ram, 0x19, speed);    // Write to RAM, as a status update ???
    }
    else
    {
        log_e("Speed not in range, it should be between 0-3\n");
    }
}

void setCombustionQuality(int speed)
{
    writeToStove(eeprom, 0xc0, speed);
}

int readErrorMemory(int pos)
{
    switch (pos)
    {
    case 0 ... 4:
        pos = 0xe0 + pos;
        return readFromStove(eeprom, byte(pos));
        break;
    }
}

int getErrorMemory(int pos)
{
    int error = -1 ; 
    switch (pos)
    {
    case 0 ... 4:
        pos = 0xe0 + pos;
        error = readFromStove(eeprom, byte(pos));
        break;
    }
    return error;
}

String getErrorCode(int error)
{
    String errorCode;
    switch (error)
    {
    case 0:
        errorCode = "---";
        break;
    case 1:
        errorCode = "E8";
        break;
    case 2:
        errorCode = "E4";
        break;
    case 4:
        errorCode = "E9";
        break;
    case 8:
        errorCode = "E7";
        break;
    case 16:
        errorCode = "E3";
        break;
    case 32:
        errorCode = "E1";
        break;
    case 64:
        errorCode = "E2";
        break;
    case 128:
        errorCode = "E6";
        break;
    case 129:
        errorCode = "E12";
        break;
    case 130:
        errorCode = "E11";
        break;
    case 132:
        errorCode = "E19";
        break;
    case 136:
        errorCode = "E13";
        break;
    case 255:
        errorCode = "PowerError";
        break;
    }
    return errorCode;
          
}

String getErrorDesc(int error)
{
    String errorCode;
    switch (error)
    {
    case 0:
        errorCode = "No Error";
        break;
    case 1:
        errorCode = "Fume sensor error";
        break;
    case 2:
        errorCode = "Fume temperature to high";
        break;
    case 4:
        errorCode = "Ignition temperature not reached within limit";
        break;
    case 8:
        errorCode = "Fume temperatuer to low";
        break;
    case 16:
        errorCode = "Temperature inside stove to high";
        break;
    case 32:
        errorCode = "Pallet loading door is open for to long or air pressure inside stove is wrong";
        break;
    case 64:
        errorCode = "Pressure sensor error during ignition";
        break;
    case 128:
        errorCode = "Auger error - To much pallets are dispatched";
        break;
    case 129:
        errorCode = "Fume fan error";
        break;
    case 130:
        errorCode = "Unknown error - Not described in manual";
        break;
    case 132:
        errorCode = "External contact N.PEL / Pallet is active";
        break;
    case 136:
        errorCode = "Unknown error - Not described in manual";
        break;
    case 255:
        errorCode = "Display shows power error";
        break;
    }
    return errorCode;
          
}


void setFlamePower(int power)
{
    if ((power >= 1) & (power <= 4))
    {
        writeToStove(eeprom, 0x7f, power);
    }
    else
    {
        log_e("Flamepower not in range, it should be between 0-3\n");
    }
}

void setError(int pos, int error)
{
    int address = 0xe0 + pos;
    writeToStove(eeprom, address, error);
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

void handleRouteFanspeed()
{
    StaticJsonDocument<350> json;
    json.clear();

    for (uint8_t i = 0; i < wm.server->args(); i++)
    {
        if (wm.server->argName(i) == "s")
        {
            int speed = wm.server->arg(i).toInt();
            setRoomFanSpeed(speed);
        }
    }

    json["action"] = "ok";

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

void handleRouteFlamePower()
{
    StaticJsonDocument<350> json;
    json.clear();

    if (wm.server->method() == HTTP_GET)
    {
        getFlamePower();
        json["power"] = flamePower;
    }

    if (wm.server->method() == HTTP_POST)
    {
        if (wm.server->hasArg("plain") == false)
        {
            // handle error here
        }
        StaticJsonDocument<350> jsonDocument;
        String body = wm.server->arg("plain");
        deserializeJson(jsonDocument, body);

        int power = jsonDocument["power"];
        setFlamePower(power);
        getFlamePower();

        json["power"] = flamePower;
    }

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void setStoveOn()
{
    String message;
    StaticJsonDocument<250> json;
    json.clear();
    getStoveState();
    if (stoveState > 5)
    {
        if (writeToStove(ram, stoveStateAddr, 1))
        {
            message = "Powered on sended";
        }
        else
        {
            message = "FAILED to send Power on";
        }
    }
    else if (stoveState == 0)
    {
        if (writeToStove(ram, stoveStateAddr, 1))
        {
            message = "Powered on sended";
        }
        else
        {
            message = "FAILED to send Power on";
        }
    }
    else
    {
        message = "Stove already on";
        Serial.print("Stove already on\n");
    }

    json["state"] = message;
    serializeJson(json, buffer);
    getStoveState();
    wm.server->send(200, "application/json", buffer);
}

void setStoveOff()
{
    String message;
    getStoveState();
    Serial.printf("  StoveState == %i\n", stoveState);

    switch (stoveState)
    {
    case 0:
        message = "Stove already off";
        break;
    case 1:
        if (writeToStove(ram, stoveStateAddr, 0))
        {
            message = "Powered off sended - 0";
        }
        else
        {
            message = "FAILED to send Power off - 0";
        }
        break;
    case 2:
        if (writeToStove(ram, stoveStateAddr, 0))
        {
            message = "Powered off sended - 0";
        }
        else
        {
            message = "FAILED to send Power off - 0";
        }
        break;
    case 3:
        if (writeToStove(ram, stoveStateAddr, 6))
        {
            message = "Powered off sended - 6";
        }
        else
        {
            message = "FAILED to send Power off - 6";
        }
        break;
    case 4:
        if (writeToStove(ram, stoveStateAddr, 6))
        {
            message = "Powered off sended - 6";
        }
        else
        {
            message = "FAILED to send Power off - 6";
        }
        break;
    case 5:
        message = "Stove already shutting down - Brazier cleaning";
        break;
    case 6:
        message = "Stove already shutting down - Final cleaning";
        break;
    case 7:
        message = "Stove already off";
        break;
    case 8:
        if (writeToStove(ram, stoveStateAddr, 6))
        {
            message = "Powered off sended - 6";
        }
        else
        {
            message = "FAILED to send Power off - 6";
        }
        break;
    case 9:
        message = "Stove already off";
        break;
    case 10:
        message = "Stove already off";
        break;
    case -1:
        StoveStateStr = "Unkown [RS232 Serial Error]";
        break;
    }

    StaticJsonDocument<250> json;
    json.clear();

    json["state"] = message;
    serializeJson(json, buffer);
    getStoveState();
    wm.server->send(200, "application/json", buffer);
}

void storeAllRamValues()
{
    telnet.println("Getting all values : ");
    for (int i = 0; i <= 255; i++)
    {
        esp_task_wdt_reset();
        char value = readFromStove(ram, byte(i));
        if (value >= 0)
        {
            //telnet.print(".");
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
    //storeAllRamValues();
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

        // if (wm.server->argName(i) == "hex")
        // {
        //     //char *eptr;
        //     //char charBuf[2];

        //     int str_len = wm.server->arg(i).length() + 1;
        //     char char_array[str_len];
        //     wm.server->arg(i).toCharArray(char_array, str_len);

        //     int number = (int)strtol(char_array, NULL, 16);
        //     Serial.printf("AAA = ");
        //     Serial.printf(number);
        //     Serial.printf("\n");

        //     //data[1] = strtoul(wm.server->arg(i).toCharArray(charBuf, 2), &eptr, 16);
        // }
    }

    // Serial.printf("send[0] = 0x%02X\n", data[0]);
    // Serial.printf("send[1] = 0x%02X\n", data[1]);

    sprintf(buffer, "0x%02X", data[0]);
    JsonObject type = json.createNestedObject(buffer);

    sprintf(buffer, "0x%02X", data[1]);
    JsonObject address = type.createNestedObject(buffer);

    char value = readFromStove(data[0], data[1]);
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

bool getNTPtime(int sec)
{

    {
        uint32_t start = millis();
        do
        {
            time(&now);
            localtime_r(&now, &timeinfo);
            Serial.print(".");
            delay(10);
        } while (((millis() - start) <= (1000 * sec)) && (timeinfo.tm_year < (2016 - 1900)));
        if (timeinfo.tm_year <= (2016 - 1900))
            return false; // the NTP call was not successful
        Serial.print("now ");
        Serial.println(now);
        char time_output[30];
        strftime(time_output, 30, "%a  %d-%m-%y %T", localtime(&now));
        Serial.println(time_output);
        Serial.println();
    }
    return true;
}

void getStatus()
{
    telnet.println("Get status called");
    getStates();

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

    json["RoomFanSpeed"] = roomFanSpeed;    

    json["lowPalletAlarm"] = arePalletsLow(); 

    json["stoveBeepEnabled"] = stoveBeepIsEnabled();
    json["numberOfStarts"] = numberOfStarts();
    json["fumeFanRPM"] = getFumeFanRPM();
    json["RemoteControlLastSeen"] = getRemoteControlLastSeen();
    
    JsonObject obj_errors = json.createNestedObject("previousErrors");

    JsonObject errorbank1 = obj_errors.createNestedObject("MemoryPos_1");
    int error = getErrorMemory(0);
    errorbank1["id"] = error;
    errorbank1["error"] = getErrorCode(error);
    errorbank1["desc"] = getErrorDesc(error);
    
    JsonObject errorbank2 = obj_errors.createNestedObject("MemoryPos_2");
    error = getErrorMemory(1);
    errorbank2["id"] = error;
    errorbank2["error"] = getErrorCode(error);
    errorbank2["desc"] = getErrorDesc(error);

    JsonObject errorbank3 = obj_errors.createNestedObject("MemoryPos_3");
    error = getErrorMemory(2);
    errorbank3["id"] = error;
    errorbank3["error"] = getErrorCode(error);
    errorbank3["desc"] = getErrorDesc(error);

    JsonObject errorbank4 = obj_errors.createNestedObject("MemoryPos_4");
    error = getErrorMemory(3);
    errorbank4["id"] = error;
    errorbank4["error"] = getErrorCode(error);
    errorbank4["desc"] = getErrorDesc(error);

    JsonObject errorbank5 = obj_errors.createNestedObject("MemoryPos_5");
    error = getErrorMemory(4);
    errorbank5["id"] = error;
    errorbank5["error"] = getErrorCode(error);
    errorbank5["desc"] = getErrorDesc(error);


    // JsonObject time = json.createNestedObject("time");
    // time["stove"] = getStoveDateTime();

    // getNTPtime(2);
    // char time_output[30];

    // strftime(time_output, 30, "%F %T", localtime(&now));
    // time["ntp"] = time_output;

    // getStoveDateTime();
    // strftime(time_output, 30, "%F %T", localtime(&stoveTime));
    // time["stove"] = time_output;

    // time["delta"] = now - stoveTime;

    // strftime(time_output, 30, "%d", localtime(&now));
    // time["day"] = atoi(time_output);
    // strftime(time_output, 30, "%m", localtime(&now));
    // time["month"] = atoi(time_output);
    // strftime(time_output, 30, "%y", localtime(&now));
    // time["year"] = atoi(time_output);
    // strftime(time_output, 30, "%H", localtime(&now));
    // time["hour"] = atoi(time_output);
    // strftime(time_output, 30, "%M", localtime(&now));
    // time["min"] = atoi(time_output);
    // strftime(time_output, 30, "%S", localtime(&now));
    // time["seconds"] = atoi(time_output);
    // strftime(time_output, 30, "%W", localtime(&now));
    // time["weekday"] = atoi(time_output);

    // setStoveDateTime();

    // if (getStatesUpdatedAt == 0)
    // {
    //     json["secondsSinceLastUpdate"] = "-1";
    // }
    // else
    // {
    //     json["secondsSinceLastUpdate"] = (currentMillis - getStatesUpdatedAt) / 1000;
    // }

    // json["secondsSinceLastUpdate"]="foo";
    // serializeJsonPretty(json, buffer);
    serializeJson(json, buffer);
    // Serial.print(buffer);

    // Serial.println ( "test" );
    // byte message[] = {0x80, 0x21, 0x01, 0xA2};

    // StoveSerial.write(message, sizeof(message));
    wm.server->send(200, "application/json", buffer);
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
    StoveSerial.begin(1200, SERIAL_MODE, RX_PIN, TX_PIN, false, 256);
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
    wm.server->on("/fan", handleRouteFanspeed);
    wm.server->on("/error", handleRouteSetError);
    
    wm.server->on("/cq", handleRouteCQ);
    wm.server->on("/flame", handleRouteFlamePower);
    wm.server->on("/off", setStoveOff);
    wm.server->on("/state", getStatus);
    wm.server->on("/get", getParams);

    wm.server->on("/stove/flame", handleRouteFlamePower);
    wm.server->on("/stove/fan", handleRouteFanspeed);
    // getStates();

    // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    //  printLocalTime();
    configTzTime(defaultTimezone, ntpServer); // sets TZ and starts NTP sync
    getNTPtime(2);

    if (isConnected())
    {
        setupTelnet();
    }
}

void loop()
{
    telnet.loop();
    if (Serial.available())
    {
        telnet.print(Serial.read());
    }
    // checkStoveReply();
    esp_task_wdt_reset();
    wm.process();
    resetButton.read();
    ArduinoOTA.handle();

    unsigned long currentMillis = millis();
    if (previousMillis > currentMillis)
    {
        previousMillis = 0;
    }
    if (currentMillis - previousMillis >= 10000)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            onboardLED.off();
            onboardLED.flash();
            Serial.println("Reconnecting to WiFi...");
            WiFi.reconnect();
        }
        if (WiFi.status() == WL_CONNECTED)
        {
            getNTPtime(2);
            onboardLED.on();
        }
        // getStates();
        previousMillis = currentMillis;
    }
}