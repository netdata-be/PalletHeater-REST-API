#include <constants.h>
#include <WebServer.h>

// ---------------------------------------------------------------------------
// HTTP request tracking
// ---------------------------------------------------------------------------

struct EndpointCounter {
    const char* path;
    unsigned long count;
};

static EndpointCounter endpointCounters[] = {
    { "/api/state",       0 },
    { "/api/flame",       0 },
    { "/api/fan",         0 },
    { "/api/power",       0 },
    { "/api/energysaving",0 },
    { "/api/errors",      0 },
    { "/api/health",      0 },
    { "/api/time",        0 },
    { "/api/remote",      0 },
    { "/api/burn",        0 },
    { "/api/beep",        0 },
    { "/api/setpoint",    0 },
    { "/api/timers",      0 },
    { "/api/timers/pr1",  0 },
    { "/api/timers/pr2",  0 },
    { "/api/timers/pr3",  0 },
    { "/api/timers/pr4",  0 },
    { "/api/timers/pr5",  0 },
    { "/api/timers/pr6",  0 },
    { "/api/dump",        0 },
    { "/api-specs.json",  0 },
    { "/app",             0 },
    { "/api/history",     0 },
    { "/api/events",      0 },
    { "/api/bluetooth",   0 },
    { "/api/telnet",      0 },
    { "/api/auth",        0 },
    { "/api/update",      0 },
    { "/metrics",         0 },
    { "/api",             0 },
};
static const int endpointCountersSize = sizeof(endpointCounters) / sizeof(endpointCounters[0]);

// Access log: every request at level debug of module "http" (method, path,
// parameters, client). Secrets in parameters are masked. The live log's own
// polling (/api/log, /api/busmon) is left out, it would only log itself.
static void httpAccessLog(HTTPMethod method, const String& uri)
{
    LogModule* m = logModule("http");
    if (!m || m->level < LOG_DEBUG) return;
    if (uri == "/api/log" || uri == "/api/busmon") return;
    const char* mt = method == HTTP_GET ? "GET" : method == HTTP_POST ? "POST" : method == HTTP_DELETE ? "DELETE" : method == HTTP_PUT ? "PUT" : method == HTTP_PATCH ? "PATCH" : method == HTTP_OPTIONS ? "OPTIONS" : "?";
    String q;
    for (int i = 0; i < wm.server->args(); i++) {
        String k = wm.server->argName(i);
        if (k == "plain") continue;   // request body
        String v = wm.server->arg(i);
        String kl = k; kl.toLowerCase();
        if (kl.indexOf("pass") >= 0 || kl.indexOf("key") >= 0 || kl.indexOf("token") >= 0) v = "***";
        if (v.length() > 40) v = v.substring(0, 40) + "…";
        q += (q.length() ? "&" : "?") + k + "=" + v;
    }
    LOGD("http", "%s %s%s from %s", mt, uri.c_str(), q.c_str(), clientIp().c_str());
}

class HttpMetricsHandler : public RequestHandler {
public:
    unsigned long requests_total = 0;
    unsigned long requests_get   = 0;
    unsigned long requests_post  = 0;
    unsigned long requests_patch = 0;
    unsigned long requests_other = 0;

    bool canHandle(HTTPMethod method, String uri) override {
        requests_total++;
        netHttpSeen = true;   // confirms a new static IP, see wifiInfo.h
        switch (method) {
            case HTTP_GET:   requests_get++;   break;
            case HTTP_POST:  requests_post++;  break;
            case HTTP_PATCH: requests_patch++; break;
            default:         requests_other++; break;
        }
        httpAccessLog(method, uri);
        for (int i = 0; i < endpointCountersSize; i++) {
            if (uri == endpointCounters[i].path) {
                endpointCounters[i].count++;
                break;
            }
        }
        return false; // never handle — let the real handlers run
    }

    bool handle(WebServer& server, HTTPMethod method, String uri) override {
        return false;
    }
};

static HttpMetricsHandler httpMetricsHandler;   // the counters
// A WebServer deletes its handlers when it is destroyed (WiFiManager does that when
// it closes its portal), so each server gets its own small forwarder on the heap.
class HttpMetricsForwarder : public RequestHandler {
public:
    bool canHandle(HTTPMethod method, String uri) override { return httpMetricsHandler.canHandle(method, uri); }
    bool handle(WebServer&, HTTPMethod, String) override { return false; }
};

// ---------------------------------------------------------------------------
// Metrics output
// ---------------------------------------------------------------------------

// Stove values never read since the start are left out (Prometheus: absent =
// unknown), instead of exporting the firmware's initial 0s. State, power and
// the serial counters are always exported.
static bool metricsStoveKnown = true;
static bool metricSkipped(const String& m)
{
    return !metricsStoveKnown && m.startsWith("stove_") && !m.startsWith("stove_serial_") && m != "stove_state" && m != "stove_powered_on";
}
void setMetric(String *p, String metric, String help, String value) {
  if (metricSkipped(metric)) return;
  *p += "# HELP " + metric + " " + help + "\n";
  *p += "# TYPE " + metric + " gauge\n";
  *p += metric + " " + value + "\n";
}

void setCounter(String *p, String metric, String help, String value) {
  if (metricSkipped(metric)) return;
  *p += "# HELP " + metric + " " + help + "\n";
  *p += "# TYPE " + metric + " counter\n";
  *p += metric + " " + value + "\n";
}

String getMetrics() {
  String p = "";
  metricsStoveKnown = lastFastStatesAt != 0 && lastSlowStatesAt != 0;

  int sketch_size    = ESP.getSketchSize();
  int flash_size     = ESP.getFreeSketchSpace();
  int available_size = flash_size - sketch_size;

  // ESP32 system metrics
  setMetric(&p,  "esp32_uptime_ms",            "Uptime in milliseconds",                           String(millis()));
  setMetric(&p,  "esp32_cpu_freq_mhz",         "CPU frequency in MHz",                             String(ESP.getCpuFreqMHz()));

  // ESP32 memory metrics
  setMetric(&p,  "esp32_heap_size",            "Total heap size in bytes",                         String(ESP.getHeapSize()));
  setMetric(&p,  "esp32_free_heap",            "Free heap in bytes",                               String(ESP.getFreeHeap()));
  setMetric(&p,  "esp32_min_free_heap",        "Minimum free heap since boot in bytes",            String(ESP.getMinFreeHeap()));
  if (spiffsMounted) {
    setMetric(&p, "esp32_spiffs_used_bytes",  "SPIFFS bytes used (history, events, settings)", String((unsigned)SPIFFS.usedBytes()));
    setMetric(&p, "esp32_spiffs_total_bytes", "SPIFFS size in bytes",                         String((unsigned)SPIFFS.totalBytes()));
  }
  setCounter(&p, "esp32_boot_count",           "Starts of the controller",                         String(diagBoots));
  setCounter(&p, "esp32_crash_count",          "Starts after a crash, watchdog or brown-out",      String(diagCrashes));
  setCounter(&p, "esp32_wifi_disconnects_total", "Drops of a working WiFi connection since start", String((uint32_t)wifiDisconnects));
  setCounter(&p, "esp32_wifi_watchdog_repairs_total", "Network watchdog repairs (reconnect, WiFi driver restart) since start", String(netwdRepairs));
  p += String("# HELP esp32_reset_info Reason of the last start\n# TYPE esp32_reset_info gauge\nesp32_reset_info{reason=\"") + resetReasonName(diagReason) + "\"} 1\n";
  setMetric(&p,  "esp32_max_alloc_heap",       "Largest allocatable heap block in bytes",          String(ESP.getMaxAllocHeap()));
  // never used part of each task's stack: shows how far a stack can safely be made smaller
  {
    p += "# HELP esp32_task_stack_free_bytes Stack bytes never used since boot, per task\n";
    p += "# TYPE esp32_task_stack_free_bytes gauge\n";
    static const char* const tasks[] = { "loopTask", "SensorTask", "https", "http6", "fwUpdate" };
    for (const char* name : tasks) {
      TaskHandle_t h = xTaskGetHandle(name);
      if (h) p += String("esp32_task_stack_free_bytes{task=\"") + name + "\"} " + String(uxTaskGetStackHighWaterMark(h)) + "\n";
    }
  }

  // ESP32 flash metrics
  setMetric(&p,  "esp32_flash_chip_size",      "Total flash chip size in bytes",                   String(ESP.getFlashChipSize()));
  setMetric(&p,  "esp32_sketch_size",          "Sketch size in bytes",                             String(sketch_size));
  setMetric(&p,  "esp32_free_sketch_space",    "Free sketch space in bytes",                       String(flash_size));
  setMetric(&p,  "esp32_available_size",       "Available flash space in bytes",                   String(available_size));

  // WiFi / network metrics
  setMetric(&p,  "wifi_connected",             "1 if WiFi is connected",                           String(WiFi.status() == WL_CONNECTED ? 1 : 0));
  setMetric(&p,  "wifi_rssi_dbm",              "WiFi signal strength in dBm",                      String(WiFi.RSSI()));
  setMetric(&p,  "wifi_channel",               "WiFi channel number",                              String(WiFi.channel()));
  setMetric(&p,  "wifi_tx_power_dbm",          "WiFi transmit power in dBm",                       String(WiFi.getTxPower()));

  // HTTP request counters
  setCounter(&p, "http_requests_total",          "Total number of HTTP requests",          String(httpMetricsHandler.requests_total));
  setCounter(&p, "http_requests_get_total",      "Total number of HTTP GET requests",      String(httpMetricsHandler.requests_get));
  setCounter(&p, "http_requests_post_total",     "Total number of HTTP POST requests",     String(httpMetricsHandler.requests_post));
  setCounter(&p, "http_requests_patch_total",    "Total number of HTTP PATCH requests",    String(httpMetricsHandler.requests_patch));
  setCounter(&p, "http_requests_other_total",    "Total number of other HTTP requests",    String(httpMetricsHandler.requests_other));

  // Per-endpoint counters (HELP/TYPE must appear only once per metric name)
  p += "# HELP http_requests_endpoint_total Number of HTTP requests per endpoint\n";
  p += "# TYPE http_requests_endpoint_total counter\n";
  for (int i = 0; i < endpointCountersSize; i++) {
    String name = "http_requests_endpoint_total{path=\"";
    name += endpointCounters[i].path;
    name += "\"}";
    p += name + " " + String(endpointCounters[i].count) + "\n";
  }

  // Stove state
  setMetric(&p, "stove_state",                   "Stove state code",                       String(stoveState));
  if (stoveState != UNKNOWN)   // unknown while the stove does not answer: left out
    setMetric(&p, "stove_powered_on",            "1 if stove is powered on",               String(StoveIsOn ? 1 : 0));

  // Temperatures
  setMetric(&p, "stove_temp_room_c",             "Room ambient temperature in Celsius",             String(ambTemp));
  setMetric(&p, "stove_temp_stove_c",            "Stove sensor ambient temperature in Celsius",     String(ambTempStove));
  setMetric(&p, "stove_temp_remote_c",           "Remote control ambient temperature in Celsius",   String(ambTempRemoteControl));
  setMetric(&p, "stove_temp_setpoint_c",         "Room temperature setpoint in Celsius",            String(setPointTemp));
  setMetric(&p, "stove_wireless_sensor_enabled", "INSTELL WIRELESS: 1 = external wireless temperature sensor enabled, 0 = off, -1 = unknown", String(cachedWirelessSensor));
  setMetric(&p, "stove_temp_fume_c",             "Fume temperature in Celsius",                     String(fumesTemp));

  // Fan & flame
  setMetric(&p, "stove_flame_power",             "Flame power level setting (1-4)",        String(flamePower));
  p += "# HELP esp32_build_info Controller firmware build (labels), always 1\n# TYPE esp32_build_info gauge\n";
  p += String("esp32_build_info{version=\"") + BUILD_VERSION + "\",built=\"" + BUILD_TIME + "\",env=\"" + BUILD_ENV + "\",stove_model=\"" + BUILD_STOVE_MODEL + "\"} 1\n";
  setMetric(&p, "stove_room_fan_actual",          "Speed the room fan really runs at, 0-3 (-1 unknown)", String(roomFanActual()));
  setMetric(&p, "stove_room_fan_running",         "Room fan turning (power RAM 0x0A > 0, -1 unknown)", String(roomFanIsRunning()));
  { const char* m = roomFanMode();
    setMetric(&p, "stove_room_fan_mode",          "Room fan from its power (RAM 0x0A): 0 off, 1 on, -1 unknown",
              String(!m ? -1 : strcmp(m, "off") == 0 ? 0 : 1)); }
  setMetric(&p, "stove_room_fan_drive",           "Room fan drive value (RAM 0x0A)", String(cachedRoomFanControl));
  setMetric(&p, "stove_brazier_cleaning_minutes", "Minutes to the next brazier cleaning (RAM 0x32, -1 when not working)", String(stoveState == 4 ? cachedBrazierCountdown : -1));
  setMetric(&p, "stove_flame_power_actual",      "Flame power the stove currently runs at (RAM 0x34)", String(cachedFlameActual));
  setMetric(&p, "esp32_bluetooth_enabled",       "1 if BLE is running",                    String(bleEnabled ? 1 : 0));
  setMetric(&p, "esp32_telnet_enabled",          "1 if the telnet menu is running",        String(telnetEnabled ? 1 : 0));
  setMetric(&p, "esp32_api_key_required",        "1 if the HTTP API requires an API key",  String(apiKeyRequired ? 1 : 0));
  setMetric(&p, "stove_room_fan_speed",          "Room fan speed (0-3)",                   String(roomFanSpeed));
  setMetric(&p, "stove_fume_fan_rpm",            "Fume fan RPM",                           String(cachedFumeFanRPM));
  setMetric(&p, "stove_auger_speed",             "Auger on-time per cycle in seconds (RAM 0x0D / 10; the cycle is about 12 s)",                 String(cachedAugerSpeed));
  setMetric(&p, "stove_auger_percent",           "Share of the time the auger runs, 0 - 100 (on-time / 12 s cycle)",                          String(pc::augerPercent(cachedAugerSpeed)));

  // Energy saving
  setMetric(&p, "stove_energy_saving_start",     "Energy saving start offset in Celsius",  String(energySavingStart));
  setMetric(&p, "stove_energy_saving_stop",      "Energy saving stop offset in Celsius",   String(energySavingStop));

  // Pellets
  setMetric(&p, "stove_low_pallet_alarm",        "1 if pellet level is low",               String(cachedLowPallet));

  // Remote control
  setMetric(&p, "stove_remote_last_seen",        "Seconds since last remote control contact", String(cachedRemoteControlLastSeen));

  // Timer metrics
  if (metricsStoveKnown) {
  p.concat("# HELP stove_timer_enabled 1 if timer is enabled\n");
  p.concat("# TYPE stove_timer_enabled gauge\n");
  p.concat("# HELP stove_timer_on_minutes On time in minutes since midnight (-1 = not set)\n");
  p.concat("# TYPE stove_timer_on_minutes gauge\n");
  p.concat("# HELP stove_timer_off_minutes Off time in minutes since midnight (-1 = not set)\n");
  p.concat("# TYPE stove_timer_off_minutes gauge\n");
  p.concat("# HELP stove_timer_dow_mask Day-of-week bitmask (Mon=1 Tue=2 Wed=4 Thu=8 Fri=16 Sat=32 Sun=64)\n");
  p.concat("# TYPE stove_timer_dow_mask gauge\n");
  p.concat("# HELP stove_timer_setpoint_c Temperature setpoint for timer\n");
  p.concat("# TYPE stove_timer_setpoint_c gauge\n");
  p.concat("# HELP stove_timer_flame_power Flame power configured for timer\n");
  p.concat("# TYPE stove_timer_flame_power gauge\n");
  p.concat("# HELP stove_timer_room_fan_speed Room fan speed configured for timer\n");
  p.concat("# TYPE stove_timer_room_fan_speed gauge\n");

  for (int i = 0; i < 6; i++) {
    char timerLabel[8];
    sprintf(timerLabel, "PR%d", i + 1);
    char labels[24];
    sprintf(labels, "{timer=\"%s\"}", timerLabel);

    int enabled = cachedTimerEnabled[i];
    int onVal = cachedTimerOnVal[i];
    int offVal = cachedTimerOffVal[i];
    int setpoint = cachedTimerSetpoint[i];
    int flame = cachedTimerFlame[i];
    int fan = cachedTimerFan[i];
    int dow = cachedTimerDowMask[i];

    p.concat("stove_timer_enabled"); p.concat(labels); p.concat(" "); p.concat(String(enabled == 1 ? 1 : 0)); p.concat("\n");
    p.concat("stove_timer_on_minutes"); p.concat(labels); p.concat(" "); p.concat(String(onVal  >= 144 ? -1 : onVal  * 10)); p.concat("\n");
    p.concat("stove_timer_off_minutes"); p.concat(labels); p.concat(" "); p.concat(String(offVal >= 144 ? -1 : offVal * 10)); p.concat("\n");
    p.concat("stove_timer_dow_mask"); p.concat(labels); p.concat(" "); p.concat(String(dow)); p.concat("\n");
    p.concat("stove_timer_setpoint_c"); p.concat(labels); p.concat(" "); p.concat(String(setpoint)); p.concat("\n");
    p.concat("stove_timer_flame_power"); p.concat(labels); p.concat(" "); p.concat(String(flame)); p.concat("\n");
    p.concat("stove_timer_room_fan_speed"); p.concat(labels); p.concat(" "); p.concat(String(fan)); p.concat("\n");
  }
  }

  // Counters
  setCounter(&p, "stove_number_of_starts",       "Total number of stove starts",           String(cachedNumberOfStarts));
  // Serial bus health
  setCounter(&p, "stove_serial_read_errors_total",  "Stove reads that failed after all retries",  String(stove.readErrors));
  setCounter(&p, "stove_serial_write_errors_total", "Stove writes that failed after all retries", String(stove.writeErrors));
  setCounter(&p, "stove_serial_retries_total",      "Individual stove read/write retry attempts", String(stove.retries));
  setMetric(&p,  "stove_serial_link_up",            "1 when the stove answers on the serial bus, 0 when the link is down", String(stove.linkDown ? 0 : 1));
  setCounter(&p, "stove_serial_skipped_total",      "Stove reads/writes refused at once because the link was down", String(stove.skipped));
  setCounter(&p, "stove_serial_transactions_total", "Stove reads + writes (not counting retries)", String(stove.transactions));

  if (stoveTimeReadMs) setMetric(&p, "stove_time_delta_seconds", "Delta in seconds between NTP time and stove internal clock", String(ntpTimeNow() - stoveTimeNow()));

  // Firmware version (info metric — value is always 1, label carries the version string)
  if (metricsStoveKnown) {
  p += "# HELP stove_firmware_info Firmware version of the stove\n";
  p += "# TYPE stove_firmware_info gauge\n";
  {
    String firmwareMetric = String("stove_firmware_info{version=\"") + String(cachedFirmwareVersion) + String("\"} 1\n");
    p += firmwareMetric;
  }
  if (cachedSerialNumber >= 0) {
    p += "# HELP stove_serial_number_info Serial number of the stove (MATR on the nameplate)\n";
    p += "# TYPE stove_serial_number_info gauge\n";
    p += String("stove_serial_number_info{serial=\"") + String(cachedSerialNumber) + String("\"} 1\n");
  }
  }

  return p;
}

void handleMetrics() {
  httpSendText(200, "text/plain; version=0.0.4", getMetrics());
}

void httpRouteMetrics() {
  wm.server->addHandler(new HttpMetricsForwarder());
  wm.server->on("/metrics", authed(handleMetrics));   // Prometheus can send "Authorization: Bearer <key>"
}
