#pragma once
// Event log: stove state changes, confirmed errors and pellet reserve changes
// (/api/events), kept on SPIFFS.
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

// ---------- stove state change and error log ----------
// Besides state changes, every change of the active error (RAM 0x33) is logged:
// kind EVT_ERROR with the raw error value (0 = error cleared). The stove itself does
// not store when an error happened, so this is the only source of error times.
// Every change of the stove state (RAM 0x21, refreshed ~1 s by the sensor task) is
// recorded with its exact time. The last EVT_SIZE changes are kept in RAM and
// persisted in /events.bin (one small append per change, so very few flash writes;
// the file is compacted when it reaches 2 * EVT_SIZE records).
enum : uint8_t { EVT_STATE = 0, EVT_ERROR = 1, EVT_PELLET = 2, EVT_LINK = 3, EVT_SOURCE = 4, EVT_POWER = 5 };   // EVT_PELLET: code = RAM address, from/to = values
// EVT_SOURCE: the sensor the stove regulates on changed, from / to = pc::RoomSource (1 stove, 2 remote
// control), code = the room temperature x 2 at that moment
// EVT_LINK: serial link to the stove lost (to = 0) or back (to = 1)
// EVT_POWER: power source of the board switched, from / to = 1 stove, 2 USB (powerSource.h)
struct __attribute__((packed)) StateEvent { uint32_t t; uint8_t kind; int8_t from; int8_t to; uint8_t code; };
struct __attribute__((packed)) StateEventV1 { uint32_t t; int8_t from; int8_t to; };   // PHE1 file layout
static const int      EVT_SIZE = 200;
static StateEvent     evts[EVT_SIZE];
static int            evtHead = 0, evtCount = 0, evtFileCount = 0;
static const char*    EVT_FILE = "/events.bin";
static const uint8_t  EVT_MAGIC[4] = { 'P', 'H', 'E', '2' };   // v2: + kind, code
static const size_t   EVT_HDR = sizeof(EVT_MAGIC) + 1;
static int            evtLastState = -1;   // last known state (from the log or the bus)
static int            evtLastError = 0;    // last logged error value (0 = none)
static int            evtLastPellet[2] = { -1, -1 };   // last logged RAM 0xA4 / 0x89
static int            evtLastLink = -1;    // last logged serial link state (0 down, 1 up, -1 none)
static int            evtLastSource = 0;   // last logged room temperature source (0 none)

static void evtPush(const StateEvent& e)
{
    evts[evtHead] = e;
    evtHead = (evtHead + 1) % EVT_SIZE;
    if (evtCount < EVT_SIZE) evtCount++;
}

static void evtWriteAll()
{
    File f = SPIFFS.open(EVT_FILE, FILE_WRITE);
    if (!f) return;
    uint8_t sz = sizeof(StateEvent);
    f.write(EVT_MAGIC, sizeof(EVT_MAGIC)); f.write(&sz, 1);
    int start = (evtHead - evtCount + EVT_SIZE) % EVT_SIZE;
    for (int i = 0; i < evtCount; i++) f.write((const uint8_t*)&evts[(start + i) % EVT_SIZE], sizeof(StateEvent));
    f.close();
    evtFileCount = evtCount;
}

static void eventsLoad()
{
    if (!spiffsMounted) return;
    File f = SPIFFS.open(EVT_FILE, FILE_READ);
    if (!f) return;
    uint8_t hdr[EVT_HDR];
    int ver = 0;
    if (f.read(hdr, EVT_HDR) == EVT_HDR && memcmp(hdr, EVT_MAGIC, 3) == 0) {
        if (hdr[3] == '2' && hdr[4] == sizeof(StateEvent))   ver = 2;
        if (hdr[3] == '1' && hdr[4] == sizeof(StateEventV1)) ver = 1;
    }
    if (!ver) { f.close(); SPIFFS.remove(EVT_FILE); return; }
    evtFileCount = 0;
    if (ver == 2) {
        StateEvent e;
        while (f.read((uint8_t*)&e, sizeof(e)) == sizeof(e)) { evtPush(e); evtFileCount++; }
    } else {
        StateEventV1 o;
        while (f.read((uint8_t*)&o, sizeof(o)) == sizeof(o)) { StateEvent e = { o.t, EVT_STATE, o.from, o.to, 0 }; evtPush(e); }
    }
    f.close();
    // Drop short pre-warnings logged by older firmware (error cleared again within
    // ERROR_CONFIRM_MS, see updateConfirmedError), together with their "cleared" entry
    bool dropped = false;
    {
        StateEvent* keep = (StateEvent*)malloc(EVT_SIZE * sizeof(StateEvent));   // only while loading
        if (!keep) return;
        int n = 0, start = (evtHead - evtCount + EVT_SIZE) % EVT_SIZE;
        bool skipClear = false;
        for (int i = 0; i < evtCount; i++) {
            const StateEvent& e = evts[(start + i) % EVT_SIZE];
            if (e.kind == EVT_ERROR && e.code != 0) {
                uint32_t clearedAt = 0;   // next error entry, if it clears this one
                for (int j = i + 1; j < evtCount; j++) {
                    const StateEvent& n2 = evts[(start + j) % EVT_SIZE];
                    if (n2.kind == EVT_ERROR) { if (n2.code == 0) clearedAt = n2.t; break; }
                }
                if (clearedAt && clearedAt - e.t < ERROR_CONFIRM_MS / 1000) { skipClear = true; dropped = true; continue; }
            }
            if (e.kind == EVT_ERROR && e.code == 0 && skipClear) { skipClear = false; continue; }
            // the pellet reserve counter (RAM 0x89) is no longer logged: it changed every
            // minute during the reserve and pushed the useful entries out
            if (e.kind == EVT_PELLET && e.code == palletLevelAddr) { dropped = true; continue; }
            keep[n++] = e;
        }
        if (dropped) { evtHead = 0; evtCount = 0; for (int i = 0; i < n; i++) evtPush(keep[i]); }
        free(keep);
    }
    if (ver == 1 || dropped) evtWriteAll();   // convert / rewrite the file
    bool gotState = false, gotError = false;
    for (int i = 0; i < evtCount && !(gotState && gotError); i++) {   // newest state and error, walking back
        const StateEvent& e = evts[(evtHead - 1 - i + 2 * EVT_SIZE) % EVT_SIZE];
        if (e.kind == EVT_STATE && !gotState) { evtLastState = e.to; gotState = true; }
        if (e.kind == EVT_ERROR && !gotError) { evtLastError = e.code; gotError = true; }
        if (e.kind == EVT_LINK && evtLastLink == -1) evtLastLink = e.to;
        if (e.kind == EVT_SOURCE && !evtLastSource) evtLastSource = e.to;
        if (e.kind == EVT_PELLET) {
            int k = e.code == palletSensorAddr ? 0 : e.code == palletLevelAddr ? 1 : -1;
            if (k >= 0 && evtLastPellet[k] == -1) evtLastPellet[k] = e.to;
        }
    }
}

// Probable cause of a state change, derived from what the controller knows: a
// recent power command (with its source), a timer program that matches the time,
// energy saving and the room temperature, the brazier cleaning interval or an
// error. It is an interpretation, the stove does not report the cause.
static String stateChangeReason(int from, int to)
{
    char b[160] = "";
    bool recent = lastStateWriteMs && millis() - lastStateWriteMs < 60000;
    time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
    int nowMin = tm.tm_hour * 60 + tm.tm_min, dayBit = 1 << ((tm.tm_wday + 6) % 7);   // bit 0 = Monday
    auto timerAt = [&](bool on) -> int {
        for (int i = 0; i < 6; i++) {
            if (!cachedTimerEnabled[i] || !(cachedTimerDowMask[i] & dayBit)) continue;
            int v = on ? cachedTimerOnVal[i] : cachedTimerOffVal[i];
            if (v < 0 || v >= 144) continue;              // OFF
            int m = v * 10;                               // 10-minute steps
            if (nowMin >= m && nowMin - m <= 10) return i;
        }
        return -1;
    };
    int esStopAt = (int)setPointTemp + energySavingStop, esStartAt = (int)setPointTemp + energySavingStart;
    switch (to) {
    case 1: {
        int t = timerAt(true);
        if (recent && lastStateWriteVal == 1) snprintf(b, sizeof(b), "switched on by %s", lastStateWriteSrc);
        else if (t >= 0) snprintf(b, sizeof(b), "timer PR%d (on at %02d:%02d)", t + 1, cachedTimerOnVal[t] * 10 / 60, cachedTimerOnVal[t] * 10 % 60);
        else if (from == 7 && energySavingStop) snprintf(b, sizeof(b), "energy saving restart: room %.1f °C, restart at %d °C", ambTemp, esStartAt);
        else snprintf(b, sizeof(b), "on the remote control or by the stove itself");
        break;
    }
    case 7:
        if (energySavingStop) snprintf(b, sizeof(b), "energy saving: room %.1f °C, stop at %d °C (set point %d + %d)", ambTemp, esStopAt, (int)setPointTemp, energySavingStop);
        else snprintf(b, sizeof(b), "standby");
        break;
    case 6: case 0: {
        if (from == 6 && to == 0) break;                  // end of the shut-down: nothing to explain
        int t = timerAt(false);
        if (recent && (lastStateWriteVal == 6 || lastStateWriteVal == 0)) snprintf(b, sizeof(b), "switched off by %s", lastStateWriteSrc);
        // pellet reserve phase 2 (level sensor 0xA4 = 5, countdown 0x89 at 0, 0x91 running):
        // the stove switches itself off when the PELLET RESERVE time is over
        else if (pc::reserveSensorLow(cachedPelletSensor) && cachedPelletCounter == 0)
            snprintf(b, sizeof(b), "pellet reserve finished: pellets used up (PELLET RESERVE countdown ended)");
        else if (t >= 0) snprintf(b, sizeof(b), "timer PR%d (off at %02d:%02d)", t + 1, cachedTimerOffVal[t] * 10 / 60, cachedTimerOffVal[t] * 10 % 60);
        else snprintf(b, sizeof(b), "on the remote control or by the stove itself");
        break;
    }
    case 5:
        if (cachedBrazierInterval > 0) snprintf(b, sizeof(b), "periodic brazier cleaning (every %d min of burning)", cachedBrazierInterval);
        else snprintf(b, sizeof(b), "periodic brazier cleaning");
        break;
    case 8: case 9: case 10:
        if (cachedCurrentError > 0) snprintf(b, sizeof(b), "alarm: %s %s", pc::errorCode(cachedCurrentError), pc::errorDesc(cachedCurrentError));
        break;
    }
    return String(b);
}

static void evtStore(const StateEvent& e);
static void eventsTick()
{
    // serial link to the stove: lost / back. "Back" needs a real answer (a full read),
    // so a restart without the stove does not log a false "back".
    {
        time_t tn = time(nullptr);
        int cur = stove.linkDown ? 0 : (lastFastStatesAt ? 1 : -1);
        if (tn > 1700000000 && cur >= 0 && cur != evtLastLink && !(cur == 1 && evtLastLink == -1)) {
            StateEvent e = { (uint32_t)tn, EVT_LINK, (int8_t)evtLastLink, (int8_t)cur, 0 };
            evtLastLink = cur;
            evtStore(e);
        }
    }
    if (lastFastStatesAt == 0) return;                 // no data from the stove yet
    updateConfirmedError(stoveState);
    {   // the sensor the stove regulates on (stoveCache.h): stove sensor <-> remote control
        time_t tn = time(nullptr);
        if (tn > 1700000000 && cachedRoomSource != pc::RS_UNKNOWN && cachedRoomSource != evtLastSource) {
            if (evtLastSource) {   // not at the first determination after a start
                StateEvent e = { (uint32_t)tn, EVT_SOURCE, (int8_t)evtLastSource, (int8_t)cachedRoomSource, (uint8_t)(ambTemp * 2) };
                evtStore(e);
            }
            evtLastSource = cachedRoomSource;
        }
    }
    time_t t = time(nullptr);
    if (t < 1700000000) return;                        // no valid time yet: wait, changes are logged once NTP is in
    int s = stoveState;
    // Brazier cleaning (5) is a short periodic step while working (every ~45 min):
    // not an event, it would only clutter the log and the chart. Only in the firmware log (debug).
    if (s == 5 || (s == 4 && evtLastState == 5)) {
        static int lastSeen = -1;
        if (s != lastSeen) LOGD("stove", "state %s", pc::stoveStateName(s));
        lastSeen = s;
        if (evtLastState == 5) evtLastState = 4;   // an old event ended in 5: continue as working
        s = 4;
    }
    if (s >= 0 && s <= 20 && s != evtLastState) {
        static uint32_t stateSince = 0;   // time of the previous change seen in this run
        char dur[24] = "";
        if (stateSince) { uint32_t d = (uint32_t)t - stateSince; snprintf(dur, sizeof(dur), " after %lu min %02lu s", (unsigned long)(d / 60), (unsigned long)(d % 60)); }
        String why = stateChangeReason(evtLastState, s);
        if (evtLastState >= 0) logPrintf(s >= 8 && s <= 10 ? LOG_ERROR : LOG_INFO,   // alarm states are errors
                                         "stove", "state %s -> %s%s%s%s", pc::stoveStateName(evtLastState), pc::stoveStateName(s), dur,
                                         why.length() ? " - probable cause: " : "", why.c_str());
        else LOGI("stove", "state %s", pc::stoveStateName(s));
        stateSince = (uint32_t)t;
        StateEvent e = { (uint32_t)t, EVT_STATE, (int8_t)evtLastState, (int8_t)s, 0 };
        evtLastState = s;
        evtStore(e);
    }
    // pellet reserve: the level sensor (RAM 0xA4) goes to the event log; the countdown
    // (RAM 0x89, about one step a minute during the reserve) only to the debug log
    const int pv[2] = { cachedPelletSensor, cachedPelletCounter };
    const uint8_t pa[2] = { palletSensorAddr, palletLevelAddr };
    for (int k = 0; k < 2; k++) {
        if (pv[k] < 0 || pv[k] > 127 || pv[k] == evtLastPellet[k]) continue;
        if (k == 1) {
            if (evtLastPellet[k] >= 0) LOGD("stove", "pellet reserve counter (RAM 0x89) %d -> %d", evtLastPellet[k], pv[k]);
            evtLastPellet[k] = pv[k];
            continue;
        }
        if (evtLastPellet[k] >= 0)
            LOGI("stove", "pellet reserve sensor (RAM 0x%02X) %d -> %d", pa[k], evtLastPellet[k], pv[k]);
        int stepWas = reserveStep.measured;
        reserveStep.update(pv[k], (uint32_t)t);
        if (reserveStep.measured != stepWas) LOGD("stove", "pellet reserve sensor: a step takes ~%d s", reserveStep.stepS);
        StateEvent e = { (uint32_t)t, EVT_PELLET, (int8_t)evtLastPellet[k], (int8_t)pv[k], pa[k] };
        evtLastPellet[k] = pv[k];
        evtStore(e);
    }
    int err = confirmedCurrentError;   // debounced: short pre-warnings are not logged
    if (err >= 0 && err <= 255 && err != evtLastError) {
        if (err) LOGE("stove", "error %s: %s (state %s)", pc::errorCode(err), pc::errorDesc(err), pc::stoveStateName(stoveState));
        else if (evtLastError > 0) LOGI("stove", "error %s cleared", pc::errorCode(evtLastError));
        StateEvent e = { (uint32_t)t, EVT_ERROR, -1, -1, (uint8_t)err };
        evtLastError = err;
        evtStore(e);
    }
}

static void evtStore(const StateEvent& e)
{
    evtPush(e);
    if (!spiffsMounted) return;
    if (evtFileCount >= 2 * EVT_SIZE || !SPIFFS.exists(EVT_FILE)) { evtWriteAll(); return; }
    File f = SPIFFS.open(EVT_FILE, FILE_APPEND);
    if (!f) return;
    f.write((const uint8_t*)&e, sizeof(e));
    f.close();
    evtFileCount++;
}

// GET /api/events[?hours=1..720] -> { events: [ {t, type:"state", from, to} | {t, type:"error", id, error}, ... ] } (oldest first)
// type "power" (board with power sense): {t, type:"power", from:"stove"|"usb", to}
void handleRouteEvents()
{
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    uint32_t since = 0;
    if (wm.server->hasArg("hours")) {
        int hours = wm.server->arg("hours").toInt();
        if (hours < 1 || hours > 720) { wm.server->send(400, "application/json; charset=utf-8", "{\"result\":\"failed - hours must be 1..720\"}"); return; }
        since = (uint32_t)time(nullptr) - (uint32_t)hours * 3600UL;
    }
    wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    wm.server->send(200, "application/json; charset=utf-8", "");
    String out = "{\"events\":[";
    bool first = true;
    int start = (evtHead - evtCount + EVT_SIZE) % EVT_SIZE;
    for (int i = 0; i < evtCount; i++) {
        const StateEvent& e = evts[(start + i) % EVT_SIZE];
        if (e.t < since) continue;
        char row[96], from[6];
        if (e.kind == EVT_PELLET) {
            if (e.from < 0) strcpy(from, "null"); else snprintf(from, sizeof(from), "%d", e.from);
            snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"pellet\",\"register\":\"0x%02X\",\"from\":%s,\"to\":%d}",
                     first ? "" : ",", (unsigned long)e.t, e.code, from, e.to);
        } else if (e.kind == EVT_LINK) {
            snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"link\",\"up\":%s}", first ? "" : ",", (unsigned long)e.t, e.to ? "true" : "false");
        } else if (e.kind == EVT_SOURCE) {
            snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"source\",\"from\":\"%s\",\"to\":\"%s\",\"room\":%.1f}", first ? "" : ",",
                     (unsigned long)e.t, e.from == 2 ? "remote" : "stove", e.to == 2 ? "remote" : "stove", e.code / 2.0);
#if BOARD_HAS_POWER_SENSE
        } else if (e.kind == EVT_POWER) {
            snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"power\",\"from\":\"%s\",\"to\":\"%s\"}", first ? "" : ",",
                     (unsigned long)e.t, e.from == 2 ? "usb" : "stove", e.to == 2 ? "usb" : "stove");
#endif
        } else if (e.kind == EVT_ERROR) {
            if (e.code == 0) snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"error\",\"id\":0,\"error\":null}", first ? "" : ",", (unsigned long)e.t);
            else snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"error\",\"id\":%u,\"error\":\"%s\"}", first ? "" : ",",
                          (unsigned long)e.t, e.code, getErrorCode(e.code).c_str());
        } else {
            if (e.from < 0) strcpy(from, "null"); else snprintf(from, sizeof(from), "%d", e.from);
            snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"state\",\"from\":%s,\"to\":%d}", first ? "" : ",", (unsigned long)e.t, from, e.to);
        }
        out += row; first = false;
        if (out.length() > 1500) httpSendChunk(out);
    }
    out += "]}";
    httpSendChunk(out);
    httpEndChunks();
}
