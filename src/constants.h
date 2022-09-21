#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <WiFiManager.h>
#include <WiFiUdp.h>
#include "EasyLed.h"
#include <EasyButton.h>
#include "time.h"
#include "Stove.h"


#ifdef ESP32
#include <FS.h>
#include "ESPTelnet.h" 
#include <SPIFFS.h>
#include "esp_log.h"
#include <esp_task_wdt.h>
#define RESET_PIN 13
#define ENABLE_RX 18
#define ONBOARD_LED 2
#define LED 14
#define WDT_TIMEOUT 3
#endif

const char* ntpServer = "nl.pool.ntp.org";
const char * defaultTimezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";

tm timeinfo;
struct tm tm = {0};
time_t now;
time_t stoveTime;
long unsigned lastNTPtime;
unsigned long lastEntryTime;

WiFiUDP udp;
WiFiClient espClient;
ESPTelnet telnet;
EasyButton resetButton(RESET_PIN);



EasyLed led(LED, EasyLed::ActiveLevel::High);

EasyLed onboardLED(ONBOARD_LED, EasyLed::ActiveLevel::High);

WiFiManager wm;
WiFiManagerParameter custom_hydro_mode("hydro", "hydro_mode", "0", 2);

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

String hydro_mode;
int int_hydro_mode;

Stove stove(2);

char buffer[9000];

bool StoveIsOn = false;
String StoveStateStr = "Unknown";

unsigned long firstv;
unsigned long secondv;

unsigned long StoveStateUpdatedAt = 0;
unsigned long ambTempUpdatedAt = 0;
unsigned long flamePowerpUpdatedAt = 0;
unsigned long fumesTempUpdatedAt = 0;
unsigned long getStatesUpdatedAt = 0;

int buttonState;

int deepSleep = 0;
long previousMillis;

#define stoveStateAddr 0x21
#define ram 0x00
#define eeprom 0x20
#define ambTempAddr 0x01
#define TempSetpointAddr 0x7d
#define fumesTempAddr 0x3E
#define flamePowerAddr 0x34
#define roomFanAddr 0x19
#define waterTempAddr 0x03
#define waterSetAddr 0x36
#define waterPresAddr 0x3C




uint8_t waterTemp = 0;
int stoveState = 0;
uint8_t fumesTemp = 0;
int flamePower = 0;
int roomFanSpeed = 0;
float ambTemp = 0;
float ambTempStove = 0;
float ambTempRemoteControl = 0;
float setPointTemp = 0;
float waterPres = 0;

char resp[2];

char storedRam[256];
char storedEeprom[256];

enum stoveStateCode {
    UNKNOWN = -1,
    OFF = 0,
    STARTING = 1,
    PALLET_LOADING = 2,
    IGNITION = 3,
    WORKING = 4,
    BRAZIER_CLEANING = 5,
    FINAL_CLEANING = 6,
    STANDBY = 7,
    ALARM = 8,
    IGNITION_FAILURE = 9,
    ALARM_STATE = 10
};

#endif

