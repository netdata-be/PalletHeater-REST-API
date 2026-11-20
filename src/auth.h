#pragma once
// Optional API key for the HTTP interface.
//
// - A random 128-bit key (32 hex chars) is generated on first boot and stored in
//   NVS ("auth"/"key"); an existing key is reused.
// - Enforcement is optional ("auth"/"required", default off). When required,
//   every route except the public ones (see main.cpp) needs the key in a header:
//     X-API-Key: <key>      or      Authorization: Bearer <key>
// - GET/POST /api/auth manages the setting; the key is only returned to callers
//   that are authorised (or while the key is not required).
#include <constants.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <functional>
#include <esp_system.h>

static String apiKey;
static bool   apiKeyRequired = false;

// HTTPS (https.h) passes each request on to this HTTP server over 127.0.0.1; it sets the
// address of the TLS client here while it does, so logs show the real client
volatile uint32_t httpsPeer = 0;
static char httpsPeerText[48] = "";        // the TLS client as text (also IPv6), empty when none
// The two relays in front of this server (https.h: TLS, http6.h: plain HTTP over IPv6) both
// connect from 127.0.0.1; the local port of their connection tells which relay, and so which
// real client, a request comes from
struct ProxyPeer { volatile uint16_t port; bool tls; char text[48]; };
static ProxyPeer proxyPeers[2];            // 0 = HTTPS, 1 = HTTP over IPv6
static void proxyPeerSet(int slot, int lfd, bool tls, const char* who)
{
    struct sockaddr_in a = {}; socklen_t l = sizeof(a);
    uint16_t port = lwip_getsockname(lfd, (struct sockaddr*)&a, &l) == 0 ? ntohs(a.sin_port) : 0;
    proxyPeers[slot].tls = tls;
    strlcpy(proxyPeers[slot].text, who, sizeof(proxyPeers[slot].text));
    proxyPeers[slot].port = port;
}
static void proxyPeerClear(int slot) { proxyPeers[slot].port = 0; }
static const ProxyPeer* proxyPeerOfRequest()   // the relay behind the current request, or nullptr
{
    WiFiClient c = wm.server->client();
    if (c.remoteIP() != IPAddress(127, 0, 0, 1)) return nullptr;
    uint16_t port = c.remotePort();
    for (auto& p : proxyPeers) if (p.port && p.port == port) return &p;
    return nullptr;
}

// JSON answer, serialized into a String of exactly the size it needs (replaces the old fixed
// 9 KB global buffer, which took 9 KB of RAM all the time)
template <typename Doc> static void sendJson(int code, const Doc& doc)
{
    String out;
    out.reserve(measureJson(doc) + 1);
    serializeJson(doc, out);
    wm.server->send(code, "application/json", out);
}
static String clientIp()
{
    IPAddress ip = wm.server->client().remoteIP();
    if (ip == IPAddress(127, 0, 0, 1)) {
        const ProxyPeer* p = proxyPeerOfRequest();
        if (p) return String(p->text) + (p->tls ? " (https)" : " (IPv6)");
    }
    return ip.toString();
}

static String generateApiKey()
{
    char buf[33];
    for (int i = 0; i < 16; i++) snprintf(buf + i * 2, 3, "%02x", (unsigned)(esp_random() & 0xFF));
    return String(buf);
}

void authInit()
{
    Preferences prefs;
    prefs.begin("auth", false);
    apiKey = prefs.getString("key", "");
    if (apiKey.length() != 32) {
        apiKey = generateApiKey();
        prefs.putString("key", apiKey);
        LOGI("sys", "API key generated");
    }
    apiKeyRequired = prefs.getBool("required", false);
    prefs.end();
    LOGI("sys", "API key %.4s… (%s)", apiKey.c_str(), apiKeyRequired ? "required" : "optional");   // never the whole key in the log
}

// The web server only exposes headers it was told to collect (call before begin())
void authCollectHeaders()
{
    // Content-Type: /api/update must only touch the upload for multipart requests (httpUpdate.h)
    // Accept-Encoding: the web app and spec are only served gzipped (routesBasic.h)
    static const char* keys[] = { "X-API-Key", "Authorization", "Content-Type", "Accept-Encoding" };
    wm.server->collectHeaders(keys, 4);
}

static bool constantTimeEquals(const String& a, const String& b)
{
    if (a.length() != b.length()) return false;
    uint8_t diff = 0;
    for (unsigned int i = 0; i < a.length(); i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

// true when the request carries the right key (header only, never a query parameter)
static bool requestHasValidKey()
{
    String k = wm.server->header("X-API-Key");
    if (k.length() == 0) {
        String a = wm.server->header("Authorization");
        if (a.startsWith("Bearer ")) k = a.substring(7);
    }
    k.trim();
    return k.length() && constantTimeEquals(k, apiKey);
}

// Bluetooth OTA: the key sent after the size (bleUart.h)
static bool bleOtaKeyOk(const uint8_t* key, size_t len)
{
    String k;
    k.reserve(len);
    for (size_t i = 0; i < len; i++) k += (char)key[i];
    k.trim();
    return k.length() && constantTimeEquals(k, apiKey);
}

bool authOk() { return !apiKeyRequired || requestHasValidKey(); }

static const char* httpMethodName(HTTPMethod m)
{
    switch (m) {
    case HTTP_GET: return "GET"; case HTTP_POST: return "POST"; case HTTP_PUT: return "PUT";
    case HTTP_DELETE: return "DELETE"; case HTTP_OPTIONS: return "OPTIONS"; case HTTP_HEAD: return "HEAD";
    default: return "?";
    }
}
// Breadcrumb for a web request: method and URI while the handler runs (liveLog.h)
static void crumbRequest()
{
    httpStalled = false;
    crumbf("http %s %s", httpMethodName(wm.server->method()), wm.server->uri().c_str());
}
// Wrap a route handler so the breadcrumb shows its request (public routes; authed() does it too)
std::function<void()> crumbed(std::function<void()> fn)
{
    return [fn]() { crumbRequest(); fn(); crumb("http: request done"); };
}

// Wrap a route handler so it is only executed with a valid key (when required)
std::function<void()> authed(std::function<void()> fn)
{
    return [fn]() {
        crumbRequest();
        if (!authOk()) {
            wm.server->sendHeader("WWW-Authenticate", "Bearer");
            LOGW("http", "401 %s from %s: missing or invalid API key", wm.server->uri().c_str(), clientIp().c_str());
            wm.server->send(401, "application/json", "{\"error\":\"Missing or invalid API key (X-API-Key header)\"}");
            return;
        }
        uint32_t t0 = millis();
        fn();
        crumb("http: request done");
        uint32_t d = millis() - t0;
        const String& uri = wm.server->uri();
        if (d > 5000) LOGW("http", "slow request: %s took %lu ms", uri.c_str(), (unsigned long)d);
        else if (uri != "/api/log" && uri != "/api/busmon") LOGD("http", "%s done in %lu ms", uri.c_str(), (unsigned long)d);
    };
}

// GET  /api/auth                       -> { required, key? }
// POST /api/auth?required=true|false   -> enable / disable enforcement
// POST /api/auth?rotate=true           -> generate a new key
// Changing anything while the key is required needs the current key.
void handleRouteAuth()
{
    bool authorised = authOk();
    if (wm.server->method() == HTTP_POST)
    {
        if (!authorised) {
            LOGW("http", "401 %s from %s: missing or invalid API key", wm.server->uri().c_str(), clientIp().c_str());
            wm.server->send(401, "application/json", "{\"error\":\"Missing or invalid API key (X-API-Key header)\"}");
            return;
        }
        Preferences prefs;
        prefs.begin("auth", false);
        if (wm.server->hasArg("required")) {
            String v = wm.server->arg("required");
            if (v != "true" && v != "false") {
                prefs.end();
                wm.server->send(400, "application/json", "{\"result\":\"failed - expected required=true/false\"}");
                return;
            }
            apiKeyRequired = (v == "true");
            prefs.putBool("required", apiKeyRequired);
        }
        if (wm.server->arg("rotate") == "true") {
            apiKey = generateApiKey();
            prefs.putString("key", apiKey);
            ArduinoOTA.setPassword(apiKey.c_str());   // the OTA password is the API key (ota.h)
            LOGI("sys", "API key rotated by %s (also the OTA password)", clientIp().c_str());
        }
        prefs.end();
    }
    else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }

    StaticJsonDocument<192> json;
    if (wm.server->method() == HTTP_POST) json["result"] = "success";
    json["required"] = apiKeyRequired;
    if (authorised) json["key"] = apiKey;   // never leak the key to unauthorised callers
    String out; serializeJson(json, out);
    wm.server->send(200, "application/json", out);
}
