#pragma once
// Pellet consumption and the time left until the low pellet alarm.
//
// pc::PelletTracker (lib/pelletcore) counts "auger units" (auger value RAM 0x0D x
// seconds) and calibrates kg per unit from the level sensor (RAM 0xA4): between two
// trips of the sensor exactly the pellets added in between were burned. A refill
// is detected when the sensor clears again after a trip (default 15 kg, set with
// POST /api/pellets?refillKg=); topping up without an alarm is entered by hand
// (POST /api/pellets?add=kg).
//
// Kept in NVS "pellet" (blob), saved on a trip / refill / change and every 10 min.
// Runs in the loop task (pelletsTick), fed by the values the sensor task cached.
#include <Preferences.h>

static pc::PelletTracker pellets;
static bool     pelletsDirty = false;
static uint32_t pelletsSavedAt = 0;   // millis()
static double   pelletsSavedUnits = -1;

// Runtime diagnostics of the counter (not stored; /api/pellets "debug", web app debug tile):
// how often the loop fed the tracker, why it skipped, and gaps that were capped at MAX_STEP_S
struct PelletsDiag {
    uint32_t ticks = 0;           // readings fed to the tracker since the start
    uint32_t lastTickT = 0;       // epoch s of the last one
    uint32_t lastDt = 0;          // seconds between the last two readings
    uint32_t maxDt = 0;           // longest gap since the start
    uint32_t capped = 0;          // gaps longer than MAX_STEP_S (counted as MAX_STEP_S)
    uint32_t cappedLostS = 0;     // seconds not counted because of that
    uint32_t feedS = 0;           // seconds with the auger feeding since the start
    uint32_t preloadS = 0;        // of which with an auger value above the cycle (capped at 12 s)
    uint32_t skipTime = 0, skipLink = 0, skipState = 0, skipSensor = 0;
    const char* lastSkip = nullptr;
    uint32_t lastSkipMs = 0;
    double   unitsAtStart = -1;   // units when the controller started
    uint32_t startT = 0;          // epoch s of the first reading
    int      lastEvent = 0;       // 1 = trip, 2 = refill
    uint32_t lastEventT = 0;
};
static PelletsDiag pelDiag;

static void pelletsSave()
{
    Preferences p;
    p.begin("pellet", false);
    p.putBytes("s", &pellets, sizeof(pellets));
    p.end();
    pelletsDirty = false;
    pelletsSavedAt = millis();
    pelletsSavedUnits = pellets.units;
}

void pelletsLoad()
{
    Preferences p;
    p.begin("pellet", true);
    pc::PelletTracker t;
    size_t n = p.getBytesLength("s");
    // version 1 had no predKg at the end (same fields before it): read it and convert
    const size_t v1 = offsetof(pc::PelletTracker, predKg);
    bool v1ok = n >= v1 && n < v1 + 8 && n < sizeof(t) && p.getBytes("s", &t, n) == n && t.version == 1;
    if (v1ok) {
        for (int i = 0; i < pc::PelletTracker::MAX_CYCLES; i++) t.predKg[i] = NAN;
        t.version = pellets.version;
        LOGI("stove", "pellets: stored data converted from version 1");
    }
    if (v1ok || (n == sizeof(t) && p.getBytes("s", &t, sizeof(t)) == sizeof(t) && t.version == pellets.version)) {
        pellets = t;
        if (v1ok) pelletsDirty = true;
        pellets.lastT = 0;   // no units for the time the controller was off
        LOGI("stove", "pellets: %d calibration cycle(s), %s", pellets.nCycles,
             pellets.cycleStart < 0 ? "waiting for the first low pellet alarm" : pellets.low ? "pellets low, waiting for the refill" : "counting since the last refill");
    } else if (n) {
        LOGW("stove", "pellets: stored data has another layout (%u bytes) - starting again", (unsigned)n);
    }
    p.end();
    if (pelletsDirty) pelletsSave();
}

// Average auger value over the hourly records of the last 7 days (burning and not
// burning hours alike): the usual consumption rate. 0 = no data.
static float pelletsAvgAuger(int* hoursOut = nullptr)
{
    long sum = 0; int n = 0;
    int start = (weekHead - weekCount + WEEK_SIZE) % WEEK_SIZE;
    for (int i = 0; i < weekCount; i++) {
        const pc::HourRecord& r = week[(start + i) % WEEK_SIZE];
        if (r.auger10 < 0 || !r.n) continue;
        sum += r.auger10; n++;
    }
    if (hoursOut) *hoursOut = n;
    return n ? sum / 10.0f / n : 0;
}

void pelletsTick()
{
    static unsigned long seenFast = 0;
    if (lastFastStatesAt == seenFast) return;
    seenFast = lastFastStatesAt;
    time_t now = time(nullptr);
    const char* skip = now <= 1700000000 ? "clock not set" : stove.linkDown ? "no answer from the stove"
                     : stoveState == UNKNOWN ? "stove state unknown" : cachedPelletSensor < 0 ? "level sensor not read" : nullptr;
    if (skip) {
        if (now <= 1700000000) pelDiag.skipTime++; else if (stove.linkDown) pelDiag.skipLink++;
        else if (stoveState == UNKNOWN) pelDiag.skipState++; else pelDiag.skipSensor++;
        pelDiag.lastSkip = skip; pelDiag.lastSkipMs = millis();
        return;
    }
    if (pelDiag.unitsAtStart < 0) { pelDiag.unitsAtStart = pellets.units; pelDiag.startT = (uint32_t)now; }
    if (pellets.lastT && (uint32_t)now > pellets.lastT) {
        uint32_t dt = (uint32_t)now - pellets.lastT;
        pelDiag.lastDt = dt;
        if (dt > pelDiag.maxDt) pelDiag.maxDt = dt;
        if (cachedAugerSpeed > 0) {
            uint32_t c = dt > pc::PelletTracker::MAX_STEP_S ? pc::PelletTracker::MAX_STEP_S : dt;
            pelDiag.feedS += c;
            if (cachedAugerSpeed > pc::PelletTracker::AUGER_CYCLE_S) pelDiag.preloadS += c;
            if (dt > pc::PelletTracker::MAX_STEP_S) { pelDiag.capped++; pelDiag.cappedLostS += dt - c; }
        }
    }
    pelDiag.ticks++; pelDiag.lastTickT = (uint32_t)now;
    int ev = pellets.update((uint32_t)now, cachedPelletSensor, cachedAugerSpeed);
    if (ev) { pelDiag.lastEvent = ev; pelDiag.lastEventT = (uint32_t)now; }
    if (ev == pc::PelletTracker::TRIP) {
        double k = pellets.kgPerUnit();
        if (pellets.nCycles && k > 0) {
            const pc::PelletCycle& c = pellets.cycles[pellets.nCycles - 1];
            LOGI("stove", "pellets: low pellet alarm - cycle done: %.1f kg in %.0f auger units; now %.3f g per unit over %d cycle(s)",
                 c.kg, c.units, k * 1000.0, pellets.nCycles);
        } else {
            LOGI("stove", "pellets: low pellet alarm - start of the first measuring cycle (the estimate needs one refill and the next alarm)");
        }
        pelletsSave();
    } else if (ev == pc::PelletTracker::REFILL) {
        LOGI("stove", "pellets: refill detected (level sensor clear again): %.1f kg in the hopper above the sensor", pellets.cycleKg);
        pelletsSave();
    } else if (millis() - pelletsSavedAt > 10UL * 60UL * 1000UL && pellets.units != pelletsSavedUnits) {
        pelletsSave();   // the counter, every 10 min while the auger feeds
    }
}

// Summary for /api/state and the detail for /api/pellets
void pelletsJson(JsonObject o, bool detail)
{
    double k = pellets.kgPerUnit();
    double rem = pellets.remainingKg();
    o["calibrated"] = k > 0;
    o["cycles"] = pellets.nCycles;
    o["low"] = pellets.low != 0;
    o["overdue"] = pellets.overdue();   // past the estimate, the alarm did not come yet
    if (rem >= 0) o["remainingKg"] = serialized(String(rem, 1)); else o["remainingKg"] = nullptr;
    // until the low pellet alarm, at the current auger value (only while it feeds)
    double h = pellets.hoursAt(cachedAugerSpeed);
    if (h >= 0) o["hoursLeft"] = serialized(String(h, 2)); else o["hoursLeft"] = nullptr;
    // at the average use of the last 7 days: the expected time of the alarm
    int avgHours = 0;
    float avg = pelletsAvgAuger(&avgHours);
    double ha = pellets.hoursAt(avg);
    time_t now = time(nullptr);
    if (ha >= 0 && avgHours >= 6 && now > 1700000000) {
        o["hoursLeftAverage"] = serialized(String(ha, 1));
        o["alarmExpectedAt"] = (uint32_t)now + (uint32_t)(ha * 3600.0);
    } else { o["hoursLeftAverage"] = nullptr; o["alarmExpectedAt"] = nullptr; }
    if (k > 0) o["kgPerHour"] = cachedAugerSpeed > 0 ? serialized(String(k * cachedAugerSpeed * 3600.0, 2)) : serialized(String("0")); else o["kgPerHour"] = nullptr;
    o["refillKg"] = pellets.refillKg;
    // last refill: detected when the low pellet alarm cleared, or entered by hand
    if (pellets.lastRefillT) { o["lastRefillAt"] = pellets.lastRefillT; o["lastRefillKg"] = pellets.lastRefillKg; }
    else { o["lastRefillAt"] = nullptr; o["lastRefillKg"] = nullptr; }
    if (!detail) return;
    o["kgSinceAlarm"] = pellets.cycleStart >= 0 ? serialized(String(pellets.cycleKg, 1)) : serialized(String("null"));
    o["gramsPerUnit"] = k > 0 ? serialized(String(k * 1000.0, 4)) : serialized(String("null"));
    o["spread"] = pellets.nCycles >= 2 ? serialized(String(pellets.spread(), 3)) : serialized(String("null"));
    o["averageAuger"] = avgHours ? serialized(String(avg, 2)) : serialized(String("null"));
    o["averageHours"] = avgHours;
    o["unitsSinceAlarm"] = pellets.cycleStart >= 0 ? serialized(String(pellets.units - pellets.cycleStart, 0)) : serialized(String("null"));
    if (pellets.pendingKg > 0) o["pendingKg"] = pellets.pendingKg; else o["pendingKg"] = nullptr;
    // everything the estimate is made of, to check the calculation (web app debug tile)
    JsonObject d = o.createNestedObject("debug");
    uint32_t nowT = now > 1700000000 ? (uint32_t)now : 0;
    d["now"] = nowT;
    JsonObject in = d.createNestedObject("input");
    in["sensor"] = cachedPelletSensor;                              // RAM 0xA4
    in["sensorUse"] = cachedPelletSensor == 5 ? "low" : cachedPelletSensor == 0 ? "ok" : "ignored";
    in["auger"] = serialized(String(cachedAugerSpeed, 1));          // RAM 0x0D, s on-time per cycle
    in["augerCounted"] = serialized(String(cachedAugerSpeed > pc::PelletTracker::AUGER_CYCLE_S ? pc::PelletTracker::AUGER_CYCLE_S : (cachedAugerSpeed > 0 ? cachedAugerSpeed : 0), 1));
    in["stoveState"] = stoveState;
    in["linkDown"] = stove.linkDown;
    in["dataAgeS"] = lastFastStatesAt ? (millis() - lastFastStatesAt) / 1000 : -1;
    JsonObject tr = d.createNestedObject("tracker");                 // stored in NVS "pellet"
    tr["version"] = pellets.version;
    tr["units"] = serialized(String(pellets.units, 1));
    tr["cycleStart"] = serialized(String(pellets.cycleStart, 1));
    tr["cycleKg"] = serialized(String(pellets.cycleKg, 2));
    tr["low"] = pellets.low;
    tr["lowSince"] = pellets.lowSince;
    tr["lowUnits"] = serialized(String(pellets.lowUnits, 1));
    tr["okSince"] = pellets.okSince;
    tr["lastT"] = pellets.lastT;
    tr["pendingKg"] = pellets.pendingKg;
    tr["refillKg"] = pellets.refillKg;
    tr["nCycles"] = pellets.nCycles;
    JsonObject cs = d.createNestedObject("constants");
    cs["lowConfirmS"] = pc::PelletTracker::LOW_CONFIRM_S;
    cs["refillConfirmS"] = pc::PelletTracker::REFILL_CONFIRM_S;
    cs["maxStepS"] = pc::PelletTracker::MAX_STEP_S;
    cs["augerCycleS"] = pc::PelletTracker::AUGER_CYCLE_S;
    cs["maxCycles"] = pc::PelletTracker::MAX_CYCLES;
    cs["recentWeight"] = pc::PelletTracker::RECENT_WEIGHT;
    JsonObject ca = d.createNestedObject("calc");
    if (k > 0) ca["kgPerUnit"] = serialized(String(k, 9)); else ca["kgPerUnit"] = nullptr;
    if (pellets.cycleStart >= 0) {
        double used = pellets.units - pellets.cycleStart;
        ca["unitsSinceAlarm"] = serialized(String(used, 1));
        // running time of the auger motor: on-time / cycle x time = units / 12 s
        ca["motorHoursSinceAlarm"] = serialized(String(used / pc::PelletTracker::AUGER_CYCLE_S / 3600.0, 2));
        if (k > 0) {
            ca["kgBurnedSinceAlarm"] = serialized(String(k * used, 2));
            ca["remainingUnclamped"] = serialized(String(pellets.cycleKg - k * used, 2));   // < 0: more burned than added
        }
    }
    ca["unitsPerHourNow"] = serialized(String((cachedAugerSpeed > pc::PelletTracker::AUGER_CYCLE_S ? pc::PelletTracker::AUGER_CYCLE_S : (cachedAugerSpeed > 0 ? cachedAugerSpeed : 0)) * 3600.0, 0));
    if (k > 0 && avgHours) ca["kgPerHourAverage"] = serialized(String(k * avg * 3600.0, 3));
    JsonObject lp = d.createNestedObject("loop");                    // since the controller started
    lp["ticks"] = pelDiag.ticks;
    lp["lastTickAgoS"] = pelDiag.lastTickT && nowT >= pelDiag.lastTickT ? (int32_t)(nowT - pelDiag.lastTickT) : -1;
    lp["lastDtS"] = pelDiag.lastDt;
    lp["maxDtS"] = pelDiag.maxDt;
    lp["capped"] = pelDiag.capped;
    lp["cappedLostS"] = pelDiag.cappedLostS;
    lp["feedS"] = pelDiag.feedS;
    lp["preloadS"] = pelDiag.preloadS;
    lp["startT"] = pelDiag.startT;
    if (pelDiag.unitsAtStart >= 0) lp["unitsSinceStart"] = serialized(String(pellets.units - pelDiag.unitsAtStart, 1)); else lp["unitsSinceStart"] = nullptr;
    JsonObject sk = lp.createNestedObject("skipped");
    sk["clock"] = pelDiag.skipTime; sk["link"] = pelDiag.skipLink; sk["state"] = pelDiag.skipState; sk["sensor"] = pelDiag.skipSensor;
    if (pelDiag.lastSkip) { lp["lastSkip"] = pelDiag.lastSkip; lp["lastSkipAgoS"] = (millis() - pelDiag.lastSkipMs) / 1000; }
    else { lp["lastSkip"] = nullptr; lp["lastSkipAgoS"] = nullptr; }
    lp["lastEvent"] = pelDiag.lastEvent == pc::PelletTracker::TRIP ? "trip" : pelDiag.lastEvent == pc::PelletTracker::REFILL ? "refill" : nullptr;
    lp["lastEventT"] = pelDiag.lastEventT;
    JsonObject sv = d.createNestedObject("save");
    sv["agoS"] = pelletsSavedAt ? (millis() - pelletsSavedAt) / 1000 : -1;
    sv["unsavedUnits"] = serialized(String(pelletsSavedUnits >= 0 ? pellets.units - pelletsSavedUnits : 0, 1));
    sv["bytes"] = (uint32_t)sizeof(pellets);
    JsonArray arr = o.createNestedArray("history");
    for (int i = 0; i < pellets.nCycles; i++) {
        JsonObject c = arr.createNestedObject();
        c["t"] = pellets.cycles[i].t;
        c["kg"] = pellets.cycles[i].kg;
        c["units"] = (uint32_t)pellets.cycles[i].units;
        c["gramsPerUnit"] = serialized(String(pellets.cycles[i].kg / pellets.cycles[i].units * 1000.0, 4));
        c["weight"] = serialized(String(pellets.cycleWeight(i), 3));
        // kg the estimate still had left at this alarm: > 0 the alarm came early, < 0 late
        if (isnan(pellets.predKg[i])) c["predKg"] = nullptr; else c["predKg"] = serialized(String(pellets.predKg[i], 2));
    }
}

// GET  /api/pellets                     -> estimate, calibration and its cycles
// POST /api/pellets?add=<kg>            -> pellets added by hand (topped up without an alarm,
//                                          or another amount than refillKg for the coming refill)
// POST /api/pellets?refillKg=<kg>       -> amount of an automatically detected refill
// POST /api/pellets?resetCalibration=true -> forget the measured cycles
void handleRoutePellets()
{
    int code = 200;
    String err;
    if (wm.server->method() == HTTP_POST) {
        if (wm.server->hasArg("refillKg")) {
            float v = wm.server->arg("refillKg").toFloat();
            if (v < 1 || v > 100) err = "refillKg must be 1 - 100";
            else { pellets.refillKg = v; pelletsDirty = true; LOGI("stove", "pellets: refill amount set to %.1f kg by %s", v, clientIp().c_str()); }
        }
        if (!err.length() && wm.server->hasArg("add")) {
            float v = wm.server->arg("add").toFloat();
            if (v <= 0 || v > 100) err = "add must be 0 - 100 kg";
            else {
                pellets.addKg(v, (uint32_t)time(nullptr)); pelletsDirty = true;
                LOGI("stove", "pellets: %.1f kg added by hand (%s) by %s", v,
                     pellets.low ? "used for the refill when the sensor clears" : "topped up", clientIp().c_str());
            }
        }
        if (!err.length() && pc::parseOnOff(wm.server->arg("resetCalibration").c_str()) == 1) {
            pellets.resetCalibration(); pelletsDirty = true;
            LOGI("stove", "pellets: calibration reset by %s", clientIp().c_str());
        }
        if (pelletsDirty) pelletsSave();
        if (err.length()) code = 400;
    } else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    DynamicJsonDocument doc(4096);   // the debug object and 10 cycles
    JsonObject o = doc.to<JsonObject>();
    if (err.length()) o["result"] = "failed - " + err;
    else if (wm.server->method() == HTTP_POST) o["result"] = "success";
    pelletsJson(o, true);
    String out; serializeJson(doc, out);
    wm.server->send(code, "application/json", out);
}
