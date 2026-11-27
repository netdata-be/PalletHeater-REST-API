#include <constants.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>

// Several access points with the same name (UniFi, mesh): the Arduino default is a fast scan
// (WIFI_FAST_SCAN): the first access point found with the name, channel by channel, not the
// strongest - and the ESP32 stays there. All channels, then the strongest signal. The driver
// keeps these in its own stored configuration (NVS), which WiFi.begin() without arguments
// (WiFiManager, the reconnect in loop()) uses: changed there too, and the BSSID lock of a
// WiFi.begin(ssid, pass, channel, bssid) cleared.
void wifiStaConfigFix()
{
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);      // WiFi.begin(ssid, pass) after this
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    wifi_config_t c;
    if (esp_wifi_get_config(WIFI_IF_STA, &c) != ESP_OK) return;
    if (c.sta.scan_method == WIFI_ALL_CHANNEL_SCAN && c.sta.sort_method == WIFI_CONNECT_AP_BY_SIGNAL && !c.sta.bssid_set) return;
    c.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    c.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    c.sta.bssid_set = 0;
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &c);
    if (e == ESP_OK) LOGI("wifi", "connect: scan all channels, strongest access point (was a fast scan)");
    else LOGW("wifi", "scan method not stored: %s", esp_err_to_name(e));
}

// Country: the channels and transmit power allowed here (Belgium / EU: 1 - 13). With 802.11d
// the ESP32 follows the country the access points announce.
#ifndef OPB_WIFI_COUNTRY
#define OPB_WIFI_COUNTRY "BE"
#endif
static void wifiCountrySet()
{
    esp_err_t e = esp_wifi_set_country_code(OPB_WIFI_COUNTRY, true);
    if (e != ESP_OK) LOGW("wifi", "country %s not set: %s", OPB_WIFI_COUNTRY, esp_err_to_name(e));
}

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
    wifiCountrySet();
    wifiStaConfigFix();   // before WiFiManager connects with the stored network
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
