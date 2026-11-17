#include <constants.h>
#include <Preferences.h>

void saveConfigCallback() // Save params to SPIFFS
{
    if (!spiffsMounted) {
        Serial.println("SPIFFS not mounted, skipping config save");
        return;
    }
    Serial.println("Get Params:");
    Serial.println(custom_hydro_mode.getValue());
    Serial.println(F("Initializing FS..."));
    File file = SPIFFS.open("/config.txt", "w");
    if (!file) {
        log_e("Failed to open config file for writing");
        return;
    }
    file.println(custom_hydro_mode.getValue());
    file.close();
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

    {
        Preferences prefs;
        prefs.begin("net", true);
        String hn = prefs.getString("hostname", "palletheater");
        String nc = prefs.getString("netcfg", "dhcp");
        prefs.end();
        wm.setHostname(hn.c_str());
        if (nc.startsWith("static,")) {
            String rest = nc.substring(7);
            int c1 = rest.indexOf(',');
            int c2 = rest.indexOf(',', c1 + 1);
            int c3 = rest.indexOf(',', c2 + 1);
            if (c1 > 0 && c2 > c1 && c3 > c2) {
                IPAddress ip, gw, sn, dns;
                if (ip.fromString(rest.substring(0, c1)) &&
                    gw.fromString(rest.substring(c1 + 1, c2)) &&
                    sn.fromString(rest.substring(c2 + 1, c3)) &&
                    dns.fromString(rest.substring(c3 + 1))) {
                    wm.setSTAStaticIPConfig(ip, gw, sn, dns);
                    Serial.printf("[WiFi] static IP: %s gw:%s sn:%s dns:%s\n",
                        ip.toString().c_str(), gw.toString().c_str(),
                        sn.toString().c_str(), dns.toString().c_str());
                }
            }
        }
    }
    wm.setTitle("Palletheater REST API");
    wm.setWiFiAutoReconnect(true);
    std::vector<const char *> menu = {"wifi","wifinoscan","info","param","sep", "custom","sep","restart","erase"};
    wm.setMenu(menu);
    //wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(30);
    if (wm.autoConnect("Pellet heater controller"))
    {
        wifiConnected = true;
        Serial.println("connected to Wifi");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        onboardLED.on();
        // Do not start WiFiManager's web portal here. On this firmware/library
        // combination it aborts during setupConfigPortal() after STA connect.
        // The application registers its own REST routes and starts this plain
        // server after route registration in setup().
        wm.server.reset(new WebServer(80));
        esp_task_wdt_init(WDT_TIMEOUT, true);
        esp_task_wdt_add(NULL);
    }
    else
    {
        wifiConnected = false;
        Serial.println("Configportal running");
    }
}
