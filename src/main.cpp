#include "liveLog.h"   // first: copies Serial output into the live log (redefines Serial)
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ota.h>
#include <constants.h>
#include "otaVerify.h"  // rollback of an update that does not start properly
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
#include <keyWatch.h>
#include <Preferences.h>
#include <esp_bt.h>

#define FORMAT_SPIFFS_IF_FAILED true


#include "stoveCache.h"

#include "buzzer.h"    // buzzer of the S3 board (BOARD_HAS_BUZZER); empty calls on the DevKit

#include "stoveControl.h"

#include "registers.h"

#include "stoveClock.h"

#include "routesBasic.h"

#include "routesTimers.h"

#include "routesControl.h"

#include "status.h"

#include "history.h"

#include "pellets.h"   // pellet consumption, time left until the low pellet alarm

#include "events.h"

#include "routesDevice.h"

#include "mqtt.h"

#include "boardInfo.h"   // features, power source, chip temperature of the board

#include "wifiInfo.h"

#include "fwUpdate.h"

#include "statusLedApi.h"   // WS2812 status LEDs: states, /api/leds (board only)

#include "boardButton.h"    // the board's button: presses, hold, full reset countdown (board only)

#include "liveDebug.h"

#include "coreDump.h"   // core dump of the last crash, /api/coredump

#include "https.h"      // HTTPS on port 443 in front of the HTTP server, self-signed certificate
#include "http6.h"      // HTTP over IPv6 on port 80 (the Arduino web server is IPv4 only)

#include "telnetMenu.h" // telnet menu on port 23 (IPv4 + IPv6); last: its screens use all modules above

// Serialise all stove bus access: HTTP handlers run on the loop task while the
// sensor task polls the stove from the other core. Without the lock both talk
// on the bus at the same time and reads return garbage.
static std::function<void()> withStoveLock(void (*fn)())
{
    return [fn]() {
        if (!stoveMutex) { fn(); return; }
        // The stove does not answer: fail fast instead of trying every register 3 times
        // while the (single threaded) web server waits - other requests stay responsive.
        if (stove.linkDown) {   // the serial link to the stove is down: every bus command fails at once
            LOGD("http", "503 %s: the stove does not answer", wm.server->uri().c_str());
            if (wm.server->uri() == "/api/health") wm.server->send(500, "application/json; charset=utf-8", "{\"health\":\"Failed to talk to Stove\"}");
            else wm.server->send(503, "application/json; charset=utf-8", "{\"error\":\"The stove does not answer (serial connection) - try again later\"}");
            return;
        }
        if (!stoveLockTake(3000))   // feeds the 5 s task watchdog while waiting
        {
            LOGW("http", "503 %s %s: stove bus busy for 3 s", wm.server->method() == HTTP_GET ? "GET" : "POST", wm.server->uri().c_str());
            wm.server->send(503, "application/json; charset=utf-8", "{\"error\":\"Stove bus busy, try again\"}");
            return;
        }
        {
            // writes done by this request are logged with where they came from
            char src[96];
            snprintf(src, sizeof(src), "web/API %s (%s %s)", clientIp().c_str(),
                     wm.server->method() == HTTP_GET ? "GET" : wm.server->method() == HTTP_POST ? "POST" : "other", wm.server->uri().c_str());
            CmdSource cs(src);
            fn();
        }
        xSemaphoreGiveRecursive(stoveMutex);
    };
}

// All HTTP routes. Called in setup(), and again when WiFi only connected after
// WiFiManager had started its config portal (that portal's server is replaced).
static void registerRoutes()
{
    httpRouteMetrics();   // first: counts every request and writes the access log
    wm.server->on("/on", authed(withStoveLock(setStoveOn)));
    wm.server->on("/off", authed(withStoveLock(setStoveOff)));
    
    wm.server->on("/fan", authed(withStoveLock(handleRouteFanspeed)));
    wm.server->on("/error", authed(withStoveLock(handleRouteSetError)));

    wm.server->on("/cq", authed(withStoveLock(handleRouteCQ)));
    wm.server->on("/display", authed(withStoveLock(handleRouteDisplay)));
    wm.server->on("/flame", authed(withStoveLock(handleRouteFlamePower)));

    wm.server->on("/state", authed(getStatus));
    wm.server->on("/get", authed(handleRouteDump));        // takes the bus per register itself
    wm.server->on("/set", authed(withStoveLock(getParams)));

    wm.server->on("/api/flame", authed(withStoveLock(handleRouteFlamePower)));
    wm.server->on("/api/fan", authed(withStoveLock(handleRouteFanspeed)));
    wm.server->on("/api/power", authed(withStoveLock(handleRoutePower)));
    wm.server->on("/api/energysaving", authed(withStoveLock(handleRouteEnergysaving)));
    wm.server->on("/api/errors", authed(withStoveLock(handleRouteErrors)));
    wm.server->on("/api/state", authed(getStatus));
    wm.server->on("/api/health", crumbed(withStoveLock(handleRouteHealth)));   // public: for monitoring without a key
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
    wm.server->on("/api/dump", authed(handleRouteDump));   // takes the bus per register itself
    wm.server->on("/api/keywatch", authed(handleRouteKeyWatch));   // takes the stove lock itself, in the sensor task
    wm.server->on("/api", crumbed(handleRouteSwaggerUI));
    wm.server->on("/api-specs.json", crumbed(handleRouteSwaggerJSON));
    wm.server->on("/app", HTTP_GET, crumbed(httpsOr(handleRouteWebApp)));   // also served on / (kept for old links)
    wm.server->on("/api/history", authed(handleRouteHistory));
    wm.server->on("/api/events", authed(handleRouteEvents));
    wm.server->on("/api/plan", authed(handleRoutePlan));
    wm.server->on("/api/pellets", authed(handleRoutePellets));
    wm.server->on("/api/pellets/days", authed(handleRoutePelletDays));
    wm.server->on("/api/auth", crumbed(handleRouteAuth));   // public: does its own check
    wm.server->on("/api/update", HTTP_POST, crumbed(handleUpdateDone), crumbed(handleUpdateUpload));   // does its own check
    wm.server->on("/api/telnet", authed(handleRouteTelnet));
    wm.server->on("/api/bluetooth", authed(handleRouteBluetooth));
    wm.server->on("/api/mqtt", authed(handleRouteMqtt));
    wm.server->on("/api/wifi/scan", authed(handleRouteWifiScan));
    wm.server->on("/api/wifi", authed(handleRouteWifi));
    wm.server->on("/api/firmware", authed(handleRouteFirmware));
    wm.server->on("/update", HTTP_GET, crumbed(handleRouteUpdatePage));   // status page of an install from GitHub (public)
    wm.server->on("/api/log", authed(handleRouteLog));
    wm.server->on("/api/busmon", authed(handleRouteBusmon));
    wm.server->on("/api/loglevel", authed(handleRouteLogLevel));
    wm.server->on("/api/diag/reset", authed(handleRouteDiagReset));
    wm.server->on("/api/coredump", authed(handleRouteCoreDump));
    wm.server->on("/api/https", authed(handleRouteHttps));
    buzzerRoutes();   // /api/buzzer, only with BOARD_HAS_BUZZER (buzzer.h)
    wm.server->on("/", HTTP_GET, crumbed(httpsOr(handleRouteWebApp)));   // the web app is the start page
    wm.server->on("/device", crumbed(httpsOr(handleRouteIndex)));          // small fallback page with links
    wm.server->on("/manifest.webmanifest", HTTP_GET, crumbed(handleRouteManifest));   // home screen install (public)
    wm.server->on("/icon.svg", HTTP_GET, crumbed(handleRouteIconSvg));
    wm.server->on("/icon-180.png", HTTP_GET, crumbed(handleRouteIcon180));
    wm.server->on("/restart", authed(handleRouteRestart));
#if BOARD_HAS_WS2812
    wm.server->on("/api/leds", authed(handleRouteLeds));
#endif
}

void setup()
{
    esp_log_level_set("*", ESP_LOG_WARN);
    boardPinsSetup();   // ENABLE_RX on the DevKit (board.h)
#if BOARD_HAS_WS2812
    statusLedSetup();   // LEDs show "booting" from here on (statusLed.h)
#endif

    // the time zone before the first log line: the clock keeps running over a software restart,
    // so the start-up lines get local time instead of UTC until configTzTime() below
    setenv("TZ", defaultTimezone, 1);
    tzset();
    otaVerifySetup(); // first: note the version on trial (rollback of a failed update, otaVerify.h)
    Serial.begin(115200);
    liveLogSetup();   // also capture ESP-IDF log output for /api/log; reset reason, log of the previous run
    otaVerifyReport();
    coreDumpReport(); // after a crash: task, PC and backtrace from the core dump
    // bus: a transaction that failed for good is a warning; retried attempts and the
    // frames of the telnet bus debug are debug
    // every write with its source (liveDebug.h); the read planner reads that register again at once
    Stove::writeHook = [](uint8_t type, uint8_t addr, uint8_t value, bool ok) { stoveWriteLogger(type, addr, value, ok); if (ok) planWritten(type, addr); };
    Stove::logger = [](bool failed, const char* line) { if (failed && !stove.linkDown) LOGW("bus", "%s", line); else LOGD("bus", "%s", line); };
    
    spiffsMounted = SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED); // Mount SPIFFS
    if (spiffsMounted)
    {
        LOGI("sys", "SPIFFS mounted: %u of %u bytes used", (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
    }
    else
    {
        LOGE("sys", "SPIFFS could not be mounted");
    }
    netEventsSetup();   // log why WiFi drops (before WiFi starts)
    setup_wifi();
    fwBootInstall();    // install from GitHub asked before the restart: download now, before the rest starts (fwUpdate.h)
    {
        // mDNS name = the configured hostname (IP settings), default openpelletbridge
        Preferences prefs;
        prefs.begin("net", true);
        String hn = prefs.getString("hostname", "openpelletbridge");
        prefs.end();
        mdnsStart(hn.c_str());
    }
    authInit();
    {
        Preferences prefs;
        prefs.begin("ble", true);
        bleEnabled = OPB_BLUETOOTH && prefs.getBool("enabled", true);   // not in a build without Bluetooth
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
        // controller and host (Bluedroid, not used: the firmware has NimBLE) memory back to the
        // heap; switching Bluetooth on again needs a restart anyway
        esp_bt_mem_release(ESP_BT_MODE_BTDM);
        WiFi.setSleep(false);
        LOGI("sys", "Bluetooth disabled (setting) - BLE not started");
    }
    enableOTA(apiKey);   // OTA password = API key
    fwSetup();           // update check from GitHub Releases (fwUpdate.h); before HTTPS: the start-up check goes first
    httpsSetup();        // HTTPS task: waits for WiFi, makes the certificate the first time
    http6Setup();        // IPv6 relay for plain HTTP
    if (spiffsMounted) {
        File configFile = SPIFFS.open("/config.txt", "r");
        if (configFile) {
            while (configFile.available())
            {
                String hydroString = configFile.readStringUntil('\n');
                hydro_mode = hydroString.c_str();
                hydro_mode.trim();
                int_hydro_mode = hydro_mode.toInt();
            }
            configFile.close();
        } else {
            LOGI("sys", "no /config.txt: hydro mode off (default)");
        }
    } else {
        LOGW("sys", "SPIFFS unavailable: hydro mode off (default)");
    }

    resetButton.begin();
#if !BOARD_HAS_BUTTON_MENU   // the board decodes presses and holds itself (boardButton.h)
    resetButton.onPressedFor(5000, fullReset);
    resetButton.onPressed(buttonPressed);
#endif

    LOGI("sys", "ready - firmware %s for %s, IP %s", BUILD_VERSION, BUILD_STOVE_MODEL, WiFi.localIP().toString().c_str());

    onboardLED.on();

    registerRoutes();
    if (wifiConnected && wm.server) {
        authCollectHeaders();
        wm.server->begin();
        LOGI("http", "web server started");
    }

    configTzTime(defaultTimezone, ntpServer); // sets TZ and starts NTP sync
    getNTPtime(2);
    eventsLoad();    // restore the stove state change log
    historyLoad();   // restore the persisted history (uses NTP time to drop samples older than 24 h)
    weekLoad();      // hourly averages of the last 7 days, completed from the 24 h history
    pelletsLoad();   // pellet consumption calibration (NVS)

    if (isConnected() && telnetEnabled)
    {
        setupTelnet();
    }
    mqttSetup();   // connects from loop() once enabled and WiFi is up
    buzzerSetup(); // S3 board: buzzer task, watches the stove for its events (buzzer.h)
    netSetup();    // waits for confirmation of a new static IP
    stoveMutex = xSemaphoreCreateRecursiveMutex();
    // 5 KB stack: it used 2.3 KB at most (measured, esp32_task_stack_free_bytes)
    Stove::beforeTx = planBeforeTx;   // background reads hand the bus over to a waiting command (stoveCache.h)
    xTaskCreatePinnedToCore(sensorTask, "SensorTask", 5120, NULL, 1, &stoveReaderTask, 0);
    getStates();
}

void loop()
{
    // crumb(): breadcrumb for a watchdog reset, see liveLog.h
    if (telnetEnabled) {
        crumb("telnet");
        telnetLoop();
    }
    if (bleEnabled) { crumb("bluetooth"); bleLoop(); }
    esp_task_wdt_reset();
    // wm.process() only calls handleClient() while the config portal is active.
    // Once connected to WiFi the portal is deactivated, so we must call it
    // explicitly to keep the REST server responsive.
    if (wifiConnected && wm.server) {
        crumb("http: accepting / reading a request");   // a route handler overwrites it with its request
        wm.server->handleClient();
        // back to a short read timeout for the next request (see WebServerPeek::readTimeout)
        static_cast<WebServerPeek*>(wm.server.get())->readTimeout(HTTP_READ_TIMEOUT_S);
    }
    crumb("WiFiManager");
    wm.process();
    // WiFi connected only after WiFiManager had started its config portal (connect
    // timeout at start-up): close the portal and start our own server, otherwise
    // the portal keeps answering "/" instead of the web app.
    if (!wifiConnected && WiFi.status() == WL_CONNECTED) {
        LOGI("wifi", "connected to '%s' (IP %s) after the config portal started - closing the portal, starting the web app",
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        wm.stopConfigPortal();
        wm.server.reset(new WebServer(80));
        registerRoutes();
        authCollectHeaders();
        wm.server->begin();
        wifiConnected = true;
        mdnsStart(WiFi.getHostname());
        if (telnetEnabled) setupTelnet();
        esp_task_wdt_init(WDT_TIMEOUT, true);
        esp_task_wdt_add(NULL);
    }
    crumb("reset button");
    resetButton.read();
#if BOARD_HAS_BUTTON_MENU
    boardButtonTick();
#endif
    statusLedLoop();
    crumb("board"); boardTick();   // power source, chip temperature (boardInfo.h)
    crumb("ArduinoOTA");
    ArduinoOTA.handle();

    unsigned long currentMillis = millis();
    if (previousMillisNTP > currentMillis) { previousMillisNTP = 0; }

    crumb("history"); historyTick();
    crumb("events");  eventsTick();
    crumb("pellets"); pelletsTick();
    crumb("MQTT");    mqttLoop();
    crumb("network"); netLoop();
    crumb("firmware update check"); fwLoop();
    crumb("firmware confirm"); otaVerifyLoop();   // confirm a new firmware after 2 min with WiFi (otaVerify.h)
    crumb("diagnostics"); diagTick();   // health line, clock drift, remote control (liveDebug.h)

    // Sensor cache is refreshed by a background task on the second core.

    // NTP sync + WiFi health check — every 60s
    if (currentMillis - previousMillisNTP >= 60000)
    {
        crumb("NTP / WiFi check");
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
                    LOGW("wifi", "down for 5 min while BLE is active - forcing reconnect (BLE may drop)");
                    wifiDownSince = currentMillis;  // next forced attempt after another 5 min
                }
                else
                {
                    LOGW("wifi", "not connected - reconnecting");
                }
                WiFi.disconnect(false);  // reset state machine, keep credentials
                wifiStaConfigFix();      // all channels, strongest access point (setupWifi.h)
                WiFi.begin();            // reconnect with stored credentials
            }
        }
        else
        {
            wifiDownSince = 0;
            static bool ntpOk = true;
            bool ok = getNTPtime(2);      // snapshot of the system clock + DST flag (SNTP syncs it in the background)
            if (!ok && ntpOk) LOGE("sys", "NTP: no valid time - check internet access / DNS (history, timers and HTTPS need it)");
            if (ok && !ntpOk) LOGI("sys", "NTP: valid time again");
            ntpOk = ok;
            onboardLED.on();
        }
        previousMillisNTP = currentMillis;
    }
    crumb("loop: between steps");
}
