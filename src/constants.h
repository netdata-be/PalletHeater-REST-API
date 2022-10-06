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

const char * swaggerUI = "<!DOCTYPE html><html lang=\"en\"> <head> <meta charset=\"UTF-8\"> <title>Swagger UI</title> <link rel=\"stylesheet\" type=\"text/css\" href=\"https://cdnjs.cloudflare.com/ajax/libs/swagger-ui/4.14.2/swagger-ui.css\"/> <link rel=\"stylesheet\" type=\"text/css\" href=\"https://cdnjs.cloudflare.com/ajax/libs/swagger-ui/4.14.2/swagger-ui.css\"/> </head> <body> <div id=\"swagger-ui\"></div><script src=\"https://cdnjs.cloudflare.com/ajax/libs/swagger-ui/4.14.2/swagger-ui-bundle.js\" charset=\"UTF-8\"> </script> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/swagger-ui/4.14.2/swagger-ui-standalone-preset.js\" charset=\"UTF-8\"> </script> <script>window.onload=function(){window.ui=SwaggerUIBundle({url: \"api-specs.json\", dom_id: '#swagger-ui', deepLinking: true, presets: [ SwaggerUIBundle.presets.apis, SwaggerUIStandalonePreset], plugins: [ SwaggerUIBundle.plugins.DownloadUrl], layout: \"StandaloneLayout\"});}; </script> </body></html>";
const char * swaggerJSON = "{\"openapi\":\"3.0.1\",\"info\":{\"title\":\"Palletheater REST API\",\"description\":\"This API runs on a ESP32 an provides using the hardware serial UART2 an HTTP interface towards micronova pallet stoves.\",\"version\":\"0.3\"},\"paths\":{\"\/api\/flame\":{\"get\":{\"tags\":[\"Flame power\"],\"description\":\"Get the current flame power, can be 1 - 2 - 3 - 4\",\"responses\":{\"200\":{\"description\":\"Succesful fetech the current flame power\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"power\":{\"description\":\"The amount of pallets falling\",\"enum\":[1,2,3,4],\"type\":\"integer\"}}},\"examples\":{\"Power level 1\":{\"value\":\"{\\\"power\\\":1}\"},\"Power level 2\":{\"value\":\"{\\\"power\\\":2}\"},\"Power level 3\":{\"value\":\"{\\\"power\\\":3}\"},\"Power level 4\":{\"value\":\"{\\\"power\\\":4}\"}}}}}}},\"post\":{\"tags\":[\"Flame power\"],\"description\":\"Set the current flame power\",\"parameters\":[{\"name\":\"power\",\"in\":\"query\",\"required\":true,\"schema\":{\"type\":\"integer\",\"enum\":[1,2,3,4]},\"example\":\"1\"}],\"responses\":{\"200\":{\"description\":\"Succesfully set\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"result\":{\"type\":\"string\"},\"power\":{\"type\":\"integer\"}}},\"examples\":{\"Powerlevel 1\":{\"value\":\"{\\\"result\\\":\\\"success\\\",\\\"power\\\":1}\"},\"Powerlevel 2\":{\"value\":\"{\\\"result\\\":\\\"success\\\",\\\"power\\\":2}\"},\"Powerlevel 3\":{\"value\":\"{\\\"result\\\":\\\"success\\\",\\\"power\\\":3}\"},\"Powerlevel 4\":{\"value\":\"{\\\"result\\\":\\\"success\\\",\\\"power\\\":4}\"}}}}},\"400\":{\"description\":\"Wrong value, this means you have submitted the wrong int to the `power` argument.\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"result\":{\"type\":\"string\"},\"power\":{\"type\":\"integer\"}}},\"examples\":{\"Failed\":{\"value\":\"{\\\"result\\\":\\\"failed - power is out of range: Should be between 1 and 4\\\",\\\"power\\\":1}\"}}}}},\"500\":{\"description\":\"Can happen when the RS232 communication fails\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"result\":{\"type\":\"string\"},\"power\":{\"type\":\"integer\"}}},\"examples\":{\"Failed\":{\"value\":\"{\\\"result\\\":\\\"Failed - RS2232 error...\\\",\\\"power\\\":-1}\"}}}}}}}},\"\/api\/time\":{\"get\":{\"tags\":[\"Date and Time\"],\"description\":\"Get the stove it's internal time reference as well as NTP based time\",\"responses\":{\"200\":{\"description\":\"Successful\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"time\":{\"type\":\"object\",\"properties\":{\"stove\":{\"type\":\"string\"},\"delta\":{\"type\":\"integer\"},\"ntp\":{\"type\":\"string\"}}}}},\"examples\":{\"Example\":{\"value\":\"{\\\"time\\\":{\\\"stove\\\":\\\"2022-10-05 17:50:12\\\",\\\"ntp\\\":\\\"2022-10-05 17:50:38\\\",\\\"delta\\\":26}}\"}}}}}}},\"patch\":{\"tags\":[\"Date and Time\"],\"description\":\"Set the stove time to the NTP timesource\",\"responses\":{\"200\":{\"description\":\"Successfull synced the internal clock.\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"result\":{\"type\":\"string\"},\"time\":{\"type\":\"object\",\"properties\":{\"stove\":{\"type\":\"string\"},\"delta\":{\"type\":\"integer\"},\"ntp\":{\"type\":\"string\"}}}}},\"examples\":{\"0\":{\"value\":\"{\\\"result\\\":\\\"success Synced NTP time to stove\\\",\\\"time\\\":{\\\"stove\\\":\\\"2022-10-05 17:51:00\\\",\\\"ntp\\\":\\\"2022-10-05 17:51:04\\\",\\\"delta\\\":4}}\"}}}}}}}},\"\/api\/health\":{\"get\":{\"tags\":[\"Status\"],\"description\":\"Endpoint to test the serial interface it's health.\",\"responses\":{\"200\":{\"description\":\"When the serial interfaces works you get an HTTP 200\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"health\":{\"type\":\"string\"}}},\"examples\":{\"Healthy\":{\"value\":\"{\\\"health\\\":\\\"Healthy\\\"}\"}}}}},\"500\":{\"description\":\"In case the stove serial is not working you get an HTTP 500\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"health\":{\"type\":\"string\"}}},\"examples\":{\"Error\":{\"value\":\"{\\\"health\\\":\\\"Failed to talk to Stove\\\"}\"}}}}}}}},\"\/api\/state\":{\"get\":{\"tags\":[\"Status\"],\"description\":\"Fetch all relevant info in 1 output\",\"responses\":{\"200\":{\"description\":\"Success\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"RemoteControlLastSeen\":{\"type\":\"integer\"},\"lowPalletAlarm\":{\"type\":\"integer\"},\"temperatures\":{\"type\":\"object\",\"properties\":{\"setPoint\":{\"type\":\"integer\"},\"stove\":{\"type\":\"integer\"},\"remoteControl\":{\"type\":\"integer\"},\"fume\":{\"type\":\"integer\"},\"room\":{\"type\":\"number\"}}},\"stoveBeepEnabled\":{\"type\":\"boolean\"},\"stoveState\":{\"type\":\"object\",\"properties\":{\"poweredOn\":{\"type\":\"boolean\"},\"stoveState\":{\"type\":\"integer\"},\"state\":{\"type\":\"string\"}}},\"RoomFanSpeed\":{\"type\":\"integer\"},\"time\":{\"type\":\"object\",\"properties\":{\"stove\":{\"type\":\"string\"},\"delta\":{\"type\":\"integer\"},\"ntp\":{\"type\":\"string\"}}},\"flame\":{\"type\":\"object\",\"properties\":{\"power\":{\"type\":\"integer\"}}},\"numberOfStarts\":{\"type\":\"integer\"},\"fumeFanRPM\":{\"type\":\"integer\"},\"previousErrors\":{\"type\":\"object\",\"properties\":{\"MemoryPos_1\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"MemoryPos_3\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"MemoryPos_2\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"MemoryPos_5\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"MemoryPos_4\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}}}}}},\"examples\":{\"Example\":{\"value\":\"{\\\"stoveState\\\":{\\\"stoveState\\\":0,\\\"state\\\":\\\"Off\\\",\\\"poweredOn\\\":false},\\\"flame\\\":{\\\"power\\\":2},\\\"temperatures\\\":{\\\"room\\\":21.5,\\\"stove\\\":22,\\\"remoteControl\\\":20,\\\"setPoint\\\":29,\\\"fume\\\":22},\\\"RoomFanSpeed\\\":2,\\\"lowPalletAlarm\\\":0,\\\"stoveBeepEnabled\\\":true,\\\"numberOfStarts\\\":66,\\\"fumeFanRPM\\\":0,\\\"RemoteControlLastSeen\\\":-1,\\\"previousErrors\\\":{\\\"MemoryPos_1\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_2\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_3\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_4\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_5\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"}},\\\"time\\\":{\\\"ntp\\\":\\\"2022-10-05 17:50:06\\\",\\\"stove\\\":\\\"2022-10-05 17:49:35\\\",\\\"delta\\\":31}}\"}}}}}}}},\"\/api\/errors\":{\"get\":{\"tags\":[\"Previous Error log\"],\"description\":\"Get a list of all previous errors\",\"responses\":{\"200\":{\"description\":\"Success\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"MemoryPos_1\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"MemoryPos_3\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"MemoryPos_2\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"MemoryPos_5\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"MemoryPos_4\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}}}},\"examples\":{\"Example\":{\"value\":\"{\\\"MemoryPos_1\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_2\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_3\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_4\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_5\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"}}\"}}}}}}},\"post\":{\"tags\":[\"Previous Error log\"],\"description\":\"Set the error of a memory bank\",\"parameters\":[{\"name\":\"MemoryPos_1\",\"in\":\"query\",\"schema\":{\"type\":\"integer\",\"enum\":[0,1,2,4,8,16,32,64,128,129,130,132,136,255]}},{\"name\":\"MemoryPos_2\",\"in\":\"query\",\"schema\":{\"type\":\"integer\",\"enum\":[0,1,2,4,8,16,32,64,128,129,130,132,136,255]}},{\"name\":\"MemoryPos_3\",\"in\":\"query\",\"schema\":{\"type\":\"integer\",\"enum\":[0,1,2,4,8,16,32,64,128,129,130,132,136,255]}},{\"name\":\"MemoryPos_4\",\"in\":\"query\",\"schema\":{\"type\":\"integer\",\"enum\":[0,1,2,4,8,16,32,64,128,129,130,132,136,255]}},{\"name\":\"MemoryPos_5\",\"in\":\"query\",\"schema\":{\"type\":\"integer\",\"description\":\"The decimal error code\",\"enum\":[0,1,2,4,8,16,32,64,128,129,130,132,136,255]}}],\"responses\":{\"200\":{\"description\":\"Success and return the current errors\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"MemoryPos_1\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"MemoryPos_3\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"MemoryPos_2\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"action\":{\"type\":\"string\"},\"MemoryPos_5\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}},\"MemoryPos_4\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"error\":{\"type\":\"string\"},\"desc\":{\"type\":\"string\"}}}}},\"examples\":{\"Example\":{\"value\":\"{\\\"action\\\":\\\"MemoryPos_5 set\\\",\\\"MemoryPos_1\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_2\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_3\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_4\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"},\\\"MemoryPos_5\\\":{\\\"id\\\":0,\\\"error\\\":\\\"---\\\",\\\"desc\\\":\\\"No Error\\\"}}\"}}}}}}}},\"\/api\/fan\":{\"get\":{\"tags\":[\"Room ventilator\"],\"description\":\"Get the current room fan speed 0 means disabled.\",\"responses\":{\"200\":{\"description\":\"Success\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"speed\":{\"type\":\"integer\"}}},\"examples\":{\"Disabled\":{\"value\":\"{\\\"speed\\\":0}\"},\"Speed 1\":{\"value\":\"{\\\"speed\\\":1}\"},\"Speed 2\":{\"value\":\"{\\\"speed\\\":2}\"},\"Speed 3\":{\"value\":\"{\\\"speed\\\":3}\"}}}}}}},\"post\":{\"tags\":[\"Room ventilator\"],\"description\":\"Set the roomfan its speed, 0 means the fan is disabled.\",\"parameters\":[{\"name\":\"speed\",\"in\":\"query\",\"required\":true,\"schema\":{\"type\":\"integer\",\"enum\":[0,1,2,3]},\"example\":\"1\"}],\"responses\":{\"200\":{\"description\":\"Success\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"result\":{\"type\":\"string\"},\"speed\":{\"type\":\"integer\"}}},\"examples\":{\"Disabled\":{\"value\":\"{\\\"result\\\":\\\"success\\\",\\\"speed\\\":0}\"},\"Speed 1\":{\"value\":\"{\\\"result\\\":\\\"success\\\",\\\"speed\\\":1}\"},\"Speed 2\":{\"value\":\"{\\\"result\\\":\\\"success\\\",\\\"speed\\\":2}\"},\"Speed 3\":{\"value\":\"{\\\"result\\\":\\\"success\\\",\\\"speed\\\":3}\"}}}}}}}},\"\/api\/power\":{\"get\":{\"tags\":[\"Device power state\"],\"description\":\"Get the current powerstate of the stove\",\"responses\":{\"200\":{\"description\":\"success\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"poweredOn\":{\"type\":\"boolean\"},\"stoveState\":{\"type\":\"integer\"},\"state\":{\"type\":\"string\"}}},\"examples\":{\"Stove is off\":{\"value\":\"{\\\"stoveState\\\":0,\\\"state\\\":\\\"Off\\\",\\\"poweredOn\\\":false}\"},\"Starting\":{\"value\":\"{\\\"stoveState\\\":1,\\\"state\\\":\\\"Starting\\\",\\\"poweredOn\\\":true}\"},\"Pellet loading\":{\"value\":\"{\\\"stoveState\\\":2,\\\"state\\\":\\\"Pellet loading\\\",\\\"poweredOn\\\":true}\"},\"Ignition\":{\"value\":\"{\\\"stoveState\\\":3,\\\"state\\\":\\\"Ignition\\\",\\\"poweredOn\\\":true}\"},\"Working\":{\"value\":\"{\\\"stoveState\\\":4,\\\"state\\\":\\\"Working\\\",\\\"poweredOn\\\":true}\"},\"Cleaning\":{\"value\":\"{\\\"stoveState\\\":5,\\\"state\\\":\\\"Brazier cleaning\\\",\\\"poweredOn\\\":true}\"},\"Final cleaning\":{\"value\":\"{\\\"stoveState\\\":6,\\\"state\\\":\\\"Final cleaning\\\",\\\"poweredOn\\\":true}\"},\"Alarm state\":{\"value\":\"{\\\"stoveState\\\":8,\\\"state\\\":\\\"Alarm State\\\",\\\"poweredOn\\\":true}\"}}}}}}},\"post\":{\"tags\":[\"Device power state\"],\"description\":\"Set the powerstate on or off.\",\"parameters\":[{\"name\":\"state\",\"in\":\"query\",\"required\":true,\"schema\":{\"type\":\"string\",\"enum\":[\"off\",\"on\"]},\"example\":\"off\"}],\"responses\":{\"200\":{\"description\":\"Success\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"result\":{\"type\":\"string\"},\"poweredOn\":{\"type\":\"boolean\"},\"stoveState\":{\"type\":\"integer\"},\"state\":{\"type\":\"string\"}}},\"examples\":{\"Power on\":{\"value\":\"{\\\"result\\\":\\\"Powered on sended\\\",\\\"stoveState\\\":1,\\\"state\\\":\\\"Starting\\\",\\\"poweredOn\\\":true}\"},\"Power off\":{\"value\":\"{\\\"result\\\":\\\"Powered off sended - 0\\\",\\\"stoveState\\\":0,\\\"state\\\":\\\"Off\\\",\\\"poweredOn\\\":false}\"}}}}},\"202\":{\"description\":\"Success but no state change happend, because it is already in the desired state.\",\"content\":{\"application\/json\":{\"schema\":{\"type\":\"object\",\"properties\":{\"result\":{\"type\":\"string\"},\"poweredOn\":{\"type\":\"boolean\"},\"stoveState\":{\"type\":\"integer\"},\"state\":{\"type\":\"string\"}}},\"examples\":{\"Already on\":{\"value\":\"{\\\"result\\\":\\\"Stove already on\\\",\\\"stoveState\\\":1,\\\"state\\\":\\\"Starting\\\",\\\"poweredOn\\\":true}\"},\"Already off\":{\"value\":\"{\\\"result\\\":\\\"Stove already off\\\",\\\"stoveState\\\":0,\\\"state\\\":\\\"Off\\\",\\\"poweredOn\\\":false}\"}}}}}}}}}}";

EasyLed led(LED, EasyLed::ActiveLevel::High);

EasyLed onboardLED(ONBOARD_LED, EasyLed::ActiveLevel::High);

WiFiManager wm;
WiFiManagerParameter custom_hydro_mode("hydro", "hydro_mode", "0", 2);

WiFiManagerParameter custom_ntp_server("ntp_server", "NTP server", "nl.pool.ntp.org", 20);
WiFiManagerParameter custom_TimeZone("ntp_server", "NTP server", "CET-1CEST,M3.5.0/2,M10.5.0/3", 20);


unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

String hydro_mode;
int int_hydro_mode;

// Init the Serial Stove with Hardware UART 2
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

