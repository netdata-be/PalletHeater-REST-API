#include "EasyLed.h"
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <EasyButton.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>

#ifdef ESP32
#include <FS.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>
#include <esp_task_wdt.h>
HardwareSerial StoveSerial(2);
#define SERIAL_MODE SERIAL_8N2 // 8 data bits, parity none, 2 stop bits
#define RESET_PIN 14
#define RX_PIN 16
#define TX_PIN 17
#define ENABLE_RX 18
#define ONBOARD_LED 5
#define LED 2
#define WDT_TIMEOUT 3
#endif
#define FORMAT_SPIFFS_IF_FAILED true

EasyButton resetButton(RESET_PIN);
// WebServer server(80);
EasyLed onboardLED(ONBOARD_LED, EasyLed::ActiveLevel::High);

EasyLed led(LED, EasyLed::ActiveLevel::High);

WiFiClient espClient;
// PubSubClient client(espClient);
WiFiManager wm;
WiFiManagerParameter custom_hydro_mode("hydro", "hydro_mode", "0", 2);

int deepSleep = 0;
long previousMillis;

WiFiUDP udp;

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

String hydro_mode;
int int_hydro_mode;

char buffer[250];

unsigned long firstv;
unsigned long secondv;

unsigned long StoveStateUpdatedAt = 0;
unsigned long ambTempUpdatedAt = 0;
unsigned long flamePowerpUpdatedAt = 0;
unsigned long fumesTempUpdatedAt = 0;
unsigned long getStatesUpdatedAt = 0;

int buttonState;

bool StoveIsOn = false;
String StoveStateStr = "Unknown";

// 0 - OFF, 1 - Starting, 2 - Pellet loading, 3 - Ignition, 4 - Work, 5 - Brazier cleaning, 6 - Final cleaning, 7 - Standby, 8 - Pellet missing alarm, 9 - Ignition failure alarm, 10 - Alarms (to be investigated)

// Checksum: Code+Address+Value on hexadecimal calculator

const char stoveOn[4] = {0x80, 0x21, 0x01, 0xA2};
const char stoveOff[4] = {0x80, 0x21, 0x06, 0xA7};
const char forceOff[4] = {0x80, 0x21, 0x00, 0xA1};

#define stoveStateAddr 0x21
#define ambTempAddr 0x01
#define fumesTempAddr 0x3E
#define flamePowerAddr 0x34
#define waterTempAddr 0x03
//#define waterSetAddr 0x36
#define waterPresAddr 0x3C
uint8_t waterTemp = 0;
int stoveState = 0;
uint8_t fumesTemp = 0;
int flamePower = -1;
float ambTemp = 0;
float waterPres = -1;
char stoveRxData[2]; // When the heater is sending data, it sends two bytes: a checksum and the value

void checkStoveReply() // Works only when request is RAM
{
    uint8_t rxCount = 0;
    stoveRxData[0] = 0x00;
    stoveRxData[1] = 0x00;
    while (StoveSerial.available()) // It has to be exactly 2 bytes, otherwise it's an error
    {
        stoveRxData[rxCount] = StoveSerial.read();
        rxCount++;
    }
    digitalWrite(ENABLE_RX, HIGH);
    if (rxCount == 2)
    {
        byte val = stoveRxData[1];
        byte checksum = stoveRxData[0];
        byte param = checksum - val;
        Serial.printf("Param=%01x value=%01x ", param, val);
        switch (param)
        {
        case stoveStateAddr:
            StoveStateUpdatedAt = millis();
            stoveState = val;
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
                StoveStateStr = "Pellet loading";
                StoveIsOn = true;
                break;
            case 3:
                StoveStateStr = "Ignition";
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
                StoveStateStr = "Final cleaning";
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
            }
            Serial.printf("Stove %s\n", StoveIsOn ? "ON" : "OFF");
            break;
        case ambTempAddr:
            ambTempUpdatedAt = millis();
            ambTemp = (float)val / 2;
            Serial.print("T. amb. ");
            Serial.println(ambTemp);
            break;
        case fumesTempAddr:
            fumesTempUpdatedAt = millis();
            fumesTemp = val;
            Serial.printf("T. fumes %d\n", fumesTemp);
            break;
        case flamePowerAddr:
            flamePowerpUpdatedAt = millis();
            if (stoveState < 6)
            {
                if (stoveState > 0)
                {
                    flamePower = map(val, 0, 16, 10, 100);
                }
            }
            else
            {
                flamePower = 0;
            }
            Serial.printf("Fire %d\n", flamePower);
            break;
        case waterTempAddr:
            waterTemp = val;
            Serial.printf("T. water %d\n", waterTemp);
            break;
        /*case waterSetAddr:
            waterSet = val;
            client.publish(char_waterset_topic, String(waterSet).c_str(), true);
            Serial.printf("T. water set %d\n", waterSet);
            break;*/
        case waterPresAddr:
            waterPres = (float)val / 10;
            Serial.print("Pressure ");
            Serial.println(waterPres);
            break;
        }
    }
    else
    {
        Serial.print("rxCount was not 2....\n");
    }
}

void getStoveState() // Get detailed stove state
{
    const byte readByte = 0x00;
    led.on();
    StoveSerial.write(readByte);
    led.off();
    delay(1);
    led.on();
    StoveSerial.write(stoveStateAddr);
    led.off();
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}

void getAmbTemp() // Get room temperature
{
    const byte readByte = 0x00;
    led.on();
    StoveSerial.write(readByte);
    led.off();
    delay(1);
    led.on();
    StoveSerial.write(ambTempAddr);
    led.off();
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}

void getFumeTemp() // Get flue gas temperature
{
    const byte readByte = 0x00;
    led.on();
    StoveSerial.write(readByte);
    led.off();
    delay(1);
    led.on();
    StoveSerial.write(fumesTempAddr);
    led.off();
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}

void getFlamePower() // Get the flame power (0, 1, 2, 3, 4, 5)
{
    const byte readByte = 0x00;
    led.on();
    StoveSerial.write(readByte);
    led.off();
    delay(1);
    led.on();
    StoveSerial.write(flamePowerAddr);
    led.off();
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}

void getWaterTemp() // Get the temperature of the water (if you have an hydro heater)
{
    const byte readByte = 0x00;
    StoveSerial.write(readByte);
    delay(1);
    StoveSerial.write(waterTempAddr);
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}

void getWaterPres() // Get the temperature of the water (if you have an hydro heater)
{
    const byte readByte = 0x00;
    StoveSerial.write(readByte);
    delay(1);
    StoveSerial.write(waterPresAddr);
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}

/*void getWaterSet() //Get the temperature of the water (if you have an hydro heater)
{
    const byte readByte = 0x00;
    StoveSerial.write(readByte);
    delay(1);
    StoveSerial.write(waterSetAddr);
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}*/

void getStates() // Calls all the get…() functions
{
    getStatesUpdatedAt = millis();
    getStoveState();
    delay(100);
    getAmbTemp();
    delay(100);
    getFumeTemp();
    delay(100);
    getFlamePower();
    if (int_hydro_mode == 1)
    {
        delay(100);
        getWaterTemp();
        delay(100);
        /*getWaterSet();
        delay(100);*/
        getWaterPres();
    }
}

void saveConfigCallback() // Save params to SPIFFS
{
    Serial.println("Get Params:");
    Serial.println(custom_hydro_mode.getValue());
    Serial.println(F("Initializing FS..."));
    File file = SPIFFS.open("/config.txt", "a+"); // We open a file to save value on SPIFFS
    file.println(custom_hydro_mode.getValue());   // 6th line: Hydro mode
}

void setup_wifi() // Setup WiFiManager and connect to WiFi
{
    wm.setConfigPortalBlocking(false);
    WiFi.mode(WIFI_STA);
    wm.setDarkMode(true);
    wm.addParameter(&custom_hydro_mode);
    wm.setSaveConfigCallback(saveConfigCallback); // Saves the settings in SPIFFS
    wm.setConnectTimeout(30);
    if (wm.autoConnect("Pellet heater controller"))
    {
        Serial.println("connected to Wifi");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        onboardLED.on();
        wm.startConfigPortal();
        esp_task_wdt_init(WDT_TIMEOUT, true);
        esp_task_wdt_add(NULL);
    }
    else
    {
        Serial.println("Configportal running");
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
    Serial.println("Button has been pressed");
}

void setStoveOn()
{
    Serial.print("stoveOn: called");
    for (int i = 0; i < 4; i++)
    {
        if (stoveState > 5)
        {
            if (i == 0)
            {
                Serial.print("Sending Stove on to serial\n");
            }
            led.on();
            StoveSerial.write(stoveOn[i]);
            led.off();
            delay(1);
        }
        else if (stoveState == 0)
        {
            if (i == 0)
            {
                Serial.print("Sending Stove on to serial...\n");
            }
            led.on();
            StoveSerial.write(stoveOn[i]);
            led.off();
            delay(1);
        }
        else
        {
            if (i == 0)
            {
                Serial.print("Stove already on\n");
            }
        }
    }
    delay(1000);
    getStates();

    StaticJsonDocument<250> json;
    json.clear();

    json["State_i"] = stoveState;
    json["State_s"] = StoveStateStr;
    json["State_b"] = StoveIsOn;
    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void setStoveOff()
{
    Serial.print("stoveOff: called");
    for (int i = 0; i < 4; i++)
    {
        if (stoveState < 6)
        {
            if (stoveState > 0)
            {
                if (i == 0)
                {
                    Serial.print("Sending Stove Off to serial...\n");
                }
                led.on();
                StoveSerial.write(stoveOff[i]);
                led.off();
                delay(1);
            }
            else
            {
                if (i == 0)
                {
                    Serial.print("Stove Already Off\n");
                }
            }
        }
    }
    delay(1000);
    getStates();

    StaticJsonDocument<250> json;
    json.clear();

    json["State_i"] = stoveState;
    json["State_s"] = StoveStateStr;
    json["State_b"] = StoveIsOn;
    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void getStatus()
{
    unsigned long currentMillis = millis();

    getStates();
    StaticJsonDocument<250> json;
    json.clear();

    JsonObject obj_stove = json["stoveState"].createNestedObject();

    obj_stove["stoveState"] = stoveState;
    obj_stove["StoveStateStr"] = StoveStateStr;
    obj_stove["StoveIsOn"] = StoveIsOn;

    if (StoveStateUpdatedAt == 0)
    {
        obj_stove["lastUpdated"] = "-1";
    }
    else
    {
        obj_stove["lastUpdated"] = (currentMillis - StoveStateUpdatedAt) / 1000;
    }

    if (int_hydro_mode == 1)
    {
        JsonObject hydro = json["hydro"].createNestedObject();

        hydro["Pressure"] = waterPres;
        hydro["Temp"] = waterTemp;
    }

    JsonObject obj_flame = json["flame"].createNestedObject();
    obj_flame["power"] = flamePower;
    if (flamePowerpUpdatedAt == 0)
    {
        obj_flame["lastUpdated"] = "-1";
    }
    else
    {
        obj_flame["lastUpdated"] = (currentMillis - flamePowerpUpdatedAt) / 1000;
    }

    json["ambTemp"] = ambTemp;
    if (ambTempUpdatedAt == 0)
    {
        json["ambTemp_updated"] = "-1";
    }
    else
    {
        json["ambTemp_updated"] = (currentMillis - ambTempUpdatedAt) / 1000;
    }

    if (getStatesUpdatedAt == 0)
    {
        json["lastUpdated"] = "-1";
    }
    else
    {
        json["lastUpdated"] = (currentMillis - getStatesUpdatedAt) / 1000;
    }

    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}

void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println();
    if ((char)payload[1] == 'N')
    {
        for (int i = 0; i < 4; i++)
        {
            if (stoveState > 5)
            {
                StoveSerial.write(stoveOn[i]);
                delay(1);
            }
            else if (stoveState == 0)
            {
                StoveSerial.write(stoveOn[i]);
                delay(1);
            }
        }
        // client.publish(char_onoff_topic, "ON", true);
        delay(1000);
        /* getStates(); */
    }
    else if ((char)payload[1] == 'F')
    {
        for (int i = 0; i < 4; i++)
        {
            if (stoveState < 6)
            {
                if (stoveState > 0)
                {
                    StoveSerial.write(stoveOff[i]);
                    delay(1);
                }
            }
        }
        // client.publish(char_onoff_topic, "OFF", true);
        delay(1000);
        /*  getStates(); */
    }
    else if ((char)payload[0] == '0')
    {
        for (int i = 0; i < 4; i++)
        {
            if (stoveState < 6)
            {
                if (stoveState > 0)
                {
                    StoveSerial.write(stoveOff[i]);
                    delay(1);
                }
            }
        }
        // client.publish(char_onoff_topic, "OFF", true);
        delay(1000);
        getStates();
    }
    else if ((char)payload[0] == '1')
    {
        for (int i = 0; i < 4; i++)
        {
            if (stoveState > 5)
            {
                StoveSerial.write(stoveOn[i]);
                delay(1);
            }
            else if (stoveState == 0)
            {
                StoveSerial.write(stoveOn[i]);
                delay(1);
            }
            // client.publish(char_onoff_topic, "ON", true);
            delay(1000);
            getStates();
        }
    }
    else if ((char)payload[0] == 'f')
    {
        if ((char)payload[1] == 'o')
        {
            for (int i = 0; i < 4; i++)
            {
                StoveSerial.write(forceOff[i]);
                delay(1);
            }
            // client.publish(char_onoff_topic, "OFF", true);
            delay(1000);
            getStates();
        }
    }
    else if ((char)payload[0] == 'S')
    {
        deepSleep = 1;
    }
    else if ((char)payload[0] == 'W')
    {
        deepSleep = 0;
    }
    else if ((char)payload[2] == 's')
    {
        fullReset();
    }
}

void setup()
{
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

    ArduinoOTA.setHostname("Pelletheater");
    ArduinoOTA.setPort(3232);
    ArduinoOTA.begin();

    resetButton.begin();
    resetButton.onPressedFor(5000, fullReset);
    resetButton.onPressed(buttonPressed);

    Serial.println("Ready");

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    onboardLED.on();

    wm.server->on("/on", setStoveOn);
    wm.server->on("/off", setStoveOff);
    wm.server->on("/state", getStatus);
}

void loop()
{
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
            onboardLED.on();
        }
        getStates();
        previousMillis = currentMillis;
    }
}