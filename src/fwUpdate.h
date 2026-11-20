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

// Progress of an install in NVS ("fwupd" / "prog"): survives a power cut, so a hang
// during an install can still be traced after the controller was powered off and on.
// Removed when the install finished, "failed: ..." when it failed.
static String fwLastAttempt;   // read at the start: what the previous unfinished / failed install did
static void fwProgress(const String& what)
{
    char b[160];
    snprintf(b, sizeof(b), "%s (free heap %u KB, largest block %u KB, WiFi %d dBm, uptime %lu s)", what.c_str(),
             (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024), (int)WiFi.RSSI(), millis() / 1000);
    Preferences p;
    p.begin("fwupd", false);
    p.putString("prog", b);
    p.end();
    LOGD("update", "progress: %s", b);
}
static void fwProgressClear() { Preferences p; p.begin("fwupd", false); p.remove("prog"); p.end(); }
// HTTPS (mbedTLS) needs a good 40 KB, with a block of about 17 KB for its buffers
static String fwMemoryCheck()
{
    uint32_t freeHeap = ESP.getFreeHeap(), block = ESP.getMaxAllocHeap();
    if (freeHeap >= 45000 && block >= 20000) return "";
    char b[140];
    snprintf(b, sizeof(b), "not enough free memory for HTTPS (free %u KB, largest block %u KB; needs 45 / 20 KB) - restart the controller and try again",
             (unsigned)(freeHeap / 1024), (unsigned)(block / 1024));
    return b;
}

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
    if (!fwTimeValid()) return "no valid time yet (NTP), needed to check the HTTPS certificate";
    String mem = fwMemoryCheck();
    if (mem.length()) return mem;
    static const char* apiUrl = "https://api.github.com/repos/" FW_REPO "/releases/latest";
    LOGD("update", "GET %s (free heap %u bytes, largest block %u)", apiUrl, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    WiFiClientSecure client;
    HTTPClient http;
    http.useHTTP10(true);   // no chunked encoding: the JSON is parsed straight from the stream
    fwBeginHttps(http, client, apiUrl);
    http.addHeader("Accept", "application/vnd.github+json");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // a renamed repository answers 301 to its new location
    unsigned long t0 = millis();
    int code = http.GET();
    LOGD("update", "GitHub answered %d (%s) in %lu ms (TLS connection included)", code,
         code < 0 ? http.errorToString(code).c_str() : "HTTP", millis() - t0);
    if (code == 404) { http.end(); return "no release published yet on github.com/" FW_REPO " (pre-releases and drafts do not count)"; }
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
    String assetUrl, assetName; size_t size = 0; int best = 0;
    for (JsonObject a : doc["assets"].as<JsonArray>()) {
        const char* n = a["name"] | "";
        int r = pc::firmwareAssetRank(n);
        LOGD("update", "release %s asset %s (%u bytes)%s", tag, n, (unsigned)(a["size"] | 0),
             r == 2 ? " - firmware image" : r == 1 ? " - firmware image (old name)" : "");
        if (r > best) { best = r; assetUrl = a["browser_download_url"] | ""; size = a["size"] | 0; assetName = n; }
    }
    LOGI("update", "latest release on GitHub: %s, published %s, %s", tag, (const char*)(doc["published_at"] | "?"),
         assetName.length() ? (assetName + " " + String(size / 1024) + " KB").c_str() : "no firmware image");
    const String& url = assetUrl;
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
    String mem = fwMemoryCheck();
    if (mem.length()) return mem;
    fwProgress(String("install ") + BUILD_VERSION + " -> " + fwLatest + ": connecting to GitHub");
    LOGD("update", "download %s", fwAssetUrl.c_str());
    WiFiClientSecure client;
    HTTPClient http;
    fwBeginHttps(http, client, fwAssetUrl);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // github.com -> release-assets.githubusercontent.com
    unsigned long t0 = millis();
    int code = http.GET();
    LOGD("update", "download answered %d after %lu ms (redirect to the asset host included)", code, millis() - t0);
    if (code != 200) {
        String e = code < 0 ? String("download failed: ") + http.errorToString(code) : String("download answered HTTP ") + code;
        http.end();
        return e;
    }
    int len = http.getSize();
    if (len <= 0) { http.end(); return "download has no size"; }
    if (fwAssetSize && (size_t)len != fwAssetSize) { http.end(); return String("download size ") + len + " differs from the release (" + (unsigned)fwAssetSize + " bytes)"; }
    LOGI("update", "downloading %d KB into the free OTA partition (%u KB)", len / 1024, (unsigned)(ESP.getFreeSketchSpace() / 1024));
    fwProgress(String("install ") + BUILD_VERSION + " -> " + fwLatest + ": downloading 0%");
    fwTotal = len; fwDone = 0;
    int lastPct = 0;
    fwPauseOthers(true);
    if (!Update.begin(len, U_FLASH)) {
        String e = String("begin failed: ") + Update.errorString();
        http.end(); fwPauseOthers(false);
        return e;
    }
    WiFiClient* s = http.getStreamPtr();
    uint8_t buf[2048];   // on the stack of the update task (12 KB), not 2 KB of RAM all the time
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
            int pct = (int)(fwDone * 100 / fwTotal);
            if (pct / 10 > lastPct / 10) {
                LOGI("update", "%d%% (%u KB, %lu KB/s, free heap %u KB)", pct, (unsigned)(fwDone / 1024), (unsigned long)(fwDone / 1024 * 1000 / (millis() - t0 + 1)), (unsigned)(ESP.getFreeHeap() / 1024));
                lastPct = pct;
                fwProgress(String("install ") + BUILD_VERSION + " -> " + fwLatest + ": downloading " + pct + "%");
            }
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
    if (!Update.end(true)) { err = String("image check failed: ") + Update.errorString(); fwPauseOthers(false); return err; }
    LOGI("update", "download complete: %u bytes in %lu s, image verified and marked bootable", (unsigned)fwDone, (millis() - t0) / 1000);
    return "";
}

static void fwTask(void* arg)
{
    bool install = arg != nullptr;
    fwState = FW_CHECKING;
    fwError = "";
    // one TLS connection at a time: let a running HTTPS session (often the request that
    // started this check) finish first; HTTPS accepts no new one while fwBusy is set
    // (it also hands back its reserved TLS buffers while fwBusy is set)
    for (int i = 0; i < 100 && (httpsSessionActive || httpsHoldsTls); i++) vTaskDelay(pdMS_TO_TICKS(100));
    LOGI("update", "%s: checking GitHub (%s) for a newer release than %s", install ? "install" : "update check", FW_REPO, BUILD_VERSION);
    String err = fwDoCheck();
    if (!err.length()) {
        pc::Version cur;
        bool parsed = pc::parseVersion(BUILD_VERSION, cur);
        if (pc::updateAvailable(BUILD_VERSION, fwLatest.c_str()))
            LOGI("update", "update available: %s is newer than %s", fwLatest.c_str(), BUILD_VERSION);
        else if (!parsed)
            LOGI("update", "no update offered: this is a development build (%s, not a release tag); %s can be installed with force", BUILD_VERSION, fwLatest.c_str());
        else if (cur.ahead > 0 || cur.dirty)
            LOGI("update", "no update: %s is a development build (%d commit(s) after %s%s), newer than the latest release %s; installing %s would go back to that release",
                 BUILD_VERSION, cur.ahead, fwLatest.c_str(), cur.dirty ? ", with uncommitted changes" : "", fwLatest.c_str(), fwLatest.c_str());
        else
            LOGI("update", "up to date: %s is the latest release", BUILD_VERSION);
    }
    if (!err.length() && install) {
        fwState = FW_DOWNLOADING;
        LOGI("update", "installing %s -> %s", BUILD_VERSION, fwLatest.c_str());
        err = fwDoInstall();
        if (!err.length()) {
            Preferences p;
            p.begin("fwupd", false);
            p.putString("last", String(BUILD_VERSION) + " -> " + fwLatest);
            p.end();
            fwProgressClear();
            fwState = FW_RESTARTING;
            LOGI("update", "installed, restarting");
            delay(1500);   // let a status poll see "restarting"
            restartWith("firmware update from GitHub");
        }
    }
    if (err.length()) {
        fwError = err; fwState = FW_ERROR;
        if (install) { LOGE("update", "install failed: %s", err.c_str()); fwProgress("failed: " + err); }
        else LOGW("update", "update check failed: %s", err.c_str());
    }
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
    fwLastAttempt = p.getString("prog", "");
    p.end();
    if (fwLastAttempt.length() && !fwLastAttempt.startsWith("failed"))
        LOGW("update", "the previous GitHub install did not finish (controller restarted or powered off during it), last step: %s", fwLastAttempt.c_str());
}

// From loop(): the automatic check.
void fwLoop()
{
    if (!fwAutoCheck || fwBusy || otaActive || httpsSessionActive || WiFi.status() != WL_CONNECTED || !fwTimeValid()) return;
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
    StaticJsonDocument<1536> json;
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
    bool parsed = pc::parseVersion(BUILD_VERSION, v);
    json["devBuild"] = !parsed || v.ahead > 0 || v.dirty;
    if (parsed) { json["ahead"] = v.ahead; json["dirty"] = v.dirty; }
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
        // what installing the latest release would do: update (newer), reinstall (same),
        // downgrade (older than this build, e.g. a development build after the tag),
        // unknown (this build has no release version to compare with)
        pc::Version l;
        if (!parsed || !pc::parseVersion(fwLatest.c_str(), l)) json["install"] = "unknown";
        else { int c = pc::compareVersion(l, v); json["install"] = c > 0 ? "update" : c == 0 && !v.dirty ? "reinstall" : "downgrade"; }
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
    // an install that did not finish or failed: its last step, with memory and WiFi at that time
    if (fwLastAttempt.length()) json["lastAttempt"] = fwLastAttempt; else json["lastAttempt"] = nullptr;
    json["freeHeap"] = ESP.getFreeHeap();
    json["largestBlock"] = ESP.getMaxAllocHeap();
    String out; serializeJson(json, out);
    wm.server->send(code, "application/json", out);
}
