#include <constants.h>

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