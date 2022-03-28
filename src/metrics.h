#include <constants.h>

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

uint8_t temprature_sens_read();

void setMetric(String *p, String metric, String value) {
  *p += "# " + metric + "\n";
  *p += "# TYPE " + metric + " gauge\n";
  *p += "" + metric + " ";
  *p += value;
  *p += "\n";
}

String getMetrics() {
  String p = "";

  int sketch_size = ESP.getSketchSize();
  int flash_size =  ESP.getFreeSketchSpace();
  int available_size = flash_size - sketch_size;

  setMetric(&p, "esp32_uptime", String(millis()));
  setMetric(&p, "esp32_heap_size", String(ESP.getHeapSize()));
  setMetric(&p, "esp32_free_heap", String(ESP.getFreeHeap()));
  setMetric(&p, "esp32_sketch_size", String(sketch_size));
  setMetric(&p, "esp32_flash_size", String(flash_size));
  setMetric(&p, "esp32_available_size", String(available_size));
  setMetric(&p, "esp32_cpuFreqMhz", String(ESP.getCpuFreqMHz()));
  setMetric(&p, "esp32_temperature", String((temprature_sens_read() - 32) / 1.8));
  setMetric(&p, "esp32_temperature_f", String(temprature_sens_read()));

  return p;
}

void handleMetrics() {
  wm.server->send(200, "text/plain", getMetrics());
}

void httpRouteMetrics() {
  wm.server->on("/metrics", handleMetrics);
}