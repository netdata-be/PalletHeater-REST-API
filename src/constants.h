#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <WiFiManager.h>
#include <WiFiUdp.h>
#include "EasyLed.h"
#include <EasyButton.h>

#ifdef ESP32
#include <FS.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>
#include "esp_log.h"
#include <esp_task_wdt.h>
#include <HardwareSerial.h>
#define SERIAL_MODE SERIAL_8N2 // 8 data bits, parity none, 2 stop bits
#define RESET_PIN 13
#define RX_PIN 16
#define TX_PIN 17
#define ENABLE_RX 18
#define ONBOARD_LED 2
#define LED 14
#define WDT_TIMEOUT 3
#endif

WiFiUDP udp;
WiFiClient espClient;
EasyButton resetButton(RESET_PIN);

EasyLed led(LED, EasyLed::ActiveLevel::High);

EasyLed onboardLED(ONBOARD_LED, EasyLed::ActiveLevel::High);

HardwareSerial StoveSerial(2);

WiFiManager wm;
WiFiManagerParameter custom_hydro_mode("hydro", "hydro_mode", "0", 2);

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

String hydro_mode;
int int_hydro_mode;

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
float setPointTemp = 0;
float waterPres = 0;

char resp[2];

#endif
