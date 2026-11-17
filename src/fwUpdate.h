#pragma once
// Firmware updates from GitHub Releases: GET/POST /api/firmware.
//
// The CI workflow (.github/workflows/firmware.yml) publishes a release for every
// tag vX.Y.Z with the OTA image as asset FW_ASSET. This module
//   - checks the latest release (api.github.com, releases/latest: no pre-releases
//     or drafts) 2 minutes after boot and then every 24 h, when auto check is on;
//   - downloads and installs it on request (never by itself), then restarts.
// HTTPS is verified against the Mozilla root CAs in certs/x509_crt_bundle.bin
// (scripts/gen_ca_bundle.py). The ESP-IDF checks the image (magic byte, checksum,
// SHA-256) before it is made bootable; a broken download is never activated.
//
// The work runs in its own task: a TLS connection blocks for seconds, and the
// web server (loop task) must keep answering status requests meanwhile.
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <NimBLEDevice.h>

#ifndef FW_REPO
#define FW_REPO "netdata-be/PalletHeater-REST-API"
#endif
#define FW_ASSET PC_FW_ASSET   // the old name PC_FW_ASSET_OLD (up to v1.0.0) is the fallback, see pc::firmwareAssetRank
static const unsigned long FW_FIRST_CHECK_MS = 2UL * 60UL * 1000UL;
static const unsigned long FW_CHECK_EVERY_MS = 24UL * 3600UL * 1000UL;

extern const uint8_t caBundle[] asm("_binary_certs_x509_crt_bundle_bin_start");

enum FwState { FW_IDLE, FW_CHECKING, FW_DOWNLOADING, FW_RESTARTING, FW_ERROR };
static const char* const FW_STATE_STR[] = {"idle", "checking", "downloading", "restarting", "error"};

// Written by the worker task. The strings only change in FW_CHECKING, and the
// route does not read them in that state.
static volatile int  fwState = FW_IDLE;
static volatile bool fwBusy = false;        // worker task running
static String        fwLatest, fwAssetUrl, fwReleaseUrl, fwPublished, fwError;
static volatile size_t fwAssetSize = 0, fwDone = 0, fwTotal = 0;
static unsigned long fwCheckedAt = 0;       // millis() of the last successful check, 0 = never
static bool          fwAutoCheck = true;
static String        fwLastInstall;         // "v1.0.0 -> v1.1.0", from NVS

static bool fwTimeValid() { return time(nullptr) > 1700000000; }   // TLS checks certificate dates

static void fwBeginHttps(HTTPClient& http, WiFiClientSecure& client, const String& url)
{
    client.setCACertBundle(caBundle);
    client.setTimeout(15);   // seconds
    http.setUserAgent("OpenPelletBridge-ESP32");
    http.setTimeout(15000);
    http.setConnectTimeout(10000);
    http.begin(client, url);
}

// Latest release from the GitHub API. Returns an error text, or "" when fine.
static String fwDoCheck()
{
    if (!fwTimeValid()) return "no valid time yet (NTP), needed for HTTPS";
    WiFiClientSecure client;
    HTTPClient http;
    http.useHTTP10(true);   // no chunked encoding: the JSON is parsed straight from the stream
    fwBeginHttps(http, client, "https://api.github.com/repos/" FW_REPO "/releases/latest");
    http.addHeader("Accept", "application/vnd.github+json");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // a renamed repository answers 301 to its new location
    int code = http.GET();
    if (code == 404) { http.end(); return "no release published yet"; }
    if (code != 200) {
        String e = code < 0 ? String("connection failed: ") + http.errorToString(code) : String("GitHub answered HTTP ") + code;
        http.end();
        return e;
    }
    StaticJsonDocument<256> filter;
    filter["tag_name"] = true;
    filter["html_url"] = true;
    filter["published_at"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    filter["assets"][0]["size"] = true;
    DynamicJsonDocument doc(4096);
    DeserializationError de = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (de) return String("invalid answer from GitHub: ") + de.c_str();
    const char* tag = doc["tag_name"] | "";
    if (!*tag) return "release without tag";
    String url; size_t size = 0; int best = 0;
    for (JsonObject a : doc["assets"].as<JsonArray>()) {
        int r = pc::firmwareAssetRank(a["name"] | "");
        if (r > best) { best = r; url = a["browser_download_url"] | ""; size = a["size"] | 0; }
    }
    fwLatest = tag;
    fwReleaseUrl = doc["html_url"] | "";
    fwPublished = doc["published_at"] | "";
    fwAssetUrl = url;
    fwAssetSize = size;
    fwCheckedAt = millis();
    if (!url.length()) return String("release ") + tag + " has no " FW_ASSET;
    return "";
}

static void fwPauseOthers(bool pause)
{
    otaActive = pause;   // pauses the sensor task (no stove bus traffic) and MQTT
    if (bleEnabled) { if (pause) NimBLEDevice::stopAdvertising(); else NimBLEDevice::startAdvertising(); }
}

// Download and flash the release found by fwDoCheck(). Returns an error text, or
// "" once the new image is in place (the caller restarts).
static String fwDoInstall()
{
    if (!fwAssetUrl.length()) return "no firmware asset known, check first";
    WiFiClientSecure client;
    HTTPClient http;
    fwBeginHttps(http, client, fwAssetUrl);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // github.com -> release-assets.githubusercontent.com
    int code = http.GET();
    if (code != 200) {
        String e = code < 0 ? String("download failed: ") + http.errorToString(code) : String("download answered HTTP ") + code;
        http.end();
        return e;
    }
    int len = http.getSize();
    if (len <= 0) { http.end(); return "download has no size"; }
    if (fwAssetSize && (size_t)len != fwAssetSize) { http.end(); return "download size differs from the release"; }
    fwTotal = len; fwDone = 0;
    fwPauseOthers(true);
    if (!Update.begin(len, U_FLASH)) {
        String e = String("begin failed: ") + Update.errorString();
        http.end(); fwPauseOthers(false);
        return e;
    }
    WiFiClient* s = http.getStreamPtr();
    static uint8_t buf[2048];
    unsigned long lastData = millis();
    String err;
    while (fwDone < fwTotal) {
        size_t avail = s->available();
        if (avail) {
            int n = s->readBytes(buf, avail < sizeof(buf) ? avail : sizeof(buf));
            if (n <= 0) continue;
            if (Update.write(buf, n) != (size_t)n) { err = String("write failed: ") + Update.errorString(); break; }
            fwDone += n;
            lastData = millis();
        } else if (!http.connected() && !s->available()) {
            err = "connection closed during download"; break;
        } else if (millis() - lastData > 20000) {
            err = "download stalled"; break;
        } else {
            delay(2);
        }
    }
    http.end();
    if (err.length()) { Update.abort(); fwPauseOthers(false); return err; }
    if (!Update.end(true)) { err = String("verify failed: ") + Update.errorString(); fwPauseOthers(false); return err; }
    return "";
}

static void fwTask(void* arg)
{
    bool install = arg != nullptr;
    fwState = FW_CHECKING;
    fwError = "";
    String err = fwDoCheck();
    if (!err.length() && install) {
        fwState = FW_DOWNLOADING;
        Serial.printf("Firmware update: %s -> %s\n", BUILD_VERSION, fwLatest.c_str());
        err = fwDoInstall();
        if (!err.length()) {
            Preferences p;
            p.begin("fwupd", false);
            p.putString("last", String(BUILD_VERSION) + " -> " + fwLatest);
            p.end();
            fwState = FW_RESTARTING;
            Serial.println("Firmware update: done, restarting");
            delay(1500);   // let a status poll see "restarting"
            ESP.restart();
        }
    }
    if (err.length()) { fwError = err; fwState = FW_ERROR; Serial.printf("Firmware update: %s\n", err.c_str()); }
    else fwState = FW_IDLE;
    fwBusy = false;
    vTaskDelete(nullptr);
}

static bool fwStart(bool install)
{
    if (fwBusy || otaActive) return false;
    fwBusy = true;
    // Stack: the TLS handshake (mbedTLS) needs a good 6 KB of it.
    if (xTaskCreatePinnedToCore(fwTask, "fwUpdate", 12288, install ? (void*)1 : nullptr, 1, nullptr, 1) != pdPASS) {
        fwBusy = false;
        fwError = "could not start the update task (memory)";
        fwState = FW_ERROR;
        return false;
    }
    return true;
}

void fwSetup()
{
    Preferences p;
    p.begin("fwupd", true);
    fwAutoCheck = p.getBool("auto", true);
    fwLastInstall = p.getString("last", "");
    p.end();
}

// From loop(): the automatic check.
void fwLoop()
{
    if (!fwAutoCheck || fwBusy || otaActive || WiFi.status() != WL_CONNECTED || !fwTimeValid()) return;
    static unsigned long lastTry = 0;
    unsigned long now = millis();
    unsigned long wait = fwCheckedAt ? FW_CHECK_EVERY_MS : (lastTry ? 60UL * 60UL * 1000UL : FW_FIRST_CHECK_MS);
    unsigned long since = fwCheckedAt ? now - fwCheckedAt : (lastTry ? now - lastTry : now);
    if (since < wait) return;
    lastTry = now;
    fwStart(false);   // a failed check retries after an hour
}

void handleRouteFirmware()
{
    StaticJsonDocument<1024> json;
    int code = 200;
    if (wm.server->method() == HTTP_POST) {
        String action = wm.server->arg("action");
        String err;
        if (wm.server->hasArg("autoCheck")) {
            int b = pc::parseOnOff(wm.server->arg("autoCheck").c_str());
            if (b < 0) err = "autoCheck must be true / false";
            else {
                fwAutoCheck = b == 1;
                Preferences p; p.begin("fwupd", false); p.putBool("auto", fwAutoCheck); p.end();
            }
        }
        if (!err.length() && action.length()) {
            if (action != "check" && action != "install") err = "action must be check or install";
            else if (fwBusy || otaActive) err = "an update or check is already running";
            else if (action == "install") {
                // Only when the last check found a newer release, or with force=true
                // (e.g. from a development build, whose version cannot be compared).
                bool force = pc::parseOnOff(wm.server->arg("force").c_str()) == 1;
                if (!fwAssetUrl.length()) err = "no release with firmware known, check first";
                else if (!force && !pc::updateAvailable(BUILD_VERSION, fwLatest.c_str()))
                    err = "no newer version (use force=true to install anyway)";
            }
            if (!err.length() && !fwStart(action == "install")) err = fwError.length() ? fwError : String("could not start");
        }
        if (err.length()) { json["result"] = "failed - " + err; code = err.startsWith("an update") ? 409 : 400; }
        else json["result"] = action.length() ? "started" : "success";
    } else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }

    int st = fwState;
    json["current"] = BUILD_VERSION;
    pc::Version v;
    json["devBuild"] = !pc::parseVersion(BUILD_VERSION, v) || v.ahead > 0 || v.dirty;
    json["repo"] = FW_REPO;
    json["stoveModel"] = BUILD_STOVE_MODEL;
    json["state"] = FW_STATE_STR[st];
    json["autoCheck"] = fwAutoCheck;
    if (st != FW_CHECKING && fwLatest.length()) {
        json["latest"] = fwLatest;
        json["published"] = fwPublished;
        json["releaseUrl"] = fwReleaseUrl;
        json["assetAvailable"] = fwAssetUrl.length() > 0;
        json["updateAvailable"] = fwAssetUrl.length() > 0 && pc::updateAvailable(BUILD_VERSION, fwLatest.c_str());
    } else {
        json["latest"] = nullptr;
        json["updateAvailable"] = false;
    }
    if (fwCheckedAt) json["checkedSecondsAgo"] = (millis() - fwCheckedAt) / 1000; else json["checkedSecondsAgo"] = nullptr;
    if (st == FW_DOWNLOADING || st == FW_RESTARTING) {
        JsonObject pr = json.createNestedObject("progress");
        pr["done"] = (unsigned)fwDone;
        pr["total"] = (unsigned)fwTotal;
    }
    if (st == FW_ERROR && fwError.length()) json["lastError"] = fwError; else json["lastError"] = nullptr;
    if (fwLastInstall.length()) json["lastInstall"] = fwLastInstall; else json["lastInstall"] = nullptr;
    String out; serializeJson(json, out);
    wm.server->send(code, "application/json", out);
}
