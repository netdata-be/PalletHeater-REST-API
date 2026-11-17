#pragma once
// History: 24 h ring buffer of the cached values (/api/history), kept on SPIFFS.
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

// ─────────────────────────────────────────────
// History: 24 h ring buffer of the cached values, one sample every 2 min.
// Kept in RAM only (lost on reboot). Served on GET /api/history.
// ─────────────────────────────────────────────
struct HistSample {
    uint32_t t;          // unix time (UTC)
    int16_t  room10;     // °C x10
    int16_t  stove10;
    int16_t  remote10;
    uint8_t  setpoint;
    uint8_t  fume;
    int8_t   flame;
    int8_t   state;
    int8_t   fan;
    int8_t   flameActual;   // -1 = unknown (samples from older firmware)
    int16_t  auger10;       // auger speed rpm x10, -1 = unknown (older firmware)
    uint16_t fumeRpm;       // fume fan rpm, 0xFFFF = unknown (older firmware)
    int8_t   fanActual;     // speed the room fan really runs at (0..3), -1 = unknown
    uint8_t  fanPower;      // room fan power, RAM 0x0A (0 off, 21/23/25), 0xFF = unknown
};
// On-disk layout of format v5 (no fanPower), converted when loading
struct HistSampleV5 {
    uint32_t t;
    int16_t  room10, stove10, remote10;
    uint8_t  setpoint, fume;
    int8_t   flame, state, fan, flameActual;
    int16_t  auger10;
    uint16_t fumeRpm;
    int8_t   fanActual;
};
// On-disk layout of format v4 (no fanActual), converted when loading
struct HistSampleV4 {
    uint32_t t;
    int16_t  room10, stove10, remote10;
    uint8_t  setpoint, fume;
    int8_t   flame, state, fan, flameActual;
    int16_t  auger10;
    uint16_t fumeRpm;
};
// On-disk layout of format v3 (no fume fan rpm), converted when loading
struct HistSampleV3 {
    uint32_t t;
    int16_t  room10, stove10, remote10;
    uint8_t  setpoint, fume;
    int8_t   flame, state, fan, flameActual;
    int16_t  auger10;
};
// On-disk layout of format v1/v2 (16 bytes, no auger), converted when loading
struct HistSampleV2 {
    uint32_t t;
    int16_t  room10, stove10, remote10;
    uint8_t  setpoint, fume;
    int8_t   flame, state, fan, flameActual;
};
static const uint32_t HIST_INTERVAL_S = 120;
static const int      HIST_SIZE       = 720;   // 24 h
static HistSample hist[HIST_SIZE];
static int histHead = 0, histCount = 0;

// ── Persistence on SPIFFS ─────────────────────────────────────────────
// Every sample is appended (≈16 bytes per 2 min) to one of two files. When the
// active file holds 24 h of samples, the other file is truncated and becomes
// active, so the two files together always cover at least the last 24 h.
// Appends spread wear over the 128 KB partition (well under 1 erase per sector
// per day). Each file starts with a small header so a changed sample layout
// in a future firmware is detected and ignored instead of read as garbage.
static const char* HIST_FILES[2] = { "/hist_a.bin", "/hist_b.bin" };
static const uint8_t HIST_MAGIC[4] = { 'P', 'H', 'H', '6' };   // v2: + flameActual, v3: + auger10, v4: + fumeRpm, v5: + fanActual, v6: + fanPower
// v1 samples (PHH1) were 16 bytes without flameActual; the padding byte is now
// flameActual, so v1 records are read with flameActual forced to -1 (unknown).
static const size_t  HIST_HDR = sizeof(HIST_MAGIC) + 1;   // magic + sizeof(HistSample)
static int  histActiveFile = 0;
static int  histActiveCount = 0;                           // samples in the active file

static int histFileVersion[2] = { 0, 0 };

// Returns the file format version (1..6), or 0 when the header is not valid
static int histHeaderVersion(File& f)
{
    uint8_t hdr[HIST_HDR];
    if (f.read(hdr, HIST_HDR) != HIST_HDR) return 0;
    if (memcmp(hdr, HIST_MAGIC, 3) != 0) return 0;
    if (hdr[3] == '6' && hdr[4] == sizeof(HistSample))   return 6;
    if (hdr[3] == '5' && hdr[4] == sizeof(HistSampleV5)) return 5;
    if (hdr[3] == '4' && hdr[4] == sizeof(HistSampleV4)) return 4;
    if (hdr[3] == '3' && hdr[4] == sizeof(HistSampleV3)) return 3;
    if (hdr[3] == '2' && hdr[4] == sizeof(HistSampleV2)) return 2;
    if (hdr[3] == '1' && hdr[4] == sizeof(HistSampleV2)) return 1;
    return 0;
}
static size_t histRecordSize(int ver) { return ver >= 6 ? sizeof(HistSample) : ver == 5 ? sizeof(HistSampleV5) : ver == 4 ? sizeof(HistSampleV4) : ver == 3 ? sizeof(HistSampleV3) : sizeof(HistSampleV2); }

// Read one record of the given file version into the current layout
static bool histReadRecord(File& f, int ver, HistSample& out)
{
    if (ver >= 6) return f.read((uint8_t*)&out, sizeof(out)) == sizeof(out);
    if (ver == 5) {
        HistSampleV5 o;
        if (f.read((uint8_t*)&o, sizeof(o)) != sizeof(o)) return false;
        out = {};
        memcpy(&out, &o, sizeof(o));   // v5 is a prefix of the current layout
        out.fanPower = 0xFF;
        return true;
    }
    if (ver == 4) {
        HistSampleV4 o;
        if (f.read((uint8_t*)&o, sizeof(o)) != sizeof(o)) return false;
        out = {};
        memcpy(&out, &o, sizeof(o));   // v4 is a prefix of the current layout
        out.fanActual = -1;
        out.fanPower = 0xFF;
        return true;
    }
    if (ver == 3) {
        HistSampleV3 o;
        if (f.read((uint8_t*)&o, sizeof(o)) != sizeof(o)) return false;
        out = {};
        out.t = o.t; out.room10 = o.room10; out.stove10 = o.stove10; out.remote10 = o.remote10;
        out.setpoint = o.setpoint; out.fume = o.fume; out.flame = o.flame; out.state = o.state; out.fan = o.fan;
        out.flameActual = o.flameActual; out.auger10 = o.auger10; out.fumeRpm = 0xFFFF; out.fanActual = -1; out.fanPower = 0xFF;
        return true;
    }
    HistSampleV2 o;
    if (f.read((uint8_t*)&o, sizeof(o)) != sizeof(o)) return false;
    out = {};
    out.t = o.t; out.room10 = o.room10; out.stove10 = o.stove10; out.remote10 = o.remote10;
    out.setpoint = o.setpoint; out.fume = o.fume; out.flame = o.flame; out.state = o.state; out.fan = o.fan;
    out.flameActual = ver == 1 ? -1 : o.flameActual;
    out.auger10 = -1;
    out.fumeRpm = 0xFFFF;
    out.fanActual = -1;
    out.fanPower = 0xFF;
    return true;
}

static void histPush(const HistSample& s)
{
    hist[histHead] = s;
    histHead = (histHead + 1) % HIST_SIZE;
    if (histCount < HIST_SIZE) histCount++;
}

// Load both files at boot, oldest first, keeping only the last 24 h
static void historyLoad()
{
    if (!spiffsMounted) return;
    struct { int idx; uint32_t last; int count; } info[2] = { {0, 0, 0}, {1, 0, 0} };
    for (int i = 0; i < 2; i++) {
        File f = SPIFFS.open(HIST_FILES[i], FILE_READ);
        if (!f) continue;
        histFileVersion[i] = histHeaderVersion(f);
        if (!histFileVersion[i]) { f.close(); SPIFFS.remove(HIST_FILES[i]); continue; }
        size_t rs = histRecordSize(histFileVersion[i]);
        size_t n = (f.size() - HIST_HDR) / rs;
        info[i].count = (int)n;
        if (n) { f.seek(HIST_HDR + (n - 1) * rs); HistSample s; if (histReadRecord(f, histFileVersion[i], s)) info[i].last = s.t; }
        f.close();
    }
    int newer = info[1].last > info[0].last ? 1 : 0, older = 1 - newer;
    uint32_t cutoff = (uint32_t)time(nullptr) > 1700000000 ? (uint32_t)time(nullptr) - 24UL * 3600UL : 0;
    for (int k : { older, newer }) {
        File f = SPIFFS.open(HIST_FILES[k], FILE_READ);
        if (!f) continue;
        int ver = histHeaderVersion(f);
        if (!ver) { f.close(); continue; }
        HistSample s;
        while (histReadRecord(f, ver, s)) {
            if (s.t < 1700000000) continue;                 // never stored without NTP, but be safe
            if (cutoff && s.t < cutoff) continue;
            if (histCount && s.t <= hist[(histHead - 1 + HIST_SIZE) % HIST_SIZE].t) continue;  // keep it ordered
            histPush(s);
        }
        f.close();
    }
    histActiveFile  = newer;
    histActiveCount = info[newer].count;
    Serial.printf("History: loaded %d samples from SPIFFS\n", histCount);
}

static void historyAppend(const HistSample& s)
{
    if (!spiffsMounted) return;
    // After a format change: rewrite everything in RAM (already converted, includes s)
    // in the new format to the other file, so no history is lost on an upgrade.
    if (histActiveCount > 0 && histFileVersion[histActiveFile] != 6) {
        int target = 1 - histActiveFile;
        SPIFFS.remove(HIST_FILES[target]);
        File f = SPIFFS.open(HIST_FILES[target], FILE_WRITE);
        if (!f) return;
        f.write(HIST_MAGIC, sizeof(HIST_MAGIC)); uint8_t sz = sizeof(HistSample); f.write(&sz, 1);
        int start = (histHead - histCount + HIST_SIZE) % HIST_SIZE;
        for (int i = 0; i < histCount; i++) f.write((const uint8_t*)&hist[(start + i) % HIST_SIZE], sizeof(HistSample));
        f.close();
        SPIFFS.remove(HIST_FILES[histActiveFile]);   // its samples are in the new file now
        histFileVersion[histActiveFile] = 0;
        histFileVersion[target] = 6;
        histActiveFile = target;
        histActiveCount = histCount;
        return;
    }
    if (histActiveCount >= HIST_SIZE) {
        histActiveFile = 1 - histActiveFile;
        histActiveCount = 0;
        SPIFFS.remove(HIST_FILES[histActiveFile]);
    }
    bool fresh = !SPIFFS.exists(HIST_FILES[histActiveFile]);
    File f = SPIFFS.open(HIST_FILES[histActiveFile], fresh ? FILE_WRITE : FILE_APPEND);
    if (!f) return;
    if (fresh) { f.write(HIST_MAGIC, sizeof(HIST_MAGIC)); uint8_t sz = sizeof(HistSample); f.write(&sz, 1); histFileVersion[histActiveFile] = 6; }
    if (f.write((const uint8_t*)&s, sizeof(s)) == sizeof(s)) histActiveCount++;
    f.close();
}

static void historyTick()
{
    static unsigned long lastMs = 0;
    time_t t = time(nullptr);
    if (t < 1700000000 || lastFastStatesAt == 0 || lastSlowStatesAt == 0) return;  // no NTP / no data yet
    if (histCount && millis() - lastMs < HIST_INTERVAL_S * 1000UL) return;
    lastMs = millis();
    // Loaded history may already contain a sample from just before a restart
    if (histCount && (uint32_t)t - hist[(histHead - 1 + HIST_SIZE) % HIST_SIZE].t < HIST_INTERVAL_S / 2) return;
    HistSample s = {};
    s.t        = (uint32_t)t;
    s.room10   = (int16_t)(ambTemp * 10);
    s.stove10  = (int16_t)(ambTempStove * 10);
    s.remote10 = (int16_t)(ambTempRemoteControl * 10);
    s.setpoint = (uint8_t)setPointTemp;
    s.fume     = fumesTemp;
    s.flame    = (int8_t)flamePower;
    s.state    = (int8_t)stoveState;
    s.fan      = (int8_t)roomFanSpeed;
    s.flameActual = (int8_t)cachedFlameActual;
    s.auger10 = (int16_t)(cachedAugerSpeed * 10 + 0.5f);
    s.fumeRpm = (uint16_t)constrain(cachedFumeFanRPM, 0, 0xFFFE);
    s.fanActual = (int8_t)roomFanActual();
    s.fanPower = cachedRoomFanControl < 0 ? 0xFF : (uint8_t)constrain(cachedRoomFanControl, 0, 254);
    histPush(s);
    historyAppend(s);
}

void handleRouteHistory()
{
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    int hours = wm.server->hasArg("hours") ? wm.server->arg("hours").toInt() : 24;
    if (hours < 1 || hours > 24) hours = 24;
    uint32_t since = (uint32_t)time(nullptr) - (uint32_t)hours * 3600UL;

    wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    wm.server->send(200, "application/json", "");
    String out;
    out.reserve(1800);
    out += "{\"interval\":"; out += HIST_INTERVAL_S;
    out += ",\"fields\":[\"t\",\"room\",\"stove\",\"remote\",\"setpoint\",\"fume\",\"flame\",\"state\",\"fan\",\"flameActual\",\"auger\",\"fumeFan\",\"fanActual\",\"fanPower\"],\"samples\":[";
    bool first = true;
    int start = (histHead - histCount + HIST_SIZE) % HIST_SIZE;
    for (int i = 0; i < histCount; i++) {
        const HistSample& s = hist[(start + i) % HIST_SIZE];
        if (s.t < since) continue;
        char row[144];
        char act[6], aug[10], rpm[8], fa[6], fp[6];
        if (s.fanPower == 0xFF) strcpy(fp, "null"); else snprintf(fp, sizeof(fp), "%u", s.fanPower);
        if (s.fanActual < 0) strcpy(fa, "null"); else snprintf(fa, sizeof(fa), "%d", s.fanActual);
        if (s.flameActual < 0) strcpy(act, "null"); else snprintf(act, sizeof(act), "%d", s.flameActual);
        if (s.fumeRpm == 0xFFFF) strcpy(rpm, "null"); else snprintf(rpm, sizeof(rpm), "%u", s.fumeRpm);
        if (s.auger10 < 0) strcpy(aug, "null"); else snprintf(aug, sizeof(aug), "%.1f", s.auger10 / 10.0f);
        snprintf(row, sizeof(row), "%s[%lu,%.1f,%.1f,%.1f,%u,%u,%d,%d,%d,%s,%s,%s,%s,%s]", first ? "" : ",",
                 (unsigned long)s.t, s.room10 / 10.0f, s.stove10 / 10.0f, s.remote10 / 10.0f,
                 s.setpoint, s.fume, s.flame, s.state, s.fan, act, aug, rpm, fa, fp);
        out += row;
        first = false;
        if (out.length() > 1500) { wm.server->sendContent(out); out = ""; }
    }
    out += "]}";
    wm.server->sendContent(out);
    wm.server->sendContent("");
}
