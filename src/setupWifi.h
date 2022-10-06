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
    wm.addParameter(&custom_TimeZone);
    wm.addParameter(&custom_ntp_server);
    wm.setSaveConfigCallback(saveConfigCallback); // Saves the settings in SPIFFS

    // set custom html menu content, inside menu item custom
    const char* menuhtml = "<h3>Swagger UI Docs</h3><br/><form action='/api' method='get'><button>API docs</button></form><br/>\n";
    wm.setCustomMenuHTML(menuhtml);

    wm.setHostname("palletheater");
    wm.setTitle("Palletheater REST API");
    wm.setWiFiAutoReconnect(true);
    std::vector<const char *> menu = {"wifi","wifinoscan","info","param","sep", "custom","sep","restart","erase"};
    wm.setMenu(menu);
    //wm.setConfigPortalTimeout(180);
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