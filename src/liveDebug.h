#pragma once
// Live debugging for the web app (Debug mode):
//   GET  /api/log?since=<cursor>     new lines of the firmware log (liveLog.h)
//   POST /api/busmon?enabled=true    start the stove bus monitor (60 s, extended
//                                    by every GET while someone watches)
//   GET  /api/busmon?since=<cursor>  bus transactions since the cursor
//
// Both use a cursor: the `next` of the previous answer. Start with since=0.
// `lost` = the cursor was older than the buffer (entries were overwritten).
// The bus monitor only records while it is active, so normal operation has no cost.

struct BusEvent {
    uint32_t ms;        // millis()
    uint8_t  flags;     // bit 0 write, bit 1 ok, bit 2 EEPROM
    uint8_t  addr;
    uint8_t  value;     // read result, or the value written
    uint8_t  attempts;  // attempt number (1..of)
    uint8_t  of;        // attempts allowed
    uint8_t  txLen, rxLen;
    uint8_t  tx[4];     // raw bytes sent (last attempt)
    uint8_t  rx[6];     // raw bytes received: echo of tx + the stove's answer
};
typedef pc::Ring<BusEvent, 600> BusRing;      // ~20 reads/s: half a minute of traffic
static BusRing* busRing = nullptr;            // heap, allocated when the monitor starts
static portMUX_TYPE busMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t busmonUntil = 0;
static const uint32_t BUSMON_HOLD_MS = 60000;

static void busMonitorHook(bool isWrite, uint8_t type, uint8_t addr, uint8_t value, bool ok, uint8_t attempts, uint8_t of,
                           const uint8_t* tx, uint8_t txLen, const uint8_t* rx, uint8_t rxLen)
{
    if ((int32_t)(millis() - busmonUntil) > 0) { Stove::monitor = nullptr; return; }   // nobody watching any more
    BusEvent e = { (uint32_t)millis(), (uint8_t)((isWrite ? 1 : 0) | (ok ? 2 : 0) | (type == eeprom ? 4 : 0)), addr, value, attempts, of, 0, 0, {0}, {0} };
    e.txLen = txLen > sizeof(e.tx) ? sizeof(e.tx) : txLen;
    e.rxLen = rxLen > sizeof(e.rx) ? sizeof(e.rx) : rxLen;
    memcpy(e.tx, tx, e.txLen);
    memcpy(e.rx, rx, e.rxLen);
    if (!busRing) return;
    portENTER_CRITICAL(&busMux);
    busRing->push(e);
    portEXIT_CRITICAL(&busMux);
}

static uint32_t argCursor()
{
    return wm.server->hasArg("since") ? (uint32_t)strtoul(wm.server->arg("since").c_str(), nullptr, 10) : 0;
}

static void jsonEscapeAppend(String& out, const char* s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:
            if ((uint8_t)c < 0x20) { char u[8]; snprintf(u, sizeof(u), "\\u%04x", (uint8_t)c); out += u; }
            else out += c;
        }
    }
}

void handleRouteLog()
{
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    if (!logRing) { wm.server->send(503, "application/json", "{\"error\":\"live log not available (memory)\"}"); return; }
    // Streamed in pieces of 512 bytes (at most 4 KB per answer): it used to take a 4 KB buffer and
    // a 5 KB String at once, every second while the console is open; with a fragmented heap that
    // left the WiFi driver without receive buffers (20/11/2026). "next" comes last.
    wm.server->sendHeader("Cache-Control", "no-store");
    wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    wm.server->send(200, "application/json", "");
    char piece[512];
    String out; out.reserve(700);
    uint32_t cur = argCursor(), from = cur; bool lost = false; size_t total = 0;
    for (int i = 0; total < 4096 && !httpStalled; i++) {
        uint32_t f; bool l; size_t n;
        portENTER_CRITICAL(&logMux);
        n = logRing->read(cur, piece, sizeof(piece), f, l);
        portEXIT_CRITICAL(&logMux);
        if (i == 0) {
            from = f; lost = l;
            out += "{\"from\":"; out += from; out += ",\"lost\":"; out += lost ? "true" : "false"; out += ",\"text\":\"";
        } else if (f != cur) break;   // overwritten meanwhile: the rest comes with the next poll
        if (!n) break;
        jsonEscapeAppend(out, piece, n);
        httpSendChunk(out);
        cur = f + n; total += n;
    }
    out += "\",\"next\":"; out += (uint32_t)(from + total); out += "}";
    httpSendChunk(out);
    httpEndChunks();
}

void handleRouteBusmon()
{
    if (wm.server->method() == HTTP_POST) {
        int on = pc::parseOnOff(wm.server->arg("enabled").c_str());
        if (on < 0) { wm.server->send(400, "application/json", "{\"result\":\"failed - enabled must be true / false\"}"); return; }
        if (on) {
            if (!busRing) busRing = new (std::nothrow) BusRing();
            if (!busRing) { wm.server->send(503, "application/json", "{\"result\":\"failed - not enough memory\"}"); return; }
            busmonUntil = millis() + BUSMON_HOLD_MS; Stove::monitor = busMonitorHook;
        }
        else    { Stove::monitor = nullptr; busmonUntil = millis(); }
    } else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }

    bool active = Stove::monitor != nullptr;
    if (active && wm.server->method() == HTTP_GET) busmonUntil = millis() + BUSMON_HOLD_MS;   // still watched

    const size_t MAXEV = 250;
    BusEvent* ev = busRing ? (BusEvent*)malloc(MAXEV * sizeof(BusEvent)) : nullptr;
    uint32_t from = 0; bool lost = false; size_t n = 0;
    if (ev) {
        portENTER_CRITICAL(&busMux);
        n = busRing->read(argCursor(), MAXEV, from, lost);
        for (size_t i = 0; i < n; i++) ev[i] = busRing->at(from + i);
        portEXIT_CRITICAL(&busMux);
    }

    String out;
    out.reserve(160 + n * 95);
    out += "{\"active\":"; out += active ? "true" : "false";
    out += ",\"now\":"; out += (uint32_t)millis();
    out += ",\"from\":"; out += from;
    out += ",\"next\":"; out += (uint32_t)(from + n);
    out += ",\"lost\":"; out += lost ? "true" : "false";
    out += ",\"fields\":[\"ms\",\"write\",\"eeprom\",\"address\",\"value\",\"ok\",\"attempt\",\"of\",\"text\",\"tx\",\"rx\"],\"events\":[";
    for (size_t i = 0; i < n; i++) {
        const BusEvent& e = ev[i];
        bool ok = (e.flags >> 1) & 1, isEeprom = (e.flags >> 2) & 1;
        char row[56];
        snprintf(row, sizeof(row), "%s[%lu,%d,%d,%u,%u,%d,%u,%u,\"", i ? "," : "", (unsigned long)e.ms,
                 e.flags & 1, isEeprom, e.addr, e.value, ok, e.attempts, e.of);
        out += row;
        // meaning of the value, from the same register table as /api/dump ("" = unknown register)
        String txt = ok ? (isEeprom ? getEepromValueTxt(e.addr, e.value) : getRamValueTxt(e.addr, e.value)) : String("");
        jsonEscapeAppend(out, txt.c_str(), txt.length());
        // raw bytes as hex strings
        char hex[4];
        out += "\",\"";
        for (uint8_t k = 0; k < e.txLen; k++) { snprintf(hex, sizeof(hex), "%02X", e.tx[k]); out += hex; }
        out += "\",\"";
        for (uint8_t k = 0; k < e.rxLen; k++) { snprintf(hex, sizeof(hex), "%02X", e.rx[k]); out += hex; }
        out += "\"]";
    }
    out += "]}";
    free(ev);
    wm.server->sendHeader("Cache-Control", "no-store");
    httpSendText(200, "application/json", out);
}

// GET  /api/loglevel                     levels per module
// POST /api/loglevel?wifi=debug&bus=warn  change and keep them (NVS)
void handleRouteLogLevel()
{
    String err;
    if (wm.server->method() == HTTP_POST) {
        for (int i = 0; i < LOG_MODULES; i++) {
            if (!wm.server->hasArg(logModules[i].tag)) continue;
            int lv = logLevelFromName(wm.server->arg(logModules[i].tag).c_str());
            if (lv < 0) { err = String(logModules[i].tag) + ": level must be off, error, warn, info or debug"; continue; }
            logModules[i].level = (uint8_t)lv;
            logSaveLevel(logModules[i].tag, (uint8_t)lv);
            LOGI("sys", "log level %s = %s", logModules[i].tag, LOG_LEVEL_NAMES[lv]);
        }
    } else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    StaticJsonDocument<1024> json;
    if (wm.server->method() == HTTP_POST) json["result"] = err.length() ? "failed - " + err : String("success");
    JsonObject lv = json.createNestedObject("levels");
    JsonArray mods = json.createNestedArray("modules");
    for (int i = 0; i < LOG_MODULES; i++) {
        lv[logModules[i].tag] = LOG_LEVEL_NAMES[logModules[i].level];
        JsonObject m = mods.createNestedObject();
        m["tag"] = logModules[i].tag; m["level"] = LOG_LEVEL_NAMES[logModules[i].level]; m["what"] = logModules[i].what;
    }
    String out; serializeJson(json, out);
    wm.server->send(err.length() ? 400 : 200, "application/json", out);
}

// ── Who changed what: every write to the stove, with its source ───────
static void stoveWriteLogger(uint8_t type, uint8_t addr, uint8_t value, bool ok)
{
    bool ee = type == eeprom;
    String txt = ee ? getEepromValueTxt(addr, value) : getRamValueTxt(addr, value);
    const char* src = cmdSource[0] ? cmdSource : "the controller itself";
    if (ok) LOGI("stove", "write %s 0x%02X = %u%s%s%s by %s", ee ? "EEPROM" : "RAM", addr, value,
                 txt.length() ? " (" : "", txt.c_str(), txt.length() ? ")" : "", src);
    else    LOGE("stove", "write %s 0x%02X = %u FAILED (not confirmed by the stove), by %s", ee ? "EEPROM" : "RAM", addr, value, src);
    if (ok && !ee && addr == stoveStateAddr) {
        lastStateWriteMs = millis(); lastStateWriteVal = value;
        strncpy(lastStateWriteSrc, src, sizeof(lastStateWriteSrc) - 1);
    }
}

// ── Clock drift, remote control, every minute ─────────────────────────
static void diagClockAndRemote()
{
    // stove clock vs NTP
    static bool clockWarned = false;
    if (stoveTimeReadMs && time(nullptr) > 1700000000) {
        long d = (long)(ntpTimeNow() - stoveTimeNow());
        if (!clockWarned && labs(d) > 60) {
            LOGW("stove", "stove clock is %ld s %s NTP: timers and the event times may be off (sync it under Clock)", labs(d), d > 0 ? "behind" : "ahead of");
            clockWarned = true;
        } else if (clockWarned && labs(d) <= 30) {
            LOGI("stove", "stove clock within %ld s of NTP again", labs(d));
            clockWarned = false;
        }
    }
    // remote control: RAM 0x2E counts down when the remote is not heard (-1 = not seen recently)
    static uint32_t remoteSeenMs = 0, remoteWarnedMs = 0;
    uint32_t now = millis();
    if (cachedRemoteControlLastSeen >= 0) {
        if (remoteWarnedMs) LOGI("stove", "remote control heard again after %lu min", (unsigned long)((now - remoteSeenMs) / 60000));
        remoteSeenMs = now; remoteWarnedMs = 0;
    } else if (lastFastStatesAt && !remoteWarnedMs && now - remoteSeenMs > 30UL * 60UL * 1000UL) {
        if (remoteSeenMs) LOGW("stove", "remote control not heard for %lu min (out of range or empty battery?)", (unsigned long)((now - remoteSeenMs) / 60000));
        else LOGW("stove", "remote control not heard since the start (out of range or empty battery?)");
        remoteWarnedMs = now;
    }
}

// ── Health line every 15 min (sys), with warnings ─────────────────────
void diagTick()
{
    static uint32_t lastMin = 0, last = 0, lastTx = 0, lastRetries = 0, lastFailed = 0;
    uint32_t now = millis();
    if (now - lastMin >= 60000) { lastMin = now; diagClockAndRemote(); }
    uint32_t tx = stove.transactions, rt = stove.retries, fl = stove.readErrors + stove.writeErrors;
    if (!last) { last = now; lastTx = tx; lastRetries = rt; lastFailed = fl; return; }
    if (now - last < 15UL * 60UL * 1000UL) return;
    uint32_t minutes = (now - last) / 60000;
    last = now;
    uint32_t dTx = tx - lastTx, dRt = rt - lastRetries, dFl = fl - lastFailed;
    lastTx = tx; lastRetries = rt; lastFailed = fl;
    uint32_t heap = ESP.getFreeHeap(), minHeap = ESP.getMinFreeHeap(), block = ESP.getMaxAllocHeap();
    int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    uint32_t avgFast = diagFastCycles ? diagFastSumMs / diagFastCycles : 0;
    LOGI("sys", "health: up %s | heap %lu KB free (lowest %lu KB, largest block %lu KB) | WiFi %d dBm | bus %lu transactions in %lu min, %lu retries, %lu failed | sensor reads avg %lu ms per group, max %lu ms (settings / rare: max %lu ms), longest wait for the bus %lu ms",
         uptimeStr().c_str(), (unsigned long)(heap / 1024), (unsigned long)(minHeap / 1024), (unsigned long)(block / 1024), rssi,
         (unsigned long)dTx, (unsigned long)minutes, (unsigned long)dRt, (unsigned long)dFl,
         (unsigned long)avgFast, (unsigned long)diagFastMaxMs, (unsigned long)diagSlowMaxMs, (unsigned long)diagLockWaitMaxMs);
    diagFastCycles = 0; diagFastSumMs = 0; diagFastMaxMs = 0; diagSlowCycles = 0; diagSlowMaxMs = 0; diagLockWaitMaxMs = 0;
    if (heap < 30000 || block < 20000)
        logPrintf(heap < 20000 || block < 12000 ? LOG_ERROR : LOG_WARN, "sys", "memory low: %lu KB free, largest block %lu KB (HTTPS / TLS needs ~45 KB)", (unsigned long)(heap / 1024), (unsigned long)(block / 1024));
    if (dTx && dFl * 100 > dTx)
        LOGE("bus", "%lu of %lu stove transactions failed in the last %lu min: check the serial connection to the stove", (unsigned long)dFl, (unsigned long)dTx, (unsigned long)minutes);
    else if (dTx && dRt * 20 > dTx)
        LOGW("bus", "%lu retries for %lu transactions in the last %lu min (> 5%%): the serial line is noisy", (unsigned long)dRt, (unsigned long)dTx, (unsigned long)minutes);
    if (rssi && rssi < -80) LOGW("wifi", "weak WiFi signal: %d dBm", rssi);
}

// POST /api/diag/reset?serial=true&crashes=true
//   serial : stove bus counters (read / write errors, retries, skipped, transactions)
//   crashes: the crash counter kept in NVS (starts after a crash, watchdog or brown-out)
void handleRouteDiagReset()
{
    if (wm.server->method() != HTTP_POST) { wm.server->send(405); return; }
    bool serial = pc::parseOnOff(wm.server->arg("serial").c_str()) == 1;
    bool crashes = pc::parseOnOff(wm.server->arg("crashes").c_str()) == 1;
    if (!serial && !crashes) { wm.server->send(400, "application/json", "{\"result\":\"failed - give serial=true and / or crashes=true\"}"); return; }
    String who = clientIp();
    if (serial) {
        stove.readErrors = 0; stove.writeErrors = 0; stove.retries = 0; stove.skipped = 0; stove.transactions = 0;
        LOGI("sys", "serial counters reset by web/API %s", who.c_str());
    }
    if (crashes) {
        Preferences p; p.begin("diag", false); p.putUInt("crashes", 0); p.end();
        diagCrashes = 0;
        LOGI("sys", "crash counter reset by web/API %s", who.c_str());
    }
    StaticJsonDocument<192> json;
    json["result"] = "success";
    json["crashCount"] = diagCrashes;
    JsonObject se = json.createNestedObject("serial");
    se["readErrors"] = stove.readErrors; se["writeErrors"] = stove.writeErrors; se["retries"] = stove.retries;
    String out; serializeJson(json, out);
    httpSendText(200, "application/json", out);
}
