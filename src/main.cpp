#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ota.h>
#include <constants.h>
#include <setupWifi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <bleUart.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <auth.h>
#include <httpUpdate.h>
#include <metrics.h>

#include <helperFunctions.h>
#include "Stove.h"
#include <telnetMenu.h>
#include <keyWatch.h>
#include <Preferences.h>
#include <esp_bt.h>

#define FORMAT_SPIFFS_IF_FAILED true


#include "stoveCache.h"

#include "stoveControl.h"

#include "registers.h"

#include "stoveClock.h"

#include "routesBasic.h"

#include "routesTimers.h"

#include "routesControl.h"

#include "status.h"

void setupTelnet()
{
    // Menu actions read/write the stove, so hold the bus lock while they run
    telnet.onConnect([](String ip) { StoveBusLock lock; telnetOnConnect(ip); });
    telnet.onInputReceived([](String str) { StoveBusLock lock; telnetHandleInput(str); });

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

#include "history.h"

#include "events.h"

#include "routesDevice.h"

#include "mqtt.h"

#include "wifiInfo.h"

#include "fwUpdate.h"

// Serialise all stove bus access: HTTP handlers run on the loop task while the
// sensor task polls the stove from the other core. Without the lock both talk
// on the bus at the same time and reads return garbage.
static std::function<void()> withStoveLock(void (*fn)())
{
    return [fn]() {
        if (!stoveMutex) { fn(); return; }
        if (xSemaphoreTakeRecursive(stoveMutex, pdMS_TO_TICKS(5000)) != pdTRUE)
        {
            wm.server->send(503, "application/json", "{\"error\":\"Stove bus busy, try again\"}");
            return;
        }
        fn();
        xSemaphoreGiveRecursive(stoveMutex);
    };
}

void setup()
{
    esp_log_level_set("*", ESP_LOG_WARN);
    pinMode(ENABLE_RX, OUTPUT);
    digitalWrite(ENABLE_RX, HIGH); // The led of the optocoupler is off

    Serial.begin(115200);
    
    spiffsMounted = SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED); // Mount SPIFFS
    if (spiffsMounted)
    {
        Serial.println("SPIFFS system mounted with success");
    }
    else
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
    }
    setup_wifi();
    {
        // mDNS name = the configured hostname (IP settings), default openpelletbridge
        Preferences prefs;
        prefs.begin("net", true);
        String hn = prefs.getString("hostname", "openpelletbridge");
        prefs.end();
        MDNS.begin(hn.c_str());
    }
    MDNS.addService("http", "tcp", 80);
    authInit();
    {
        Preferences prefs;
        prefs.begin("ble", true);
        bleEnabled = prefs.getBool("enabled", true);
        prefs.end();
        prefs.begin("telnet", true);
        telnetEnabled = prefs.getBool("enabled", true);
        prefs.end();
    }
    if (bleEnabled) {
        setupBLE();
    } else {
        // BLE skipped completely: release the BT controller memory and let WiFi
        // use the radio without the BLE/WiFi coexistence modem sleep.
        esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
        WiFi.setSleep(false);
        Serial.println("Bluetooth disabled (setting) - BLE not started");
    }
    httpRouteMetrics();
    enableOTA();
    if (spiffsMounted) {
        File configFile = SPIFFS.open("/config.txt", "r");
        if (configFile) {
            Serial.println("Reading values");
            while (configFile.available())
            {
                String hydroString = configFile.readStringUntil('\n');
                hydro_mode = hydroString.c_str();
                hydro_mode.trim();
                int_hydro_mode = hydro_mode.toInt();
            }
            configFile.close();
        } else {
            Serial.println("Config file not found, using defaults");
        }
    } else {
        Serial.println("SPIFFS unavailable, using defaults");
    }

    resetButton.begin();
    resetButton.onPressedFor(5000, fullReset);
    resetButton.onPressed(buttonPressed);

    Serial.printf("Ready - firmware %s for %s\n", BUILD_VERSION, BUILD_STOVE_MODEL);

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    onboardLED.on();

    wm.server->on("/on", authed(withStoveLock(setStoveOn)));
    wm.server->on("/off", authed(withStoveLock(setStoveOff)));
    
    wm.server->on("/fan", authed(withStoveLock(handleRouteFanspeed)));
    wm.server->on("/error", authed(withStoveLock(handleRouteSetError)));

    wm.server->on("/cq", authed(withStoveLock(handleRouteCQ)));
    wm.server->on("/display", authed(withStoveLock(handleRouteDisplay)));
    wm.server->on("/flame", authed(withStoveLock(handleRouteFlamePower)));

    wm.server->on("/state", authed(getStatus));
    wm.server->on("/get", authed(withStoveLock(handleRouteDump)));
    wm.server->on("/set", authed(withStoveLock(getParams)));

    wm.server->on("/api/flame", authed(withStoveLock(handleRouteFlamePower)));
    wm.server->on("/api/fan", authed(withStoveLock(handleRouteFanspeed)));
    wm.server->on("/api/power", authed(withStoveLock(handleRoutePower)));
    wm.server->on("/api/energysaving", authed(withStoveLock(handleRouteEnergysaving)));
    wm.server->on("/api/errors", authed(withStoveLock(handleRouteErrors)));
    wm.server->on("/api/state", authed(getStatus));
    wm.server->on("/api/health", authed(withStoveLock(handleRouteHealth)));
    wm.server->on("/api/time", authed(withStoveLock(handleRouteTime)));
    wm.server->on("/api/remote", authed(withStoveLock(handleRouteRemote)));
    wm.server->on("/api/burn", authed(withStoveLock(handleRouteBurn)));
    wm.server->on("/api/beep", authed(withStoveLock(handleRouteBeep)));
    wm.server->on("/api/setpoint", authed(withStoveLock(handleRouteSetpoint)));
    wm.server->on("/api/timers",     authed(withStoveLock(handleRouteTimers)));
    wm.server->on("/api/timers/pr1", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/timers/pr2", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/timers/pr3", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/timers/pr4", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/timers/pr5", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/timers/pr6", authed(withStoveLock(handleRouteTimer)));
    wm.server->on("/api/dump", authed(withStoveLock(handleRouteDump)));
    wm.server->on("/api/keywatch", authed(handleRouteKeyWatch));   // takes the stove lock itself, in the sensor task
    wm.server->on("/api", handleRouteSwaggerUI);
    wm.server->on("/api-specs.json", handleRouteSwaggerJSON);
    wm.server->on("/app", HTTP_GET, handleRouteWebApp);   // also served on / (kept for old links)
    wm.server->on("/api/history", authed(handleRouteHistory));
    wm.server->on("/api/events", authed(handleRouteEvents));
    wm.server->on("/api/auth", handleRouteAuth);   // public: does its own check
    wm.server->on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);   // does its own check
    wm.server->on("/api/telnet", authed(handleRouteTelnet));
    wm.server->on("/api/bluetooth", authed(handleRouteBluetooth));
    wm.server->on("/api/mqtt", authed(handleRouteMqtt));
    wm.server->on("/api/wifi/scan", authed(handleRouteWifiScan));
    wm.server->on("/api/wifi", authed(handleRouteWifi));
    wm.server->on("/api/firmware", authed(handleRouteFirmware));
    wm.server->on("/", HTTP_GET, handleRouteWebApp);   // the web app is the start page
    wm.server->on("/device", handleRouteIndex);          // small fallback page with links
    wm.server->on("/restart", authed(handleRouteRestart));
    if (wifiConnected && wm.server) {
        authCollectHeaders();
        wm.server->begin();
        Serial.println("REST server started");
    }

    configTzTime(defaultTimezone, ntpServer); // sets TZ and starts NTP sync
    getNTPtime(2);
    eventsLoad();    // restore the stove state change log
    historyLoad();   // restore the persisted history (uses NTP time to drop samples older than 24 h)

    if (isConnected() && telnetEnabled)
    {
        setupTelnet();
    }
    mqttSetup();   // connects from loop() once enabled and WiFi is up
    netSetup();    // waits for confirmation of a new static IP
    fwSetup();     // update check from GitHub Releases (fwUpdate.h)
    stoveMutex = xSemaphoreCreateRecursiveMutex();
    xTaskCreatePinnedToCore(sensorTask, "SensorTask", 8192, NULL, 1, NULL, 0);
    getStates();
}

void loop()
{
    if (telnetEnabled) {
        telnet.loop();
        telnetMenuLoop();
    }
    if (bleEnabled) bleLoop();
    esp_task_wdt_reset();
    // wm.process() only calls handleClient() while the config portal is active.
    // Once connected to WiFi the portal is deactivated, so we must call it
    // explicitly to keep the REST server responsive.
    if (wifiConnected && wm.server) {
        wm.server->handleClient();
    }
    wm.process();
    resetButton.read();
    ArduinoOTA.handle();

    unsigned long currentMillis = millis();
    if (previousMillisNTP > currentMillis) { previousMillisNTP = 0; }

    historyTick();
    eventsTick();
    mqttLoop();
    netLoop();
    fwLoop();

    // Sensor cache is refreshed by a background task on the second core.

    // NTP sync + WiFi health check — every 60s
    if (currentMillis - previousMillisNTP >= 60000)
    {
        // A WiFi reconnect scans the shared radio and usually drops the BLE link,
        // so normally we wait while a BLE client is connected. But never longer
        // than WIFI_FORCE_RECONNECT_MS: otherwise a long BLE session keeps the
        // device unreachable over the network indefinitely.
        static const unsigned long WIFI_FORCE_RECONNECT_MS = 5UL * 60UL * 1000UL;
        static unsigned long wifiDownSince = 0;

        if (netWifiBusy())
        {
            // switching to another network (wifiInfo.h): no reconnect attempts meanwhile
        }
        else if (WiFi.status() != WL_CONNECTED)
        {
            if (wifiDownSince == 0) wifiDownSince = currentMillis;
            bool force = currentMillis - wifiDownSince >= WIFI_FORCE_RECONNECT_MS;

            if (!bleOwnsRadio() || force)
            {
                onboardLED.off();
                onboardLED.flash();
                if (bleOwnsRadio())
                {
                    Serial.println("WiFi down for 5 min while BLE is active - forcing reconnect (BLE may drop)");
                    wifiDownSince = currentMillis;  // next forced attempt after another 5 min
                }
                else
                {
                    Serial.println("Reconnecting to WiFi...");
                }
                WiFi.disconnect(false);  // reset state machine, keep credentials
                WiFi.begin();            // reconnect with stored credentials
            }
        }
        else
        {
            wifiDownSince = 0;
            getNTPtime(2);      // snapshot of the system clock + DST flag (SNTP syncs it in the background)
            onboardLED.on();
        }
        previousMillisNTP = currentMillis;
    }
}
