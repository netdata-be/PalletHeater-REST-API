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
enum : uint8_t { EVT_STATE = 0, EVT_ERROR = 1, EVT_PELLET = 2 };   // EVT_PELLET: code = RAM address, from/to = values
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
        static StateEvent keep[EVT_SIZE];
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
            keep[n++] = e;
        }
        if (dropped) { evtHead = 0; evtCount = 0; for (int i = 0; i < n; i++) evtPush(keep[i]); }
    }
    if (ver == 1 || dropped) evtWriteAll();   // convert / rewrite the file
    bool gotState = false, gotError = false;
    for (int i = 0; i < evtCount && !(gotState && gotError); i++) {   // newest state and error, walking back
        const StateEvent& e = evts[(evtHead - 1 - i + 2 * EVT_SIZE) % EVT_SIZE];
        if (e.kind == EVT_STATE && !gotState) { evtLastState = e.to; gotState = true; }
        if (e.kind == EVT_ERROR && !gotError) { evtLastError = e.code; gotError = true; }
        if (e.kind == EVT_PELLET) {
            int k = e.code == palletSensorAddr ? 0 : e.code == palletLevelAddr ? 1 : -1;
            if (k >= 0 && evtLastPellet[k] == -1) evtLastPellet[k] = e.to;
        }
    }
}

static void evtStore(const StateEvent& e);
static void eventsTick()
{
    if (lastFastStatesAt == 0) return;                 // no data from the stove yet
    updateConfirmedError(stoveState);
    time_t t = time(nullptr);
    if (t < 1700000000) return;                        // no valid time yet: wait, changes are logged once NTP is in
    int s = stoveState;
    if (s >= 0 && s <= 20 && s != evtLastState) {
        StateEvent e = { (uint32_t)t, EVT_STATE, (int8_t)evtLastState, (int8_t)s, 0 };
        evtLastState = s;
        evtStore(e);
    }
    const int pv[2] = { cachedPelletSensor, cachedPelletCounter };
    const uint8_t pa[2] = { palletSensorAddr, palletLevelAddr };
    for (int k = 0; k < 2; k++) {
        if (pv[k] < 0 || pv[k] > 127 || pv[k] == evtLastPellet[k]) continue;
        StateEvent e = { (uint32_t)t, EVT_PELLET, (int8_t)evtLastPellet[k], (int8_t)pv[k], pa[k] };
        evtLastPellet[k] = pv[k];
        evtStore(e);
    }
    int err = confirmedCurrentError;   // debounced: short pre-warnings are not logged
    if (err >= 0 && err <= 255 && err != evtLastError) {
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
void handleRouteEvents()
{
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    uint32_t since = 0;
    if (wm.server->hasArg("hours")) {
        int hours = wm.server->arg("hours").toInt();
        if (hours < 1 || hours > 720) { wm.server->send(400, "application/json", "{\"result\":\"failed - hours must be 1..720\"}"); return; }
        since = (uint32_t)time(nullptr) - (uint32_t)hours * 3600UL;
    }
    wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    wm.server->send(200, "application/json", "");
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
        } else if (e.kind == EVT_ERROR) {
            if (e.code == 0) snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"error\",\"id\":0,\"error\":null}", first ? "" : ",", (unsigned long)e.t);
            else snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"error\",\"id\":%u,\"error\":\"%s\"}", first ? "" : ",",
                          (unsigned long)e.t, e.code, getErrorCode(e.code).c_str());
        } else {
            if (e.from < 0) strcpy(from, "null"); else snprintf(from, sizeof(from), "%d", e.from);
            snprintf(row, sizeof(row), "%s{\"t\":%lu,\"type\":\"state\",\"from\":%s,\"to\":%d}", first ? "" : ",", (unsigned long)e.t, from, e.to);
        }
        out += row; first = false;
        if (out.length() > 1500) { wm.server->sendContent(out); out = ""; }
    }
    out += "]}";
    wm.server->sendContent(out);
    wm.server->sendContent("");
}
