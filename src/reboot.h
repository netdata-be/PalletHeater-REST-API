#include <constants.h>

void handleReboot() {
    log_e("Reboot asked using HTTP, rebooting...");
    StaticJsonDocument<250> json;
    json.clear();

    json["msg"] = "rebooting";
    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
    wm.process();
    wm.disconnect();
    delay(200);
    ESP.restart();
}

void httpRouteReboot() {
  wm.server->on("/reboot", handleReboot);
}