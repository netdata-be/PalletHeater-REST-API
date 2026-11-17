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
        if (!SPIFFS.exists(HIST_FILES[i])) { LOGI("history", "%s: not present", HIST_FILES[i]); continue; }
        File f = SPIFFS.open(HIST_FILES[i], FILE_READ);
        if (!f) { LOGE("history", "%s: exists but cannot be opened", HIST_FILES[i]); continue; }
        size_t fsize = f.size();
        histFileVersion[i] = histHeaderVersion(f);
        if (!histFileVersion[i]) {
            LOGW("history", "%s: %u bytes, header not valid - removed", HIST_FILES[i], (unsigned)fsize);
            f.close(); SPIFFS.remove(HIST_FILES[i]); continue;
        }
        size_t rs = histRecordSize(histFileVersion[i]);
        size_t n = (fsize - HIST_HDR) / rs;
        info[i].count = (int)n;
        if (n) { f.seek(HIST_HDR + (n - 1) * rs); HistSample s; if (histReadRecord(f, histFileVersion[i], s)) info[i].last = s.t; }
        f.close();
        char last[24] = "-";
        if (info[i].last) { time_t lt = info[i].last; strftime(last, sizeof(last), "%d/%m %H:%M", localtime(&lt)); }
        LOGI("history", "%s: %u bytes, format v%d, %u samples, last %s", HIST_FILES[i], (unsigned)fsize, histFileVersion[i], (unsigned)n, last);
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
    LOGI("history", "loaded %d samples from SPIFFS", histCount);
}


// ── Week: hourly averages, 7 days ─────────────────────────────────────
// The 2-min samples are averaged per clock hour (pc::HourAcc) into a second ring
// of 168 records, kept in /hist_week.bin (appended once an hour; rewritten with
// the last 7 days when it grows past 14 days). GET /api/history?hours=25..168
// serves these, plus the hour in progress.
static const int   WEEK_SIZE = 168;
static pc::HourRecord week[WEEK_SIZE];
static int weekHead = 0, weekCount = 0;
static pc::HourAcc weekAcc;                         // the hour in progress
static const char* WEEK_FILE = "/hist_week.bin";
static const uint8_t WEEK_MAGIC[4] = { 'P', 'H', 'W', '1' };
static int weekFileCount = 0;

static uint32_t weekLastT() { return weekCount ? week[(weekHead - 1 + WEEK_SIZE) % WEEK_SIZE].t : 0; }
static void weekPush(const pc::HourRecord& r)
{
    week[weekHead] = r;
    weekHead = (weekHead + 1) % WEEK_SIZE;
    if (weekCount < WEEK_SIZE) weekCount++;
}
static void weekWriteAll()
{
    File f = SPIFFS.open(WEEK_FILE, FILE_WRITE);
    if (!f) return;
    f.write(WEEK_MAGIC, sizeof(WEEK_MAGIC)); uint8_t sz = sizeof(pc::HourRecord); f.write(&sz, 1);
    int start = (weekHead - weekCount + WEEK_SIZE) % WEEK_SIZE;
    for (int i = 0; i < weekCount; i++) f.write((const uint8_t*)&week[(start + i) % WEEK_SIZE], sizeof(pc::HourRecord));
    f.close();
    weekFileCount = weekCount;
}
static void weekAppend(const pc::HourRecord& r)
{
    if (!spiffsMounted) return;
    if (weekFileCount == 0 || weekFileCount >= 2 * WEEK_SIZE || !SPIFFS.exists(WEEK_FILE)) { weekWriteAll(); return; }
    File f = SPIFFS.open(WEEK_FILE, FILE_APPEND);
    if (!f) return;
    if (f.write((const uint8_t*)&r, sizeof(r)) == sizeof(r)) weekFileCount++;
    f.close();
}
static pc::HourInput weekInput(const HistSample& s)
{
    pc::HourInput in = {};
    in.t = s.t; in.room10 = s.room10; in.stove10 = s.stove10; in.remote10 = s.remote10;
    in.setpoint = s.setpoint; in.fume = s.fume; in.flame = s.flame; in.flameActual = s.flameActual;
    in.fan = s.fan; in.fanActual = s.fanActual; in.state = s.state;
    in.auger10 = s.auger10; in.fumeRpm = s.fumeRpm == 0xFFFF ? -1 : s.fumeRpm;
    return in;
}
// Feed one 2-min sample; closes the previous hour when a new one starts
static void weekAdd(const HistSample& s, bool persist)
{
    uint32_t h = pc::HourAcc::hourOf(s.t);
    if (!weekAcc.empty() && weekAcc.hour != h) {
        if (weekAcc.hour > weekLastT()) {
            pc::HourRecord r = weekAcc.result();
            weekPush(r);
            if (persist) weekAppend(r);
        }
        weekAcc.reset();
    }
    if (h > weekLastT()) weekAcc.add(weekInput(s));
}
// At boot, after historyLoad(): read the file, then add the hours from the 24 h
// history that are missing (first start with this firmware, or hours that ended
// while the controller was off).
static void weekLoad()
{
    if (spiffsMounted) {
        File f = SPIFFS.open(WEEK_FILE, FILE_READ);
        if (f) {
            uint8_t hdr[5];
            bool ok = f.read(hdr, 5) == 5 && memcmp(hdr, WEEK_MAGIC, 4) == 0 && hdr[4] == sizeof(pc::HourRecord);
            uint32_t cutoff = (uint32_t)time(nullptr) > 1700000000 ? (uint32_t)time(nullptr) - 7UL * 24UL * 3600UL : 0;
            pc::HourRecord r;
            int n = 0;
            while (ok && f.read((uint8_t*)&r, sizeof(r)) == sizeof(r)) {
                n++;
                if (r.t < 1700000000 || (cutoff && r.t < cutoff) || r.t <= weekLastT()) continue;
                weekPush(r);
            }
            f.close();
            if (!ok) SPIFFS.remove(WEEK_FILE);
            weekFileCount = ok ? n : 0;
        }
    }
    int added = weekCount;
    int start = (histHead - histCount + HIST_SIZE) % HIST_SIZE;
    for (int i = 0; i < histCount; i++) weekAdd(hist[(start + i) % HIST_SIZE], false);
    // the hour in progress stays in weekAcc; finished hours that were rebuilt go to the file
    if (weekCount != added && spiffsMounted) weekWriteAll();
    LOGI("history", "%d hours of week history", weekCount);
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
    else LOGW("history", "append to %s failed (SPIFFS %u of %u bytes used)", HIST_FILES[histActiveFile], (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
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
    weekAdd(s, true);
}

// Hourly records for ranges over 24 h (up to 7 days), same field names as the
// 2-min samples where they mean the same, plus fumeMax and burning (% of the hour
// in a heating state). The values are hourly averages; state = most frequent.
static void handleRouteHistoryWeek(int hours)
{
    uint32_t since = (uint32_t)time(nullptr) - (uint32_t)hours * 3600UL;
    wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    wm.server->send(200, "application/json", "");
    String out;
    out.reserve(1800);
    out += "{\"interval\":3600,\"fields\":[\"t\",\"room\",\"stove\",\"remote\",\"setpoint\",\"fume\",\"fumeMax\",\"flame\",\"flameActual\",\"fan\",\"fanActual\",\"auger\",\"fumeFan\",\"state\",\"burning\",\"n\"],\"samples\":[";
    bool first = true;
    auto row = [&](const pc::HourRecord& r) {
        if (r.t + 3600 <= since) return;
        char line[176], fa[8], fna[8], aug[10], rpm[8];
        if (r.flameAct10 == 0xFF) strcpy(fa, "null"); else snprintf(fa, sizeof(fa), "%.1f", r.flameAct10 / 10.0f);
        if (r.fanAct10 == 0xFF) strcpy(fna, "null"); else snprintf(fna, sizeof(fna), "%.1f", r.fanAct10 / 10.0f);
        if (r.auger10 < 0) strcpy(aug, "null"); else snprintf(aug, sizeof(aug), "%.1f", r.auger10 / 10.0f);
        if (r.fumeRpm == 0xFFFF) strcpy(rpm, "null"); else snprintf(rpm, sizeof(rpm), "%u", r.fumeRpm);
        snprintf(line, sizeof(line), "%s[%lu,%.1f,%.1f,%.1f,%u,%u,%u,%.1f,%s,%.1f,%s,%s,%s,%d,%u,%u]", first ? "" : ",",
                 (unsigned long)r.t, r.room10 / 10.0f, r.stove10 / 10.0f, r.remote10 / 10.0f, r.setpoint, r.fume, r.fumeMax,
                 r.flame10 / 10.0f, fa, r.fan10 / 10.0f, fna, aug, rpm, r.state, r.burnPct, r.n);
        out += line;
        first = false;
        if (out.length() > 1500) { wm.server->sendContent(out); out = ""; }
    };
    int start = (weekHead - weekCount + WEEK_SIZE) % WEEK_SIZE;
    for (int i = 0; i < weekCount; i++) row(week[(start + i) % WEEK_SIZE]);
    if (!weekAcc.empty()) row(weekAcc.result());   // the hour in progress
    out += "]}";
    wm.server->sendContent(out);
    wm.server->sendContent("");
}

void handleRouteHistory()
{
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    int hours = wm.server->hasArg("hours") ? wm.server->arg("hours").toInt() : 24;
    if (hours > 24) { handleRouteHistoryWeek(hours > 168 ? 168 : hours); return; }
    if (hours < 1) hours = 24;
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
