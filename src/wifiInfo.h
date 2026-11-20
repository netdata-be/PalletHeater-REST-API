#pragma once
// WiFi / network status and settings: GET / POST /api/wifi.
//
// The settings live in NVS "net" (shared with the BLE and telnet menus):
//   hostname  - mDNS / DHCP host name
//   netcfg    - "dhcp" or "static,<ip>,<gateway>,<netmask>,<dns>"
// They take effect after a restart (setup_wifi() reads them).
//
// Safety for static IP set from the web: the flag "netpend" is stored with the new
// configuration. After the restart the controller waits up to 5 minutes for any
// HTTP request (the web app polls every 5 s, so opening it on the new address is
// enough). Without one, nobody can reach it: it switches back to DHCP and restarts.
//
// Changing the WiFi network (POST ssid / password) is done live, with the same
// kind of safety: the old network, password and IP settings are kept in NVS
// ("oldssid", "oldpass", "oldcfg", "wifipend"). The new network uses DHCP. When the
// controller cannot connect within 25 s, or nobody reaches it within 5 minutes
// after it connected, it restores the old network and restarts.
// GET /api/wifi/scan lists the networks in range (asynchronous scan).
#include <constants.h>
#include <Preferences.h>
#include <esp_netif.h>

static const unsigned long NET_CONFIRM_MS = 300000;
static bool          netPendingConfirm = false;
static unsigned long netPendingSince = 0;

enum NetSwitchState { NS_IDLE, NS_START, NS_CONNECTING, NS_CONFIRM };
static NetSwitchState netSwitch = NS_IDLE;
static unsigned long  netSwitchAt = 0;
static String         netNewSsid, netNewPass, netSwitchMsg;
static const unsigned long NET_CONNECT_MS = 25000;
bool netWifiBusy() { return netSwitch != NS_IDLE; }   // main loop: no reconnect attempts meanwhile

static String netGet(const char* key, const char* def)
{
    Preferences p; p.begin("net", true);
    String v = p.getString(key, def);
    p.end();
    return v;
}

// Restore the previous network (and its IP settings) and restart
static void netRevertWifi(const char* why)
{
    Preferences p; p.begin("net", false);
    String os = p.getString("oldssid", ""), op = p.getString("oldpass", ""), oc = p.getString("oldcfg", "dhcp");
    p.putString("netcfg", oc); p.putBool("wifipend", false);
    p.putString("wifimsg", String(why) + " - back to " + os);
    p.end();
    LOGW("wifi", "%s - restoring '%s' and restarting", why, os.c_str());
    if (os.length()) { WiFi.disconnect(false); WiFi.begin(os.c_str(), op.c_str()); delay(300); }   // stores the old credentials
    restartWith("WiFi switch reverted");
}

// ── WiFi events: why the connection dropped ─────────────────────────────
// Registered before WiFi starts. A drop of a working connection is counted and
// logged as a warning; the failed attempts while reconnecting only at debug level.
// counters: liveLog.h (also used by metrics.h, which is included earlier)
// IPv6 (SLAAC on networks that offer it, always a link-local address): mDNS then also
// answers AAAA queries. Without it macOS waited 5 s for an AAAA answer on every new lookup
// of openpelletbridge.local. Global addresses are kept as they arrive (max 3).
static String ipv6Global[3];
static void netEventsSetup()
{

    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
        char a[48];
        snprintf(a, sizeof(a), IPV6STR, IPV62STR(info.got_ip6.ip6_info.ip));
        String s(a);
        bool linkLocal = s.startsWith("fe80");
        if (!linkLocal) {
            bool known = false;
            for (auto& g : ipv6Global) if (g == s) known = true;
            if (!known) for (auto& g : ipv6Global) if (!g.length()) { g = s; break; }
        }
        LOGI("wifi", "got IPv6 %s%s", a, linkLocal ? " (link-local)" : "");
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
        uint8_t r = info.wifi_sta_disconnected.reason;
        const char* name = WiFi.disconnectReasonName((wifi_err_reason_t)r);
        if (wifiWasUp) {
            wifiWasUp = false;
            wifiDisconnects++; wifiLastReason = r; wifiLastDisconnectMs = millis();
            LOGW("wifi", "connection lost: %s (%u)", name, r);
        } else {
            LOGD("wifi", "connect attempt failed: %s (%u)", name, r);
        }
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
        wifiWasUp = true;
        LOGI("wifi", "got IP %s", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
        // IPv6 link-local (and SLAAC global addresses after it): here, once the interface is
        // surely up (at "connected" it was not ready yet: esp_netif refused)
        esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_err_t e = nif ? esp_netif_create_ip6_linklocal(nif) : ESP_ERR_NOT_FOUND;
        if (e != ESP_OK) LOGW("wifi", "IPv6 could not be enabled: %s", esp_err_to_name(e));
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

// Called once in setup() after WiFi is up
void netSetup()
{
    Preferences p; p.begin("net", true);
    bool pend = p.getBool("netpend", false);
    bool wpend = p.getBool("wifipend", false);
    netSwitchMsg = p.getString("wifimsg", "");
    p.end();
    if (wpend) {   // restarted while a new network was not confirmed yet: keep waiting for a request
        netSwitch = NS_CONFIRM; netSwitchAt = millis(); netHttpSeen = false;
        if (WiFi.status() != WL_CONNECTED) netRevertWifi("new network not reachable after restart");
    }
    if (pend && netGet("netcfg", "dhcp").startsWith("static,")) {
        netPendingConfirm = true; netPendingSince = millis(); netHttpSeen = false;
        LOGI("wifi", "new static IP: waiting up to 5 min for an HTTP request, else back to DHCP");
    }
}

static void netSwitchLoop()
{
    unsigned long now = millis();
    switch (netSwitch) {
    case NS_IDLE: return;
    case NS_START:
        if (now < netSwitchAt) return;   // let the HTTP response go out first
        LOGI("wifi", "switching to '%s'", netNewSsid.c_str());
        WiFi.disconnect(false);
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);   // DHCP on the new network
        WiFi.begin(netNewSsid.c_str(), netNewPass.c_str());   // also stores the credentials
        netSwitch = NS_CONNECTING; netSwitchAt = now;
        return;
    case NS_CONNECTING:
        if (WiFi.status() == WL_CONNECTED) {
            LOGI("wifi", "connected to '%s', IP %s - waiting for an HTTP request", netNewSsid.c_str(), WiFi.localIP().toString().c_str());
            netSwitch = NS_CONFIRM; netSwitchAt = now; netHttpSeen = false;
            mdnsStart(WiFi.getHostname());
        } else if (now - netSwitchAt >= NET_CONNECT_MS) netRevertWifi("could not connect to the new network");
        return;
    case NS_CONFIRM:
        if (netHttpSeen) {
            Preferences p; p.begin("net", false);
            p.putBool("wifipend", false); p.remove("oldpass");
            p.putString("wifimsg", "switched to " + WiFi.SSID());
            p.end();
            netSwitchMsg = "switched to " + WiFi.SSID();
            netSwitch = NS_IDLE;
            LOGI("wifi", "new network confirmed");
        } else if (now - netSwitchAt >= NET_CONFIRM_MS) netRevertWifi("nobody reached the controller on the new network");
        return;
    }
}

void netLoop()
{
    netSwitchLoop();
    if (!netPendingConfirm) return;
    if (netHttpSeen) {
        Preferences p; p.begin("net", false); p.putBool("netpend", false); p.end();
        netPendingConfirm = false;
        LOGI("wifi", "static IP confirmed (HTTP request received)");
        return;
    }
    if (millis() - netPendingSince >= NET_CONFIRM_MS) {
        Preferences p; p.begin("net", false); p.putString("netcfg", "dhcp"); p.putBool("netpend", false); p.end();
        LOGW("wifi", "static IP not confirmed within 5 min - back to DHCP, restarting");
        delay(200);
        restartWith("static IP not confirmed - back to DHCP");
    }
}

void handleRouteWifi()
{
    StaticJsonDocument<1024> json;
    int code = 200;
    bool restart = false;
    if (wm.server->method() == HTTP_POST) {
        auto arg = [](const char* k) { String v = wm.server->arg(k); v.trim(); return v; };
        String err, hostname = netGet("hostname", "openpelletbridge"), netcfg = netGet("netcfg", "dhcp");
        bool pend = false;
        if (wm.server->hasArg("hostname")) {
            String h = arg("hostname");
            if (!pc::validHostname(h.c_str())) err = "hostname: 1 - 32 letters, digits or '-', not starting or ending with '-'";
            else hostname = h;
        }
        if (wm.server->hasArg("ssid")) {
            String ss = wm.server->arg("ssid"), pw = wm.server->arg("password");
            if (!pc::validSsid(ss.c_str())) err = "ssid must be 1 - 32 characters";
            else if (!pc::validWifiPassword(pw.c_str())) err = "password must be empty (open network) or 8 - 63 characters";
            else if (netSwitch != NS_IDLE) err = "a network change is already in progress";
            else {
                Preferences p; p.begin("net", false);
                p.putString("oldssid", WiFi.SSID()); p.putString("oldpass", WiFi.psk());
                p.putString("oldcfg", netcfg); p.putString("netcfg", "dhcp"); p.putBool("wifipend", true);
                p.remove("wifimsg");
                p.end();
                netNewSsid = ss; netNewPass = pw; netSwitch = NS_START; netSwitchAt = millis() + 1500;
                netSwitchMsg = "";
                json["result"] = "switching to " + ss + " - open the app again (http://" + String(WiFi.getHostname()) + ".local/) within 5 minutes";
                json["switching"] = true;
                String out; serializeJson(json, out);
                wm.server->sendHeader("Connection", "close");
                wm.server->send(200, "application/json", out);
                return;
            }
        }
        if (!err.length() && wm.server->hasArg("mode")) {
            String m = arg("mode");
            if (m == "dhcp") netcfg = "dhcp";
            else if (m == "static") {
                String ip = arg("ip"), nm = arg("netmask"), gw = arg("gateway"), dns = arg("dns");
                if (!dns.length()) dns = gw;
                const char* e = pc::checkStaticIp(ip.c_str(), nm.c_str(), gw.c_str(), dns.c_str());
                if (e) err = e;
                else {
                    String cfg = "static," + ip + "," + gw + "," + nm + "," + dns;   // order used by setup_wifi()
                    if (cfg != netcfg) pend = true;
                    netcfg = cfg;
                }
            } else err = "mode must be dhcp or static";
        }
        if (err.length()) { json["result"] = "failed - " + err; code = 400; }
        else {
            Preferences p; p.begin("net", false);
            p.putString("hostname", hostname);
            p.putString("netcfg", netcfg);
            if (pend) p.putBool("netpend", true);
            p.end();
            restart = pc::parseOnOff(arg("restart").c_str()) == 1;
            json["result"] = restart ? "success - restarting" : "success - takes effect after a restart";
        }
    } else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }

    bool up = WiFi.status() == WL_CONNECTED;
    json["connected"] = up;
    json["ssid"] = WiFi.SSID();
    json["bssid"] = up ? WiFi.BSSIDstr() : String("");
    json["channel"] = up ? WiFi.channel() : 0;
    int rssi = up ? WiFi.RSSI() : 0;
    json["rssi"] = rssi;
    json["quality"] = pc::rssiQuality(rssi);
    json["ip"] = WiFi.localIP().toString();
    {
        JsonArray v6 = json.createNestedArray("ipv6");
        String ll = WiFi.localIPv6().toString();
        if (ll.length() && ll != "0000:0000:0000:0000:0000:0000:0000:0000") v6.add(ll);
        for (auto& g : ipv6Global) if (g.length()) v6.add(g);
    }
    json["netmask"] = WiFi.subnetMask().toString();
    json["gateway"] = WiFi.gatewayIP().toString();
    json["dns"] = WiFi.dnsIP().toString();
    json["mac"] = WiFi.macAddress();
    json["hostname"] = WiFi.getHostname();
    json["sleep"] = WiFi.getSleep();
    String nc = netGet("netcfg", "dhcp");
    JsonObject saved = json.createNestedObject("saved");
    saved["hostname"] = netGet("hostname", "openpelletbridge");
    if (nc.startsWith("static,")) {
        saved["mode"] = "static";
        String r = nc.substring(7);
        int c1 = r.indexOf(','), c2 = r.indexOf(',', c1 + 1), c3 = r.indexOf(',', c2 + 1);
        if (c1 > 0 && c2 > c1 && c3 > c2) {
            saved["ip"] = r.substring(0, c1); saved["gateway"] = r.substring(c1 + 1, c2);
            saved["netmask"] = r.substring(c2 + 1, c3); saved["dns"] = r.substring(c3 + 1);
        }
    } else saved["mode"] = "dhcp";
    // What runs now: the saved mode from the last boot (a change needs a restart)
    json["restartRequired"] = String(WiFi.getHostname()) != saved["hostname"].as<String>()
        || (saved["mode"] == "static" ? WiFi.localIP().toString() != saved["ip"].as<String>() : false);
    if (netPendingConfirm) json["confirmSecondsLeft"] = (NET_CONFIRM_MS - (millis() - netPendingSince)) / 1000;
    if (netSwitch == NS_CONFIRM) json["confirmSecondsLeft"] = (NET_CONFIRM_MS - (millis() - netSwitchAt)) / 1000;
    if (netSwitchMsg.length()) json["lastChange"] = netSwitchMsg;
    json["disconnects"] = (uint32_t)wifiDisconnects;   // drops of a working connection since the start
    if (wifiLastDisconnectMs) {
        JsonObject ld = json.createNestedObject("lastDisconnect");
        ld["reason"] = WiFi.disconnectReasonName((wifi_err_reason_t)wifiLastReason);
        ld["code"] = (uint8_t)wifiLastReason;
        ld["secondsAgo"] = (millis() - wifiLastDisconnectMs) / 1000;
    } else json["lastDisconnect"] = nullptr;
    String out; serializeJson(json, out);
    if (restart) wm.server->sendHeader("Connection", "close");
    wm.server->send(code, "application/json", out);
    if (restart) { delay(500); restartWith("network settings changed"); }
}

// GET /api/wifi/scan -> {"scanning":true} while the scan runs (a few seconds), then
// {"scanning":false,"networks":[{"ssid","rssi","quality","channel","secure"}]} (strongest
// first, hidden networks and duplicates left out). ?refresh=true starts a new scan.
void handleRouteWifiScan()
{
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    int n = WiFi.scanComplete();
    bool refresh = pc::parseOnOff(wm.server->arg("refresh").c_str()) == 1;
    if (n == WIFI_SCAN_FAILED || (refresh && n != WIFI_SCAN_RUNNING)) {
        WiFi.scanDelete();
        WiFi.scanNetworks(true, false);   // async, no hidden networks
        n = WIFI_SCAN_RUNNING;
    }
    if (n == WIFI_SCAN_RUNNING) { wm.server->send(200, "application/json", "{\"scanning\":true}"); return; }
    DynamicJsonDocument json(4096);
    json["scanning"] = false;
    JsonArray arr = json.createNestedArray("networks");
    for (int i = 0; i < n; i++) {
        String ss = WiFi.SSID(i);
        if (!ss.length()) continue;
        bool dup = false;   // the same network on several access points: keep the strongest
        for (JsonObject o : arr) if (o["ssid"] == ss) { dup = true; if (WiFi.RSSI(i) > (int)o["rssi"]) { o["rssi"] = WiFi.RSSI(i); o["quality"] = pc::rssiQuality(WiFi.RSSI(i)); } }
        if (dup) continue;
        JsonObject o = arr.createNestedObject();
        o["ssid"] = ss; o["rssi"] = WiFi.RSSI(i); o["quality"] = pc::rssiQuality(WiFi.RSSI(i));
        o["channel"] = WiFi.channel(i); o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        if (arr.size() >= 25) break;
    }
    String out; serializeJson(json, out);
    wm.server->send(200, "application/json", out);
}
