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
    if (n == sizeof(t) && p.getBytes("s", &t, sizeof(t)) == sizeof(t) && t.version == pellets.version) {
        pellets = t;
        pellets.lastT = 0;   // no units for the time the controller was off
        LOGI("stove", "pellets: %d calibration cycle(s), %s", pellets.nCycles,
             pellets.cycleStart < 0 ? "waiting for the first low pellet alarm" : pellets.low ? "pellets low, waiting for the refill" : "counting since the last refill");
    } else if (n) {
        LOGW("stove", "pellets: stored data has another layout (%u bytes) - starting again", (unsigned)n);
    }
    p.end();
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
    if (now <= 1700000000 || stove.linkDown || stoveState == UNKNOWN || cachedPelletSensor < 0) return;
    int ev = pellets.update((uint32_t)now, cachedPelletSensor, cachedAugerSpeed);
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
    JsonArray arr = o.createNestedArray("history");
    for (int i = 0; i < pellets.nCycles; i++) {
        JsonObject c = arr.createNestedObject();
        c["t"] = pellets.cycles[i].t;
        c["kg"] = pellets.cycles[i].kg;
        c["units"] = (uint32_t)pellets.cycles[i].units;
        c["gramsPerUnit"] = serialized(String(pellets.cycles[i].kg / pellets.cycles[i].units * 1000.0, 4));
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
            else { pellets.refillKg = v; pelletsDirty = true; LOGI("stove", "pellets: refill amount set to %.1f kg by %s", v, wm.server->client().remoteIP().toString().c_str()); }
        }
        if (!err.length() && wm.server->hasArg("add")) {
            float v = wm.server->arg("add").toFloat();
            if (v <= 0 || v > 100) err = "add must be 0 - 100 kg";
            else {
                pellets.addKg(v, (uint32_t)time(nullptr)); pelletsDirty = true;
                LOGI("stove", "pellets: %.1f kg added by hand (%s) by %s", v,
                     pellets.low ? "used for the refill when the sensor clears" : "topped up", wm.server->client().remoteIP().toString().c_str());
            }
        }
        if (!err.length() && pc::parseOnOff(wm.server->arg("resetCalibration").c_str()) == 1) {
            pellets.resetCalibration(); pelletsDirty = true;
            LOGI("stove", "pellets: calibration reset by %s", wm.server->client().remoteIP().toString().c_str());
        }
        if (pelletsDirty) pelletsSave();
        if (err.length()) code = 400;
    } else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    DynamicJsonDocument doc(2048);
    JsonObject o = doc.to<JsonObject>();
    if (err.length()) o["result"] = "failed - " + err;
    else if (wm.server->method() == HTTP_POST) o["result"] = "success";
    pelletsJson(o, true);
    String out; serializeJson(doc, out);
    wm.server->send(code, "application/json", out);
}
