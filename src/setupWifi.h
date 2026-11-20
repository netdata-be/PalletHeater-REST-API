#include <constants.h>
#include <Preferences.h>
#include <ESPmDNS.h>

// mDNS: <hostname>.local with the web app (_http, _https) and the OTA service (_arduino,
// for the Arduino IDE / PlatformIO network port). The one place that starts it: at boot,
// after the config portal and after a network change.
void mdnsStart(const char* hostname)
{
    MDNS.end();
    if (!MDNS.begin(hostname)) { LOGE("wifi", "mDNS could not be started for %s.local", hostname); return; }
    // DNS-SD: a readable name in service browsers (Finder, Home Assistant, Bonjour tools) and
    // TXT records that tell what it is without opening it
    String instance = String("OpenPelletBridge (") + BUILD_STOVE_MODEL + ")";
    MDNS.setInstanceName(instance.c_str());
    for (const char* svc : { "http", "https" }) {
        MDNS.addService(svc, "tcp", strcmp(svc, "http") == 0 ? 80 : 443);
        MDNS.addServiceTxt(svc, "tcp", "path", "/");
        MDNS.addServiceTxt(svc, "tcp", "api", "/api");
        MDNS.addServiceTxt(svc, "tcp", "version", BUILD_VERSION);
        MDNS.addServiceTxt(svc, "tcp", "model", BUILD_STOVE_MODEL);
    }
    MDNS.enableArduino(3232, true);   // password = API key
    LOGI("wifi", "mDNS: %s.local, DNS-SD '%s' (_http, _https, _arduino)", hostname, instance.c_str());
}

void saveConfigCallback() // Save params to SPIFFS
{
    if (!spiffsMounted) {
        LOGE("sys", "SPIFFS not mounted: config portal settings not saved");
        return;
    }
    LOGI("sys", "config portal: saving hydro mode '%s'", custom_hydro_mode.getValue());
    File file = SPIFFS.open("/config.txt", "w");
    if (!file) {
        LOGE("sys", "failed to open config file for writing");
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
        String hn = prefs.getString("hostname", "openpelletbridge");
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
                    LOGI("wifi", "static IP: %s gw:%s sn:%s dns:%s",
                        ip.toString().c_str(), gw.toString().c_str(),
                        sn.toString().c_str(), dns.toString().c_str());
                }
            }
        }
    }
    wm.setTitle("OpenPelletBridge REST API");
    wm.setWiFiAutoReconnect(true);
    std::vector<const char *> menu = {"wifi","wifinoscan","info","param","sep", "custom","sep","restart","erase"};
    wm.setMenu(menu);
    //wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(30);
    if (wm.autoConnect("Pellet heater controller"))
    {
        wifiConnected = true;
        LOGI("wifi", "connected to '%s', IP %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
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
        LOGE("wifi", "no WiFi connection within 30 s: config portal running (access point 'Pellet heater controller')");
    }
}
