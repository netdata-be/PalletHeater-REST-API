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
#include <reboot.h>

#define FORMAT_SPIFFS_IF_FAILED true



int readFromStove(byte type, byte address)
{
    byte message[2] = {type, address};

    Serial.printf("Serial >>> { 0x%02X, 0x%02X }\n", message[0], message[1]);

    StoveSerial.write(message, sizeof(message));

    // Let's check the reply of the stove
    // We expect a 2 byte answer back but since we see our own sended data as echo back we need to read 4 bytes
    char stoveRxData[4];
    if (StoveSerial.readBytes(stoveRxData, 4) != 0)
    {
        Serial.printf("Serial <<< { 0x%02X, 0x%02X }\n", stoveRxData[2], stoveRxData[3]);

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
    int stoveState = readFromStove(ram, stoveStateAddr);

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
        StoveStateStr = "Pellet missing";
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
    int value = readFromStove(ram, ambTempAddr);
    if (value >= 0)
    {
        ambTemp = (float)value / 2;
        Serial.print("T. amb. ");
        Serial.println(ambTemp);
    }
    else
    {
        ambTemp = -1;
        log_e("Failed to get value");
    }
}

void getTempSetpoint()
{
    int value = readFromStove(eeprom, TempSetpointAddr);
    if (value > 0)
    {
        setPointTemp = (float)value;
        Serial.print("T. setpoint. ");
        Serial.println(setPointTemp);
    }
    else
    {
        setPointTemp = -1;
        log_e("Failed to get value");
    }
}

void getFumeTemp() // Get flue gas temperature
{
    int value = readFromStove(ram, fumesTempAddr);
    if (value >= 0)
    {
        fumesTemp = value;
        Serial.printf("T. fumes %d\n", fumesTemp);
    }
    else
    {
        fumesTemp = -1;
        log_e("Failed to get value");
    }
}

void getRoomFanSpeed()
{
    int value = readFromStove(ram, roomFanAddr);
    if (value >= 0)
    {
        roomFanSpeed = value;
        Serial.printf("Fan speed: %d\n", roomFanSpeed);
    }
    else
    {
        roomFanSpeed = -1;
        log_e("Failed to get value");
    }
}

void getFlamePower() // Get the flame power (0, 1, 2, 3, 4, 5)
{
    int value = readFromStove(ram, flamePowerAddr);
    if (value >= 0)
    {
        flamePower = value;
    }
    else
    {
        flamePower = -1;
        log_e("Failed to get value");
    }
}


void getStates() // Calls all the get…() functions
{
    getStoveState();
    getAmbTemp();
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

void handleRouteFlamePower()
{
    StaticJsonDocument<350> json;
    json.clear();

    for (uint8_t i = 0; i < wm.server->args(); i++)
    {
        if (wm.server->argName(i) == "s")
        {
            int power = wm.server->arg(i).toInt();
            setFlamePower(power);
        }
    }
    json["action"] = "ok";

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
    if (stoveState < 6)
    {
        if (stoveState > 0)
        {
            Serial.print("stoveOff: called\n");
            if (writeToStove(ram, stoveStateAddr, 6))
            {
                message = "Powered off sended";
            }
            else
            {
                message = "FAILED to send Power off";
            }

            // for (uint8_t i = 0; i < wm.server->args(); i++) {
            //     if (wm.server->argName(i) == "force") {
            //         if (wm.server->arg(i) == "1") {
            //             writeToStove(0x00, stoveStateAddr, 0);
            //             message = "Forced powered off sended";
            //         }
            //         else {
            //             writeToStove({0x00}, stoveStateAddr, 6);
            //             message = "Powered off sended";
            //         }
            //     }
            //     else {
            //         writeToStove(0x00, stoveStateAddr, 6);
            //         message = "Powered off sended";
            //     }
            // }
        }
        else
        {
            message = "Stove already off";
            Serial.print("Stove Already Off\n");
        }

        StaticJsonDocument<250> json;
        json.clear();

        json["state"] = message;
        serializeJson(json, buffer);
        getStoveState();
        wm.server->send(200, "application/json", buffer);
    }
}

void getParams()
{
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

    Serial.printf("send[0] = 0x%02X\n", data[0]);
    Serial.printf("send[1] = 0x%02X\n", data[1]);

 
    // int value = readFromStove(data[0], data[1]);
    // if (value >= 0)
    // {
    //     sprintf(buffer, "0x%02X", resp[0]);
    //     JsonObject address = json.createNestedObject(buffer);
    //     address["Address_int"] = int(resp[0]);
    //     address["Value_int"] = int(resp[1]);
    //     sprintf(buffer, "0x%02X", resp[1]);
    //     address["Value_hex"] = buffer;
    // }
    // else
    // {
    //     address["error"] = "Failed to get value RS232 error";
    //     log_e("Failed to get value");
    // }





    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void getStatus()
{
    getStates();
    StaticJsonDocument<350> json;
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
    obj_flame["FumeTemp"] = fumesTemp;

    json["ambTemp"] = ambTemp;
    json["setPointTemp"] = setPointTemp;
    json["RoomFanSpeed"] = roomFanSpeed;

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
    httpRouteReboot();
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
    wm.server->on("/cq", handleRouteCQ);
    wm.server->on("/flame", handleRouteFlamePower);
    wm.server->on("/off", setStoveOff);
    wm.server->on("/state", getStatus);
    wm.server->on("/get", getParams);
    //getStates();
}

void loop()
{
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
    if (currentMillis - previousMillis >= 2000)
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
            onboardLED.on();
        }
        // getStates();
        previousMillis = currentMillis;
    }
}