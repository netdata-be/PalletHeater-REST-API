#include <constants.h>
#include <WebServer.h>

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

uint8_t temprature_sens_read();

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
    { "/metrics",         0 },
    { "/api",             0 },
};
static const int endpointCountersSize = sizeof(endpointCounters) / sizeof(endpointCounters[0]);

class HttpMetricsHandler : public RequestHandler {
public:
    unsigned long requests_total = 0;
    unsigned long requests_get   = 0;
    unsigned long requests_post  = 0;
    unsigned long requests_patch = 0;
    unsigned long requests_other = 0;

    bool canHandle(HTTPMethod method, String uri) override {
        requests_total++;
        switch (method) {
            case HTTP_GET:   requests_get++;   break;
            case HTTP_POST:  requests_post++;  break;
            case HTTP_PATCH: requests_patch++; break;
            default:         requests_other++; break;
        }
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

static HttpMetricsHandler httpMetricsHandler;

// ---------------------------------------------------------------------------
// Metrics output
// ---------------------------------------------------------------------------

void setMetric(String *p, String metric, String help, String value) {
  *p += "# HELP " + metric + " " + help + "\n";
  *p += "# TYPE " + metric + " gauge\n";
  *p += metric + " " + value + "\n";
}

void setCounter(String *p, String metric, String help, String value) {
  *p += "# HELP " + metric + " " + help + "\n";
  *p += "# TYPE " + metric + " counter\n";
  *p += metric + " " + value + "\n";
}

String getMetrics() {
  String p = "";

  int sketch_size    = ESP.getSketchSize();
  int flash_size     = ESP.getFreeSketchSpace();
  int available_size = flash_size - sketch_size;

  // ESP32 system metrics
  setMetric(&p,  "esp32_uptime_ms",            "Uptime in milliseconds",                           String(millis()));
  setMetric(&p,  "esp32_cpu_freq_mhz",         "CPU frequency in MHz",                             String(ESP.getCpuFreqMHz()));
  setMetric(&p,  "esp32_temperature_c",        "ESP32 internal temperature in Celsius",            String((temprature_sens_read() - 32) / 1.8));
  setMetric(&p,  "esp32_temperature_f",        "ESP32 internal temperature in Fahrenheit",         String(temprature_sens_read()));

  // ESP32 memory metrics
  setMetric(&p,  "esp32_heap_size",            "Total heap size in bytes",                         String(ESP.getHeapSize()));
  setMetric(&p,  "esp32_free_heap",            "Free heap in bytes",                               String(ESP.getFreeHeap()));
  setMetric(&p,  "esp32_min_free_heap",        "Minimum free heap since boot in bytes",            String(ESP.getMinFreeHeap()));
  setMetric(&p,  "esp32_max_alloc_heap",       "Largest allocatable heap block in bytes",          String(ESP.getMaxAllocHeap()));

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

  // Per-endpoint counters
  for (int i = 0; i < endpointCountersSize; i++) {
    String name = "http_requests_endpoint_total{path=\"";
    name += endpointCounters[i].path;
    name += "\"}";
    p += "# TYPE http_requests_endpoint_total counter\n";
    p += name + " " + String(endpointCounters[i].count) + "\n";
  }

  // Stove state
  setMetric(&p, "stove_state",                   "Stove state code",                       String(stoveState));
  setMetric(&p, "stove_powered_on",              "1 if stove is powered on",               String(StoveIsOn ? 1 : 0));

  // Temperatures
  setMetric(&p, "stove_temp_room_c",             "Room ambient temperature in Celsius",             String(ambTemp));
  setMetric(&p, "stove_temp_stove_c",            "Stove sensor ambient temperature in Celsius",     String(ambTempStove));
  setMetric(&p, "stove_temp_remote_c",           "Remote control ambient temperature in Celsius",   String(ambTempRemoteControl));
  setMetric(&p, "stove_temp_setpoint_c",         "Room temperature setpoint in Celsius",            String(setPointTemp));
  setMetric(&p, "stove_temp_fume_c",             "Fume temperature in Celsius",                     String(fumesTemp));

  // Fan & flame
  setMetric(&p, "stove_flame_power",             "Flame power level (1-4)",                String(flamePower));
  setMetric(&p, "stove_room_fan_speed",          "Room fan speed (0-3)",                   String(roomFanSpeed));
  setMetric(&p, "stove_fume_fan_rpm",            "Fume fan RPM",                           String(cachedFumeFanRPM));
  setMetric(&p, "stove_auger_speed",             "Auger speed (raw / 10)",                 String(cachedAugerSpeed));

  // Energy saving
  setMetric(&p, "stove_energy_saving_start",     "Energy saving start offset in Celsius",  String(energySavingStart));
  setMetric(&p, "stove_energy_saving_stop",      "Energy saving stop offset in Celsius",   String(energySavingStop));

  // Pellets
  setMetric(&p, "stove_low_pallet_alarm",        "1 if pellet level is low",               String(cachedLowPallet));

  // Remote control
  setMetric(&p, "stove_remote_last_seen",        "Seconds since last remote control contact", String(cachedRemoteControlLastSeen));

  // Counters
  setCounter(&p, "stove_number_of_starts",       "Total number of stove starts",           String(cachedNumberOfStarts));
  setCounter(&p, "stove_total_runtime_h",        "Total runtime in hours",                 String(cachedTotalRuntime));
  setCounter(&p, "stove_hours_since_last_service", "Hours since last service",             String(cachedHoursSinceLastService));

  return p;
}

void handleMetrics() {
  wm.server->send(200, "text/plain; version=0.0.4", getMetrics());
}

void httpRouteMetrics() {
  wm.server->addHandler(&httpMetricsHandler);
  wm.server->on("/metrics", handleMetrics);
}
