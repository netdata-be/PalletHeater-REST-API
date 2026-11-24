#pragma once
// Firmware updates from GitHub Releases: GET/POST /api/firmware.
//
// The CI workflow (.github/workflows/firmware.yml) publishes a release for every
// tag vX.Y.Z with the OTA image as asset FW_ASSET. This module
//   - checks the latest release (api.github.com, releases/latest: no pre-releases
//     or drafts) 2 minutes after boot and then every 24 h, when auto check is on;
//   - downloads and installs it on request (never by itself), then restarts.
// HTTPS is verified against the root CAs in certs/x509_crt_bundle.bin: GitHub's and a few
// more as margin, listed in certs/ca_roots.pem (scripts/gen_ca_bundle.py). The ESP-IDF checks the image (magic byte, checksum,
// SHA-256) before it is made bootable; a broken download is never activated.
//
// The work runs in its own task: a TLS connection blocks for seconds, and the
// web server (loop task) must keep answering status requests meanwhile.
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_bt.h>
#include <lwip/sockets.h>
#include "fwBootPage.h"   // status page of an install (GET /update)

#ifndef FW_REPO
#define FW_REPO "netdata-be/PalletHeater-REST-API"
#endif
#define FW_ASSET PC_FW_ASSET   // the old name PC_FW_ASSET_OLD (up to v1.0.0) is the fallback, see pc::firmwareAssetRank
static const unsigned long FW_FIRST_CHECK_MS = 2UL * 60UL * 1000UL;   // HTTPS off: first check after start-up
static const unsigned long FW_BOOT_WAIT_MS = 90UL * 1000UL;           // HTTPS on: start-up check only within this time
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

void otaLastInstallChanged(const String& last) { fwLastInstall = last; }   // otaVerify.h, on confirming a new firmware
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
// HTTPS (mbedTLS) needs a good 40 KB, with a block of about 17 KB for its buffers: enough for
// the short update check. The download needs more: the TLS session stays open for a minute or
// more while the WiFi driver needs receive buffers (1.6 KB each) next to it. Started with 88 KB
// free it fell to 21 KB (largest block 2 KB) and the WiFi driver had no receive buffers left:
// "download stalled" (24/11/2026). So the install pauses the rest first and waits for this.
static const uint32_t FW_CHECK_FREE   = 45000, FW_CHECK_BLOCK   = 20000;
static const uint32_t FW_INSTALL_FREE = 70000, FW_INSTALL_BLOCK = 32000;
static const uint32_t FW_INSTALL_WAIT_MS = 15000;   // waiting for the memory after the pause
static String fwHeapText()
{
    char b[64];
    snprintf(b, sizeof(b), "free heap %u KB, largest block %u KB", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));
    return b;
}
static String fwMemoryCheck(bool install = false)
{
    uint32_t needFree = install ? FW_INSTALL_FREE : FW_CHECK_FREE, needBlock = install ? FW_INSTALL_BLOCK : FW_CHECK_BLOCK;
    uint32_t freeHeap = ESP.getFreeHeap(), block = ESP.getMaxAllocHeap();
    if (freeHeap >= needFree && block >= needBlock) return "";
    char b[180];
    snprintf(b, sizeof(b), "not enough free memory for %s (free %u KB, largest block %u KB; needs %u / %u KB) - restart the controller and try again",
             install ? "the download" : "HTTPS", (unsigned)(freeHeap / 1024), (unsigned)(block / 1024), (unsigned)(needFree / 1024), (unsigned)(needBlock / 1024));
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
    // DNS first, separately: HTTPClient reports every failed connection as "connection refused"
    IPAddress ip;
    if (!WiFi.hostByName("api.github.com", ip)) { http.end(); return String("DNS lookup of api.github.com failed (") + (millis() - t0) + " ms)"; }
    LOGD("update", "api.github.com = %s (DNS %lu ms)", ip.toString().c_str(), millis() - t0);
    int code = http.GET();
    LOGD("update", "GitHub answered %d (%s) in %lu ms (TLS connection included)", code,
         code < 0 ? http.errorToString(code).c_str() : "HTTP", millis() - t0);
    if (code < 0) {
        char tls[100] = ""; int te = client.lastError(tls, sizeof(tls));
        String e = String("connection to api.github.com (") + ip.toString() + ") failed after " + (millis() - t0) + " ms" +
                   (te ? String(": TLS error -0x") + String(-te, HEX) + " " + tls : String(": no TLS error (TCP connect or timeout)")) +
                   ", free heap " + (ESP.getFreeHeap() / 1024) + " KB, largest block " + (ESP.getMaxAllocHeap() / 1024) + " KB";
        http.end();
        return e;
    }
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

// Install after the restart (fwBootInstall): the step, for the status page (fwBootPage.h)
enum FwBootPhase { FWB_NONE, FWB_CONNECTING, FWB_DOWNLOADING, FWB_INSTALLING, FWB_RESTARTING, FWB_FAILED };
static const char* const FWB_STR[] = {"none", "connecting", "downloading", "installing", "restarting", "failed"};
static volatile int fwBootPhase = FWB_NONE;

// Download and flash the release found by fwDoCheck(), from fwBootInstall(). Returns an error
// text, or "" once the new image is in place (the caller restarts).
static String fwDoInstall()
{
    if (!fwAssetUrl.length()) return "no firmware asset known, check first";
    // runs right after start-up (fwBootInstall): nothing else is running yet; the download must
    // still have its margin (see FW_INSTALL_FREE)
    unsigned long w0 = millis();
    while (millis() - w0 < FW_INSTALL_WAIT_MS && fwMemoryCheck(true).length()) vTaskDelay(pdMS_TO_TICKS(500));
    String mem = fwMemoryCheck(true);
    if (mem.length()) return mem;
    LOGI("update", "memory for the download: %s", fwHeapText().c_str());
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
    fwBootPhase = FWB_DOWNLOADING;
    int lastPct = 0;
    otaConfirmBeforeUpdate();   // a firmware on trial is confirmed first (otaVerify.h)
    if (!Update.begin(len, U_FLASH)) {
        String e = String("begin failed: ") + Update.errorString();
        http.end();
        return e;
    }
    WiFiClient* s = http.getStreamPtr();
    uint8_t buf[2048];   // on the stack of the update task (12 KB), not 2 KB of RAM all the time
    unsigned long lastData = millis(), dl0 = millis(), lastLine = millis(), lastSample = 0;
    // memory during the download: lowest per log line and over the whole download, sampled every
    // 250 ms (getMaxAllocHeap walks the heap); a line every 5 s shows where it goes
    uint32_t minFree = UINT32_MAX, minBlock = UINT32_MAX, allMinFree = UINT32_MAX, allMinBlock = UINT32_MAX;
    size_t lineDone = 0;
    String err;
    while (fwDone < fwTotal) {
        esp_task_wdt_reset();   // runs in setup() (fwBootInstall); no effect in a task that is not watched
        unsigned long now = millis();
        if (now - lastSample >= 250) {
            lastSample = now;
            uint32_t f = ESP.getFreeHeap(), b = ESP.getMaxAllocHeap();
            if (f < minFree) minFree = f;
            if (b < minBlock) minBlock = b;
            if (f < allMinFree) allMinFree = f;
            if (b < allMinBlock) allMinBlock = b;
        }
        if (now - lastLine >= 5000) {
            LOGI("update", "%u%% (%u / %u KB, %lu KB/s) | %s | lowest in the last %lu s: %u KB free, block %u KB",
                 (unsigned)(fwDone * 100 / fwTotal), (unsigned)(fwDone / 1024), (unsigned)(fwTotal / 1024),
                 (unsigned long)((fwDone - lineDone) / 1024 * 1000 / (now - lastLine)), fwHeapText().c_str(),
                 (now - lastLine) / 1000, (unsigned)(minFree / 1024), (unsigned)(minBlock / 1024));
            lastLine = now; lineDone = fwDone; minFree = minBlock = UINT32_MAX;
        }
        size_t avail = s->available();
        if (avail) {
            int n = s->readBytes(buf, avail < sizeof(buf) ? avail : sizeof(buf));
            if (n <= 0) continue;
            if (Update.write(buf, n) != (size_t)n) { err = String("write failed: ") + Update.errorString(); break; }
            fwDone += n;
            lastData = millis();
            int pct = (int)(fwDone * 100 / fwTotal);
            if (pct / 10 > lastPct / 10) {
                lastPct = pct;
                fwProgress(String("install ") + BUILD_VERSION + " -> " + fwLatest + ": downloading " + pct + "%");
            }
        } else if (!http.connected() && !s->available()) {
            err = "connection closed during download"; break;
        } else if (millis() - lastData > 20000) {
            err = "download stalled (no data for 20 s)"; break;
        } else {
            delay(2);
        }
    }
    http.end();
    if (err.length()) {
        char b[200];
        snprintf(b, sizeof(b), " at %u%% (%u KB); now %s, lowest during the download %u KB free, block %u KB",
                 (unsigned)(fwDone * 100 / fwTotal), (unsigned)(fwDone / 1024), fwHeapText().c_str(),
                 (unsigned)(allMinFree / 1024), (unsigned)(allMinBlock / 1024));
        Update.abort();
        return err + b;
    }
    LOGI("update", "download took %lu s; lowest during the download %u KB free, largest block %u KB",
         (millis() - dl0) / 1000, (unsigned)(allMinFree / 1024), (unsigned)(allMinBlock / 1024));
    fwBootPhase = FWB_INSTALLING;
    if (!Update.end(true)) { err = String("image check failed: ") + Update.errorString(); return err; }
    LOGI("update", "download complete: %u bytes in %lu s, image verified and marked bootable", (unsigned)fwDone, (millis() - t0) / 1000);
    return "";
}

// An install from GitHub is running (check, download, restart): other web requests get a 503 (auth.h)
static volatile bool fwInstalling = false;
bool fwInstallBusy() { return fwInstalling && (fwState == FW_CHECKING || fwState == FW_DOWNLOADING || fwState == FW_RESTARTING); }
int  fwInstallPercent() { return fwState == FW_DOWNLOADING && fwTotal ? (int)(fwDone * 100 / fwTotal) : fwState == FW_RESTARTING ? 100 : 0; }

static void fwTask(void* arg)
{
    bool install = arg != nullptr;
    fwInstalling = install;
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
        // The download itself runs after a restart, before anything else starts (fwBootInstall):
        // in the running firmware the TLS session (~50 KB) did not fit next to the rest, even with
        // MQTT, telnet and the bus reads paused (78 KB free before, 14 KB during: stalled, 24/11/2026)
        Preferences p;
        p.begin("fwupd", false);
        p.putString("bootUrl", fwAssetUrl);
        p.putString("bootVer", fwLatest);
        p.putUInt("bootSize", (uint32_t)fwAssetSize);
        p.end();
        fwProgress(String("install ") + BUILD_VERSION + " -> " + fwLatest + ": restarting to download");
        fwState = FW_RESTARTING;
        LOGI("update", "installing %s -> %s: restarting, the download runs right after the start-up", BUILD_VERSION, fwLatest.c_str());
        delay(1500);   // let a status poll see "restarting"
        restartWith("firmware install from GitHub (download after the restart)");
    }
    if (err.length()) {
        fwError = err; fwState = FW_ERROR;
        if (install) { LOGE("update", "install failed: %s", err.c_str()); fwProgress("failed: " + err); }
        else LOGW("update", "update check failed: %s", err.c_str());
    }
    else fwState = FW_IDLE;
    fwInstalling = false;
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

// Install from GitHub, second half: from setup(), right after WiFi connected and before the
// rest starts (Bluetooth, web server, HTTPS, MQTT, telnet, the stove task). Only when the
// install button left a download in NVS (fwTask). One attempt: the note is removed first, so a
// crash during the download does not repeat it at every start (the bootloader keeps the
// running firmware; lastAttempt shows the step). Restarts afterwards, success or not: on
// success into the new firmware (on trial, otaVerify.h), otherwise into this one, which shows
// the error.
// Minimal web server during that download (the real one starts later in setup()): the status
// page, GET /api/firmware with the progress, and a 503 "firmware update in progress" for every
// other route (as during an install in the running firmware, auth.h), so the web app and API
// clients know what is going on. One connection at a time, answers from flash: a few hundred
// bytes of RAM next to the download. No API key: it only tells the progress.
static int fwSrv = -1;
static String fwBootError;
static void fwServeOpen()
{
    fwSrv = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fwSrv < 0) return;
    int one = 1; lwip_setsockopt(fwSrv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(80); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (lwip_bind(fwSrv, (struct sockaddr*)&a, sizeof(a)) != 0 || lwip_listen(fwSrv, 3) != 0) { lwip_close(fwSrv); fwSrv = -1; return; }
    lwip_fcntl(fwSrv, F_SETFL, lwip_fcntl(fwSrv, F_GETFL, 0) | O_NONBLOCK);
}
static void fwServeClose() { if (fwSrv >= 0) { lwip_close(fwSrv); fwSrv = -1; } }
static void fwSendAll(int c, const char* p, size_t n)
{
    while (n) { int w = lwip_send(c, p, n > 1400 ? 1400 : n, 0); if (w <= 0) return; p += w; n -= w; }
}
static void fwServe()
{
    if (fwSrv < 0) return;
    int c = lwip_accept(fwSrv, nullptr, nullptr);
    if (c < 0) return;
    struct timeval tv = { 1, 0 };
    lwip_setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    // the request line, then the rest of the headers read away (closing with unread data resets
    // the connection, and the browser may lose the answer)
    char line[96] = ""; size_t ll = 0; char b[256]; uint32_t tail = 0; bool end = false;
    for (int i = 0; i < 16 && !end; i++) {
        int n = lwip_recv(c, b, sizeof(b), 0);
        if (n <= 0) break;
        for (int j = 0; j < n; j++) {
            if (ll < sizeof(line) - 1) line[ll++] = b[j];
            tail = (tail << 8) | (uint8_t)b[j];
            if (tail == 0x0D0A0D0A) { end = true; break; }
        }
    }
    line[ll] = 0;
    char* eol = strchr(line, '\r'); if (eol) *eol = 0;
    const char* path = strchr(line, ' '); path = path ? path + 1 : "";
    auto is = [&](const char* r) { size_t n = strlen(r); return !strncmp(path, r, n) && (path[n] == ' ' || path[n] == '?'); };
    char hdr[160];
    if (is("/api/firmware")) {
        char j[640];
        int ph = fwBootPhase;
        const char* st = ph == FWB_FAILED ? "error" : ph >= FWB_INSTALLING ? "restarting" : "downloading";
        String err = fwBootError; err.replace("\\", "\\\\"); err.replace("\"", "'");
        int n = snprintf(j, sizeof(j), "{\"bootInstall\":true,\"phase\":\"%s\",\"state\":\"%s\",\"current\":\"%s\",\"latest\":\"%s\","
                         "\"progress\":{\"done\":%u,\"total\":%u},\"lastError\":%s%s%s,\"freeHeap\":%u,\"largestBlock\":%u}",
                         FWB_STR[ph], st, BUILD_VERSION, fwLatest.c_str(), (unsigned)fwDone, (unsigned)fwTotal,
                         err.length() ? "\"" : "", err.length() ? err.c_str() : "null", err.length() ? "\"" : "",
                         (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", n);
        fwSendAll(c, hdr, strlen(hdr)); fwSendAll(c, j, n);
    } else if (is("/") || is("/app") || is("/update")) {
        snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", (unsigned)strlen(FW_BOOT_PAGE));
        fwSendAll(c, hdr, strlen(hdr)); fwSendAll(c, FW_BOOT_PAGE, strlen(FW_BOOT_PAGE));
    } else {
        char j[160];
        int n = snprintf(j, sizeof(j), "{\"error\":\"Firmware update in progress - try again later\",\"firmwareUpdate\":true,\"progress\":%d,\"retryAfter\":30}",
                         fwTotal ? (int)(fwDone * 100 / fwTotal) : 0);
        snprintf(hdr, sizeof(hdr), "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nRetry-After: 30\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", n);
        fwSendAll(c, hdr, strlen(hdr)); fwSendAll(c, j, n);
    }
    lwip_shutdown(c, SHUT_WR);
    lwip_close(c);
}
// Serve the minimal server for a while (and keep the task watchdog of the loop task fed)
static void fwServeFor(uint32_t ms)
{
    unsigned long t = millis();
    do { fwServe(); esp_task_wdt_reset(); delay(20); } while (millis() - t < ms);
}

// The download in its own task: the TLS handshake needs a good 6 KB of stack, the loop task
// (setup) has 8 KB in all; setup() answers the minimal server meanwhile
static volatile bool fwBootDone = false;
static String fwBootResult;
static void fwBootTask(void*)
{
    fwBootResult = fwDoInstall();
    fwBootDone = true;
    vTaskDelete(nullptr);
}

void fwBootInstall()
{
    Preferences p;
    p.begin("fwupd", false);
    String url = p.getString("bootUrl", "");
    if (!url.length()) { p.end(); return; }
    fwLatest = p.getString("bootVer", "?");
    fwAssetSize = p.getUInt("bootSize", 0);
    p.remove("bootUrl"); p.remove("bootVer"); p.remove("bootSize");
    p.end();
    fwAssetUrl = url;
#if BOARD_HAS_WS2812
    StatusLedUpdate ledUpdate;   // LED1 "update" until it returns or restarts
#endif
    auto fail = [](const String& e) {
        LOGE("update", "install failed: %s", e.c_str());
        fwProgress("failed: " + e);
        Preferences f; f.begin("fwupd", false); f.putString("bootErr", e); f.end();
        fwBootError = e;
        fwBootPhase = FWB_FAILED;
    };
    LOGI("update", "installing %s -> %s right after the start-up (%s)", BUILD_VERSION, fwLatest.c_str(), fwHeapText().c_str());
    if (WiFi.status() != WL_CONNECTED) { fail("no WiFi at start-up"); return; }   // nothing changed yet: start normally
    fwBootPhase = FWB_CONNECTING;
    fwServeOpen();
    configTzTime(defaultTimezone, ntpServer);   // the certificate check needs the date
    for (int i = 0; i < 40 && !fwTimeValid(); i++) fwServeFor(500);
    if (!fwTimeValid()) {
        fail("no time from NTP within 20 s, needed to check the HTTPS certificate");
        fwServeFor(4000);   // the status page sees the error
        fwServeClose();
        return;             // nothing changed yet: start normally
    }
    // from here on it always restarts, so the Bluetooth memory can go back to the heap
    esp_bt_mem_release(ESP_BT_MODE_BTDM);
    fwState = FW_DOWNLOADING;
    // Watchdog: setup() (the loop task) is watched by the task watchdog (10 s, restart) and
    // feeds it here; the download task is not (a TLS connect can block longer than 10 s).
    // So setup() watches the download task: no progress (bytes or step) for FW_BOOT_HANG_MS =
    // hanging, and it restarts into the old firmware like a failed download.
    static const uint32_t FW_BOOT_HANG_MS = 90000;   // connect + redirect + 2 TLS handshakes stay well below it
    if (xTaskCreatePinnedToCore(fwBootTask, "fwBoot", 12288, nullptr, 1, nullptr, 1) != pdPASS) fwBootResult = "could not start the download task (memory)";
    else {
        unsigned long lastMove = millis(); size_t lastDone = 0; int lastPhase = fwBootPhase;
        while (!fwBootDone) {
            fwServeFor(100);
            if (fwDone != lastDone || fwBootPhase != lastPhase) { lastDone = fwDone; lastPhase = fwBootPhase; lastMove = millis(); }
            else if (millis() - lastMove > FW_BOOT_HANG_MS) {
                char b[120];
                snprintf(b, sizeof(b), "the download hangs: no progress for %lu s (step %s, %u KB) - back to the previous firmware",
                         (unsigned long)(FW_BOOT_HANG_MS / 1000), FWB_STR[fwBootPhase], (unsigned)(fwDone / 1024));
                fwBootResult = b;
                break;
            }
        }
    }
    if (fwBootResult.length()) {
        fail(fwBootResult);
        fwServeFor(4000);   // the status page sees the error
        fwServeClose();
        restartWith("firmware install from GitHub failed");
    }
    Preferences f; f.begin("fwupd", false); f.putString("last", String(BUILD_VERSION) + " -> " + fwLatest); f.end();
    fwProgressClear();
    fwBootPhase = FWB_RESTARTING;
    LOGI("update", "installed, restarting into %s", fwLatest.c_str());
    fwServeFor(2500);   // the status page sees "installing and restarting"
    fwServeClose();
    restartWith("firmware update from GitHub");
}

void fwSetup()
{
    Preferences p;
    p.begin("fwupd", false);
    fwAutoCheck = p.getBool("auto", true);
    fwLastInstall = p.getString("last", "");
    fwLastAttempt = p.getString("prog", "");
    // a download after the restart that failed (fwBootInstall): shown as the last error
    String bootErr = p.getString("bootErr", "");
    if (bootErr.length()) { fwError = bootErr; fwState = FW_ERROR; p.remove("bootErr"); }
    p.end();
    // HTTPS on: the automatic check runs right at start-up, before HTTPS reserves its buffers
    // (constants.h); the HTTPS setting is read here as well, fwSetup() runs first
    Preferences h; h.begin("https", true); bool httpsOn = h.getBool("enabled", true); h.end();
    fwBootCheckPending = fwAutoCheck && httpsOn;
    if (fwLastAttempt.length() && !fwLastAttempt.startsWith("failed"))
        LOGW("update", "the previous GitHub install did not finish (controller restarted or powered off during it), last step: %s", fwLastAttempt.c_str());
}

// From loop(): the automatic check.
void fwLoop()
{
    static unsigned long lastTry = 0;
    if (fwBootCheckPending) {
        static bool started = false;
        if (fwBusy) return;                                   // running
        if (started) { fwBootCheckPending = false; return; }  // done (result logged by the task)
        if (millis() > FW_BOOT_WAIT_MS) {
            fwBootCheckPending = false;
            LOGW("update", "no update check at start-up: no network or clock within %lu s (with HTTPS on there is no daily check; use the button)", FW_BOOT_WAIT_MS / 1000);
            return;
        }
        if (otaActive || WiFi.status() != WL_CONNECTED || !fwTimeValid()) return;
        started = fwStart(false);
        lastTry = millis();
        if (!started) fwBootCheckPending = false;
        return;
    }
    // HTTPS on: no daily check (it would take the HTTPS buffers, see constants.h); by hand only
    if (httpsTlsOn) return;
    if (!fwAutoCheck || fwBusy || otaActive || httpsSessionActive || WiFi.status() != WL_CONNECTED || !fwTimeValid()) return;
    unsigned long now = millis();
    unsigned long wait = fwCheckedAt ? FW_CHECK_EVERY_MS : (lastTry ? 60UL * 60UL * 1000UL : FW_FIRST_CHECK_MS);
    unsigned long since = fwCheckedAt ? now - fwCheckedAt : (lastTry ? now - lastTry : now);
    if (since < wait) return;
    lastTry = now;
    fwStart(false);   // a failed check retries after an hour
}

// GET /update: the status page of an install (fwBootPage.h); public like the web app
void handleRouteUpdatePage()
{
    wm.server->sendHeader("Cache-Control", "no-store");
    wm.server->send_P(200, "text/html; charset=utf-8", FW_BOOT_PAGE);
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
    // rollback of a failed update (otaVerify.h)
    json["trial"] = otaTrial;
    if (otaTrial) json["trialSecondsLeft"] = otaTrialSecondsLeft(); else json["trialSecondsLeft"] = nullptr;
    if (otaRolledBack.length()) json["rolledBack"] = otaRolledBack; else json["rolledBack"] = nullptr;
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
