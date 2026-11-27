#pragma once
// HTTP routes for controller settings: /api/bluetooth and /api/telnet.
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

// GET  /api/bluetooth                     -> { enabled, active, restartRequired }
// POST /api/bluetooth?enabled=true|false[&restart=true]
// The setting is stored in NVS and applied at the next boot (BLE is started or
// skipped in setup()), so a change requires a restart of the controller.
// Bluetooth setting in NVS (applied at the next start); shared with the telnet menu
static bool bleSavedEnabled() { Preferences p; p.begin("ble", true); bool on = p.getBool("enabled", true); p.end(); return on; }
static void bleSaveEnabled(bool on, const char* who)
{
    Preferences p; p.begin("ble", false); p.putBool("enabled", on); p.end();
    LOGI("sys", "Bluetooth %s by %s (applied after a restart)", on ? "on" : "off", who);
}
// Telnet on / off, stored in NVS and applied at once (switching off ends a session)
void setupTelnet();
void telnetStop();
static void telnetSetEnabled(bool on)
{
    Preferences prefs; prefs.begin("telnet", false); prefs.putBool("enabled", on); prefs.end();
    if (on && !telnetEnabled) { telnetEnabled = true; setupTelnet(); }
    else if (!on && telnetEnabled) { telnetStop(); telnetEnabled = false; LOGI("sys", "telnet stopped"); }
}

void handleRouteBluetooth()
{
    if (wm.server->method() == HTTP_POST)
    {
        String v = wm.server->arg("enabled");
        if (v != "true" && v != "false" && v != "1" && v != "0") {
            wm.server->send(400, "application/json; charset=utf-8", "{\"result\":\"failed - expected enabled=true/false\"}");
            return;
        }
        if (!OPB_BLUETOOTH && (v == "true" || v == "1")) {
            wm.server->send(409, "application/json; charset=utf-8", "{\"result\":\"failed - this firmware is built without Bluetooth\"}");
            return;
        }
        bleSaveEnabled(v == "true" || v == "1", clientIp().c_str());
    }
    else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }

    bool saved = bleSavedEnabled();
    bool restart = OPB_BLUETOOTH && wm.server->method() == HTTP_POST && wm.server->arg("restart") == "true" && saved != bleEnabled;

    StaticJsonDocument<192> json;
    if (wm.server->method() == HTTP_POST) json["result"] = restart ? "success - restarting" : "success";
    json["available"] = OPB_BLUETOOTH != 0;   // false: built without Bluetooth (-DOPB_BLUETOOTH=0)
    json["enabled"] = OPB_BLUETOOTH && saved;
    json["active"] = bleEnabled;
    json["restartRequired"] = OPB_BLUETOOTH && saved != bleEnabled;
    String out; serializeJson(json, out);
    wm.server->send(200, "application/json; charset=utf-8", out);
    if (restart) { delay(300); restartWith("Bluetooth setting changed"); }
}

// GET  /api/telnet                   -> { enabled }
// POST /api/telnet?enabled=true|false  (applied immediately, stored in NVS)
void handleRouteTelnet()
{
    if (wm.server->method() == HTTP_POST)
    {
        String v = wm.server->arg("enabled");
        if (v != "true" && v != "false" && v != "1" && v != "0") {
            wm.server->send(400, "application/json; charset=utf-8", "{\"result\":\"failed - expected enabled=true/false\"}");
            return;
        }
        telnetSetEnabled(v == "true" || v == "1");
    }
    else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    StaticJsonDocument<96> json;
    if (wm.server->method() == HTTP_POST) json["result"] = "success";
    json["enabled"] = telnetEnabled;
    String out; serializeJson(json, out);
    wm.server->send(200, "application/json; charset=utf-8", out);
}
