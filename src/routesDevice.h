#pragma once
// HTTP routes for controller settings: /api/bluetooth and /api/telnet.
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

// GET  /api/bluetooth                     -> { enabled, active, restartRequired }
// POST /api/bluetooth?enabled=true|false[&restart=true]
// The setting is stored in NVS and applied at the next boot (BLE is started or
// skipped in setup()), so a change requires a restart of the controller.
void handleRouteBluetooth()
{
    Preferences prefs;
    if (wm.server->method() == HTTP_POST)
    {
        String v = wm.server->arg("enabled");
        if (v != "true" && v != "false" && v != "1" && v != "0") {
            wm.server->send(400, "application/json", "{\"result\":\"failed - expected enabled=true/false\"}");
            return;
        }
        prefs.begin("ble", false);
        prefs.putBool("enabled", v == "true" || v == "1");
        prefs.end();
    }
    else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }

    prefs.begin("ble", true);
    bool saved = prefs.getBool("enabled", true);
    prefs.end();
    bool restart = wm.server->method() == HTTP_POST && wm.server->arg("restart") == "true" && saved != bleEnabled;

    StaticJsonDocument<192> json;
    if (wm.server->method() == HTTP_POST) json["result"] = restart ? "success - restarting" : "success";
    json["enabled"] = saved;
    json["active"] = bleEnabled;
    json["restartRequired"] = saved != bleEnabled;
    String out; serializeJson(json, out);
    wm.server->send(200, "application/json", out);
    if (restart) { delay(300); restartWith("Bluetooth setting changed"); }
}

// GET  /api/telnet                   -> { enabled }
// POST /api/telnet?enabled=true|false  (applied immediately, stored in NVS)
void setupTelnet();
void handleRouteTelnet()
{
    if (wm.server->method() == HTTP_POST)
    {
        String v = wm.server->arg("enabled");
        if (v != "true" && v != "false" && v != "1" && v != "0") {
            wm.server->send(400, "application/json", "{\"result\":\"failed - expected enabled=true/false\"}");
            return;
        }
        bool on = (v == "true" || v == "1");
        Preferences prefs;
        prefs.begin("telnet", false);
        prefs.putBool("enabled", on);
        prefs.end();
        if (on && !telnetEnabled) { telnetEnabled = true; setupTelnet(); }
        else if (!on && telnetEnabled) { telnet.stop(); telnetEnabled = false; LOGI("sys", "telnet stopped"); }
    }
    else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    StaticJsonDocument<96> json;
    if (wm.server->method() == HTTP_POST) json["result"] = "success";
    json["enabled"] = telnetEnabled;
    String out; serializeJson(json, out);
    wm.server->send(200, "application/json", out);
}
