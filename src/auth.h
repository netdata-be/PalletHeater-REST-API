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
            LOGW("http", "401 %s from %s: missing or invalid API key", wm.server->uri().c_str(), wm.server->client().remoteIP().toString().c_str());
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
            LOGW("http", "401 %s from %s: missing or invalid API key", wm.server->uri().c_str(), wm.server->client().remoteIP().toString().c_str());
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
            LOGI("sys", "API key rotated by %s (also the OTA password)", wm.server->client().remoteIP().toString().c_str());
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
