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

// ── Use per day and cost (pc::PelletUsage / pc::PelletCost) ──────────────────────────────
// Auger units per local day on SPIFFS (/pellet_days.bin, rewritten once a day and at the first
// start), price per bag, kg per bag and the bags in stock in NVS "pelcost". A refill takes its
// kg from the stock.
static pc::PelletUsage pelUse;
static pc::PelletCost  pelCost;
static const char*     PELDAYS_FILE = "/pellet_days.bin";
static const uint8_t   PELDAYS_MAGIC[4] = { 'P', 'P', 'D', '1' };
static bool            pelUseLoaded = false;

static uint32_t pelLocalDay(time_t now)
{
    if (now <= 1700000000) return 0;
    struct tm tm; localtime_r(&now, &tm);
    return (uint32_t)(tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}
static void pelUseSave()
{
    if (!spiffsMounted) return;
    File f = SPIFFS.open(PELDAYS_FILE, FILE_WRITE);
    if (!f) { LOGW("stove", "pellets: %s could not be written", PELDAYS_FILE); return; }
    f.write(PELDAYS_MAGIC, 4);
    f.write((const uint8_t*)&pelUse, sizeof(pelUse));
    f.close();
}
static void pelUseLoad()
{
    pelUseLoaded = true;
    if (!spiffsMounted || !SPIFFS.exists(PELDAYS_FILE)) return;
    File f = SPIFFS.open(PELDAYS_FILE, FILE_READ);
    uint8_t m[4];
    pc::PelletUsage u;
    if (f && f.read(m, 4) == 4 && !memcmp(m, PELDAYS_MAGIC, 4) && f.read((uint8_t*)&u, sizeof(u)) == sizeof(u)
        && u.version == pelUse.version && u.n >= 0 && u.n <= pc::PelletUsage::MAX_DAYS) {
        pelUse = u;
        LOGI("stove", "pellets: use of %d day(s) loaded", (int)pelUse.n);
    } else LOGW("stove", "pellets: %s has another layout - day totals start again", PELDAYS_FILE);
    if (f) f.close();
}
// Location for the outdoor temperature (web app: use against the weather, Open-Meteo); rounded
// to 0.01 deg (~1 km). The controller only keeps it; the browser fetches the weather.
static float pelLat = NAN, pelLon = NAN;
static void pelCostLoad()
{
    Preferences p; p.begin("pelcost", true);
    pelLat = p.getFloat("lat", NAN); pelLon = p.getFloat("lon", NAN);
    pelCost.pricePerBag = p.getFloat("price", 0);
    pelCost.bagKg       = p.getFloat("bagkg", 15);
    pelCost.stockBags   = p.getFloat("stock", -1);
    p.end();
}
static void pelCostSave()
{
    Preferences p; p.begin("pelcost", false);
    p.putFloat("price", pelCost.pricePerBag); p.putFloat("bagkg", pelCost.bagKg); p.putFloat("stock", pelCost.stockBags);
    p.putFloat("lat", pelLat); p.putFloat("lon", pelLon);
    p.end();
}
// First start with day totals: fill in the days before today from the hourly records of the last
// 7 days (history.h): average auger value x 3600 s per hour = the units of that hour.
static void pelUseBackfill(uint32_t today)
{
    if (pelUse.n || pelUse.curDay || !weekCount) return;
    uint32_t day = 0; double units = 0, todayUnits = 0; int filled = 0;
    for (int i = 0; i < weekCount; i++) {
        const pc::HourRecord& r = week[(weekHead - weekCount + i + WEEK_SIZE) % WEEK_SIZE];
        uint32_t d = pelLocalDay((time_t)r.t + 1800);
        if (!d || d > today) break;
        if (d == today) { if (r.auger10 > 0) todayUnits += min((int)r.auger10, 120) / 10.0 * 3600.0; continue; }
        if (d != day) {
            if (day) { pelUse.days[pelUse.head] = { day, (float)units }; pelUse.head = (pelUse.head + 1) % pc::PelletUsage::MAX_DAYS; pelUse.n++; filled++; }
            day = d; units = 0;
        }
        if (r.auger10 > 0) units += min((int)r.auger10, 120) / 10.0 * 3600.0;
    }
    if (day) { pelUse.days[pelUse.head] = { day, (float)units }; pelUse.head = (pelUse.head + 1) % pc::PelletUsage::MAX_DAYS; pelUse.n++; filled++; }
    if (filled) LOGI("stove", "pellets: %d day(s) filled in from the hourly history", filled);
    // today so far: the finished hours of today count as already burned today
    pelUse.curDay = today;
    pelUse.dayStart = pellets.units - todayUnits;
}

// kg used today and per day over the last (up to) 7 closed days; < 0 = not known (no calibration)
// (not static: also used by metrics.h, which is included earlier)
double pelKgToday() { double k = pellets.kgPerUnit(); return k > 0 ? k * pelUse.unitsToday(pellets.units) : -1; }
double pelStockBags() { return pelCost.stockBags; }
double pelKgPerDay(int days, int* got)
{
    double k = pellets.kgPerUnit(); int g = 0;
    double u = pelUse.unitsClosed(days, &g);
    if (got) *got = g;
    return k > 0 && g ? k * u / g : -1;
}
double pelKgPerDay7() { return pelKgPerDay(7, nullptr); }
double pelCostPerMonth() { double d = pelKgPerDay7(), ppk = pelCost.pricePerKg(); return d >= 0 && ppk > 0 ? d * 30.44 * ppk : -1; }


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
    pelCostLoad();
    pelUseLoad();
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
    // day totals: a new local day closes the previous one (saved once a day)
    uint32_t before = pelUse.curDay, today = pelLocalDay(now);
    if (!before && today) pelUseBackfill(today);
    if (pelUse.roll(today, pellets.units) || before != pelUse.curDay) pelUseSave();
    if (ev == pc::PelletTracker::REFILL && pelCost.stockBags >= 0) {
        pelCost.take(pellets.lastRefillKg); pelCostSave();
        LOGI("stove", "pellets: %.1f kg taken from the stock, %.1f bag(s) left", pellets.lastRefillKg, pelCost.stockBags);
    }
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
    // use per day and cost (also in /api/state: the web app status card)
    {
        double today = pelKgToday(), perDay = pelKgPerDay7(), ppk = pelCost.pricePerKg();
        if (today >= 0) o["kgToday"] = serialized(String(today, 1)); else o["kgToday"] = nullptr;
        if (perDay >= 0) o["kgPerDay"] = serialized(String(perDay, 1)); else o["kgPerDay"] = nullptr;
        if (perDay >= 0 && ppk > 0) o["costPerMonth"] = serialized(String(perDay * 30.44 * ppk, 0)); else o["costPerMonth"] = nullptr;
        if (pelCost.stockBags >= 0) o["stockBags"] = serialized(String(pelCost.stockBags, 1)); else o["stockBags"] = nullptr;
    }
    // last refill: detected when the low pellet alarm cleared, or entered by hand
    if (pellets.lastRefillT) { o["lastRefillAt"] = pellets.lastRefillT; o["lastRefillKg"] = pellets.lastRefillKg; }
    else { o["lastRefillAt"] = nullptr; o["lastRefillKg"] = nullptr; }
    if (!detail) return;
    {
        JsonObject u = o.createNestedObject("usage");
        double k = pellets.kgPerUnit(), ppk = pelCost.pricePerKg();
        u["pricePerBag"] = pelCost.pricePerBag > 0 ? serialized(String(pelCost.pricePerBag, 2)) : serialized(String("null"));
        u["bagKg"] = pelCost.bagKg;
        u["pricePerKg"] = ppk > 0 ? serialized(String(ppk, 3)) : serialized(String("null"));
        u["stockBags"] = pelCost.stockBags >= 0 ? serialized(String(pelCost.stockBags, 2)) : serialized(String("null"));
        u["days"] = pelUse.n;
        if (!isnan(pelLat) && !isnan(pelLon)) { JsonObject l = u.createNestedObject("location"); l["lat"] = serialized(String(pelLat, 2)); l["lon"] = serialized(String(pelLon, 2)); }
        else u["location"] = nullptr;
        u["unitsToday"] = serialized(String(pelUse.unitsToday(pellets.units), 0));
        static const int SPANS[] = { 7, 30, 365 };
        for (int span : SPANS) {
            int got = 0; double units = pelUse.unitsClosed(span, &got);
            JsonObject p = u.createNestedObject(span == 7 ? "last7" : span == 30 ? "last30" : "last365");
            p["days"] = got;
            if (k > 0 && got) {
                p["kg"] = serialized(String(k * units, 1));
                p["kgPerDay"] = serialized(String(k * units / got, 2));
                if (ppk > 0) p["cost"] = serialized(String(k * units * ppk, 2)); else p["cost"] = nullptr;
            } else { p["kg"] = nullptr; p["kgPerDay"] = nullptr; p["cost"] = nullptr; }
        }
        // how long the stock lasts at the use of the last 7 days (plus what is in the hopper)
        double perDay = pelKgPerDay7();
        if (pelCost.stockBags >= 0 && perDay > 0) {
            double rem = pellets.remainingKg();
            u["stockDays"] = serialized(String((pelCost.stockBags * pelCost.bagKg + (rem > 0 ? rem : 0)) / perDay, 0));
        } else u["stockDays"] = nullptr;
    }
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

// GET /api/pellets/days[?days=1..400]  -> use per day, oldest first, today last:
//   {"kgPerUnit":…, "pricePerKg":…|null, "days":[{"d":"2026-11-22","units":…,"kg":…|null,"cost":…|null}, …]}
// kg (and cost) with the calibration of now; null before the first calibration cycle. Built as a
// string (up to ~20 KB for 400 days) instead of a JSON document.
void handleRoutePelletDays()
{
    int want = 31;
    if (wm.server->hasArg("days") && !pc::parseIntRange(wm.server->arg("days").c_str(), 1, pc::PelletUsage::MAX_DAYS, want)) {
        wm.server->send(400, "application/json", "{\"result\":\"failed - days must be 1 - 400\"}"); return;
    }
    double k = pellets.kgPerUnit(), ppk = pelCost.pricePerKg();
    String out; out.reserve(80 + want * 64);
    out += "{\"kgPerUnit\":"; out += k > 0 ? String(k, 9) : String("null");
    out += ",\"pricePerKg\":"; out += ppk > 0 ? String(ppk, 3) : String("null");
    out += ",\"days\":[";
    auto add = [&](uint32_t d, double units, bool first) {
        char ds[11]; pc::dayText(d, ds);
        if (!first) out += ',';
        out += "{\"d\":\""; out += ds; out += "\",\"units\":"; out += String(units, 0);
        out += ",\"kg\":"; out += k > 0 ? String(k * units, 2) : String("null");
        out += ",\"cost\":"; out += k > 0 && ppk > 0 ? String(k * units * ppk, 2) : String("null");
        out += '}';
    };
    int n = min((int)pelUse.n, want - 1);
    bool first = true;
    for (int i = n - 1; i >= 0; i--) { const pc::PelletDay& d = pelUse.closed(i); add(d.day, d.units, first); first = false; }
    if (pelUse.curDay) add(pelUse.curDay, pelUse.unitsToday(pellets.units), first);   // today, so far
    out += "]}";
    wm.server->send(200, "application/json", out);
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
                if (!pellets.low) { pelCost.take(v); pelCostSave(); }   // while low: taken at the refill itself
                pellets.addKg(v, (uint32_t)time(nullptr)); pelletsDirty = true;
                LOGI("stove", "pellets: %.1f kg added by hand (%s) by %s", v,
                     pellets.low ? "used for the refill when the sensor clears" : "topped up", clientIp().c_str());
            }
        }
        if (!err.length() && wm.server->hasArg("pricePerBag")) {
            float v = wm.server->arg("pricePerBag").toFloat();
            if (v < 0 || v > 200) err = "pricePerBag must be 0 - 200 (EUR, 0 = not set)";
            else { pelCost.pricePerBag = v; pelCostSave(); LOGI("stove", "pellets: price %.2f per bag set by %s", v, clientIp().c_str()); }
        }
        if (!err.length() && wm.server->hasArg("bagKg")) {
            float v = wm.server->arg("bagKg").toFloat();
            if (v < 1 || v > 100) err = "bagKg must be 1 - 100";
            else { pelCost.bagKg = v; pelCostSave(); }
        }
        if (!err.length() && wm.server->hasArg("stockBags")) {
            String a = wm.server->arg("stockBags");
            float v = a == "-" || a == "off" ? -1 : a.toFloat();
            if (v > 10000 || (v < 0 && v != -1)) err = "stockBags must be 0 - 10000, or 'off'";
            else { pelCost.stockBags = v; pelCostSave(); LOGI("stove", "pellets: stock set to %.1f bag(s) by %s", v, clientIp().c_str()); }
        }
        if (!err.length() && (wm.server->hasArg("lat") || wm.server->hasArg("lon"))) {
            String a = wm.server->arg("lat"), b = wm.server->arg("lon");
            if (a == "off" || a == "-") { pelLat = pelLon = NAN; pelCostSave(); }
            else {
                float la = a.toFloat(), lo = b.toFloat();
                if (!a.length() || !b.length() || la < -90 || la > 90 || lo < -180 || lo > 180) err = "lat (-90 - 90) and lon (-180 - 180) together, or lat=off";
                else { pelLat = roundf(la * 100) / 100; pelLon = roundf(lo * 100) / 100; pelCostSave(); }
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
