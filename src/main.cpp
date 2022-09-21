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
    Serial.print("T. amb. ");
    Serial.println(ambTemp);
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
    Serial.print("T. amb. stove : ");
    Serial.println(ambTempStove);
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
    Serial.print("T. amb. remote control : ");
    Serial.println(ambTempRemoteControl);
}

int numberOfStarts()
{
    return stove.read(eeprom, 0xee);
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
    Serial.print("T. setpoint. ");
    Serial.println(setPointTemp);
}

void getFumeTemp() // Get flue gas temperature
{
    fumesTemp = stove.read(ram, fumesTempAddr);
    Serial.printf("T. fumes %d\n", fumesTemp);
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
    Serial.printf("Fan speed: %d\n", roomFanSpeed);
}

void getFlamePower() // Get the flame power (0, 1, 2, 3, 4, 5)
{
    flamePower = stove.read(eeprom, 0x7f);
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
        stove.write(eeprom, 0x81, speed); // Write to EEPROM, this triggers the fan adjustment
        stove.write(ram, 0x19, speed);    // Write to RAM, as a status update ???
    }
    else
    {
        log_e("Speed not in range, it should be between 0-3\n");
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

void setFlamePower(int power)
{
    if ((power >= 1) & (power <= 4))
    {
        stove.write(eeprom, 0x7f, power);
    }
    else
    {
        log_e("Flamepower not in range, it should be between 0-3\n");
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

void handleRouteSyncTime()
{
    StaticJsonDocument<350> json;
    char time_output[30];

    json.clear();

    JsonObject time = json.createNestedObject("time");

    strftime(time_output, 30, "%F %T", localtime(&now));
    time["ntp"] = time_output;
    setStoveDateTime();

    json["success"] = true;

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void handleRouteNtpTime()
{
    StaticJsonDocument<350> json;
    char time_output[30];

    json.clear();

    getNTPtime(2);
    JsonObject time = json.createNestedObject("time");

    strftime(time_output, 30, "%F %T", localtime(&now));
    time["ntp"] = time_output;
    json["success"] = true;

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void handleRouteStoveTime()
{
    StaticJsonDocument<350> json;
    char time_output[30];

    json.clear();

    JsonObject time = json.createNestedObject("time");
    getStoveDateTime();
    strftime(time_output, 30, "%F %T", localtime(&stoveTime));
    time["stove"] = time_output;
    json["success"] = true;

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void handleRouteTime()
{
    StaticJsonDocument<350> json;
    char time_output[30];

    json.clear();

    JsonObject time = json.createNestedObject("time");
    getStoveDateTime();
    strftime(time_output, 30, "%F %T", localtime(&stoveTime));
    time["stove"] = time_output;
    getNTPtime(2);
    strftime(time_output, 30, "%F %T", localtime(&now));
    time["ntp"] = time_output;
    json["success"] = true;

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
    if (stoveState == 0)
    {
        if (stove.write(ram, stoveStateAddr, 1))
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

    json["result"] = message;

    delay(50);
    getStoveState();
    JsonObject obj_stove = json.createNestedObject("stoveState");
    obj_stove["stoveState"] = stoveState;
    obj_stove["state"] = StoveStateStr;
    obj_stove["poweredOn"] = StoveIsOn;

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
    delay(50);
    getStoveState();
    JsonObject obj_stove = json.createNestedObject("stoveState");
    obj_stove["stoveState"] = stoveState;
    obj_stove["state"] = StoveStateStr;
    obj_stove["poweredOn"] = StoveIsOn;

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

    JsonObject time = json.createNestedObject("time");

    getNTPtime(2);
    char time_output[30];

    strftime(time_output, 30, "%F %T", localtime(&now));
    time["ntp"] = time_output;

    getStoveDateTime();
    strftime(time_output, 30, "%F %T", localtime(&stoveTime));
    time["stove"] = time_output;

    time["delta"] = now - stoveTime;

    serializeJson(json, buffer);
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

    wm.server->on("/time/sync", handleRouteSyncTime);
    wm.server->on("/time/ntp", handleRouteNtpTime);
    wm.server->on("/time/stove", handleRouteStoveTime);
    wm.server->on("/time", handleRouteTime);

    wm.server->on("/error", handleRouteSetError);

    wm.server->on("/cq", handleRouteCQ);
    wm.server->on("/flame", handleRouteFlamePower);
    wm.server->on("/off", setStoveOff);
    wm.server->on("/state", getStatus);
    wm.server->on("/get", getParams);

    wm.server->on("/stove/flame", handleRouteFlamePower);
    wm.server->on("/stove/fan", handleRouteFanspeed);

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