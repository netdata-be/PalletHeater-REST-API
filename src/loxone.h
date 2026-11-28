#pragma once
// Loxone: the stove values in a form a Loxone Miniserver reads reliably, two ways.
//
// 1. GET /api/loxone: one plain text line with only numbers, for a Virtual HTTP Input
//      opb seq=1834 state=7 power=1 burn=0 link=1 ... kg=9.5 hAvg=28.5 resMin=-1
//    Command recognition in Loxone: "errId=\v", "kg=\v", ... Rules that keep it reliable:
//    - numbers only: true / false are 1 / 0, unknown is -1
//    - no key ends with another key ("errId=" never matches inside another key)
//    - no timestamps, time left instead (alarmMin = minutes until the pellet alarm)
//    - seq goes up at every change of an event value and at every heartbeat (interval),
//      so Loxone can tell when the controller stops reporting
// 2. Miniserver web service (push): GET http://<host>:<port>/jdev/sps/io/<input>/<value>
//    with Basic auth sets virtual inputs and a switch directly:
//    - map: key -> name or UUID of a virtual input (also the text keys stateText / errText /
//      warnText for a Virtual Text Input)
//    - switch: name or UUID of a switch that follows "powered on" (On / Off, the state stable
//      for 5 s). A power command from Loxone (POST /api/power with header X-Source: loxone)
//      is not pushed back (no echo); refused or failed (409 pellets used up, 500) -> the
//      real state is pushed at once
//    - event keys at once, slow values (temperatures, kg, hours) at most every 60 s and only
//      after a real difference (0.5 °C, 0.1 kg, ...); everything again every 5 min
//    - every answer is checked (HTTP 200 and the code in the LL JSON of the Miniserver)
//    The Miniserver's structure file (/data/LoxAPP3.json) lists its controls with name, type,
//    room and UUID: POST /api/loxone/controls reads it, the web app searches it for targets.
//
// Settings in NVS ("loxone"): GET / POST /api/loxone/config. The password is never returned.
// All network traffic runs in its own task: a Miniserver that does not answer never blocks
// the loop (web server, MQTT, button). Basic auth over plain HTTP: the password can be read
// on the network, so use a Loxone user with only the rights it needs.
#include <HTTPClient.h>
#include <Preferences.h>

enum : uint8_t { LXK_EVENT, LXK_SLOW, LXK_TEXT };
struct LoxKey { const char* key; uint8_t kind; uint8_t dec; float delta; uint16_t minS; };

enum LoxIdx {
    LX_SEQ, LX_STATE, LX_POWER, LX_BURN, LX_LINK, LX_AGE,
    LX_ERR, LX_PEND, LX_PENDS, LX_MEMERR, LX_WARN, LX_WREMOTE,
    LX_ROOM, LX_SETP, LX_FUME, LX_TSTOVE, LX_TREMOTE,
    LX_FLAME, LX_FLAMEACT, LX_FAN, LX_FANRUN, LX_ESON, LX_ESSTOP, LX_ESSTART, LX_AUGER,
    LX_KG, LX_HNOW, LX_HAVG, LX_ALARMMIN, LX_KGTODAY, LX_KGDAY, LX_EURMONTH, LX_BAGS,
    LX_LOWPEL, LX_RESMIN, LX_RESOFF, LX_OFFMIN, LX_BRZMIN, LX_SVCH, LX_RUNH, LX_STARTS, LX_RSSI,
    LX_NUM,                                   // numbers above: in the line
    LX_STATETEXT = LX_NUM, LX_ERRTEXT, LX_WARNTEXT, LX_WARNSHORT,
    LX_TOTAL
};

// Same order as LoxIdx. kind, decimals, minimum difference and interval for slow values.
static const LoxKey LOX_KEYS[LX_TOTAL] = {
    { "seq",      LXK_EVENT, 0, 0, 0 },     // + 1 at every change of an event value and every heartbeat
    { "state",    LXK_EVENT, 0, 0, 0 },     // stove state 0 - 10, -1 no answer
    { "power",    LXK_EVENT, 0, 0, 0 },     // powered on (1 - 5, 7; an alarm counts as off)
    { "burn",     LXK_EVENT, 0, 0, 0 },     // fire: ignition, working, brazier cleaning (3 - 5)
    { "link",     LXK_EVENT, 0, 0, 0 },     // serial link to the stove up
    { "age",      LXK_SLOW,  0, 5, 60 },    // seconds since the last full read of the stove
    { "errId",    LXK_EVENT, 0, 0, 0 },     // confirmed error: id, 0 none, -1 LINK, -2 PELLETS
    { "pendId",   LXK_EVENT, 0, 0, 0 },     // error the stove reports now (before it counts), 0 none
    { "pendSec",  LXK_SLOW,  0, 1, 5 },     // seconds until the stove acts on it, -1 none
    { "memErr",   LXK_EVENT, 0, 0, 0 },     // entries in the error memory (0 - 5)
    { "warn",     LXK_EVENT, 0, 0, 0 },     // any warning
    { "wRemote",  LXK_EVENT, 0, 0, 0 },     // warning: the remote control's sensor is not used
    { "room",     LXK_SLOW,  1, 0.5, 60 },  // room temperature the stove regulates on
    { "setp",     LXK_EVENT, 0, 0, 0 },     // set point
    { "fume",     LXK_SLOW,  0, 2, 60 },    // fume temperature
    { "tStove",   LXK_SLOW,  1, 0.5, 60 },  // sensor of the stove
    { "tRemote",  LXK_SLOW,  1, 0.5, 60 },  // sensor of the remote control
    { "flame",    LXK_EVENT, 0, 0, 0 },     // flame power setting 1 - 4
    { "flameAct", LXK_EVENT, 0, 0, 0 },     // flame power the stove runs at now
    { "fan",      LXK_EVENT, 0, 0, 0 },     // room fan setting 0 - 3
    { "fanRun",   LXK_EVENT, 0, 0, 0 },     // room fan turning
    { "esOn",     LXK_EVENT, 0, 0, 0 },     // energy saving on
    { "esStop",   LXK_EVENT, 0, 0, 0 },     // stop at set point + esStop
    { "esStart",  LXK_EVENT, 0, 0, 0 },     // restart at set point - esStart
    { "auger",    LXK_SLOW,  0, 1, 60 },    // auger, % of the time
    { "kg",       LXK_SLOW,  1, 0.1, 60 },  // pellets until the low pellet alarm
    { "hNow",     LXK_SLOW,  1, 0.5, 60 },  // hours until the alarm at the current rate
    { "hAvg",     LXK_SLOW,  1, 0.5, 60 },  // hours until the alarm at the 7 day average
    { "alarmMin", LXK_SLOW,  0, 10, 60 },   // minutes until the expected alarm (average)
    { "kgToday",  LXK_SLOW,  1, 0.1, 60 },  // pellets burnt today
    { "kgDay",    LXK_SLOW,  1, 0.1, 60 },  // pellets per day (7 days)
    { "eurMonth", LXK_SLOW,  0, 1, 60 },    // cost per month (EUR)
    { "bags",     LXK_EVENT, 1, 0, 0 },     // stock in bags
    { "lowPel",   LXK_EVENT, 0, 0, 0 },     // low pellet alarm of the stove
    { "resMin",   LXK_EVENT, 0, 0, 0 },     // pellet reserve: minutes until the reserve alarm
    { "resOff",   LXK_EVENT, 0, 0, 0 },     // the stove switched itself off: pellets used up
    { "offMin",   LXK_EVENT, 0, 0, 0 },     // pellet reserve phase 2: minutes until the stove switches itself off
    { "brzMin",   LXK_SLOW,  0, 1, 60 },    // minutes until the next brazier cleaning
    { "svcH",     LXK_EVENT, 0, 0, 0 },     // hours since the last service
    { "runH",     LXK_EVENT, 0, 0, 0 },     // working hours
    { "starts",   LXK_EVENT, 0, 0, 0 },     // number of starts
    { "rssi",     LXK_SLOW,  0, 3, 300 },   // WiFi signal (dBm)
    { "stateText", LXK_TEXT, 0, 0, 0 },     // push only (Virtual Text Input)
    { "errText",  LXK_TEXT,  0, 0, 0 },
    { "warnText", LXK_TEXT,  0, 0, 0 },
    { "warnShort", LXK_TEXT, 0, 0, 0 },     // very short, for a tile: "Remote not used", "OK"
};
static_assert(sizeof(LOX_KEYS) / sizeof(LOX_KEYS[0]) == LX_TOTAL, "LOX_KEYS out of step with LoxIdx");

static int loxKeyIndex(const char* k)
{
    for (int i = 0; i < LX_TOTAL; i++) if (strcmp(LOX_KEYS[i].key, k) == 0) return i;
    return -1;
}

struct LoxSnap {
    float  v[LX_NUM];
    String txt[LX_TOTAL - LX_NUM];
    int    powerStable = -1;   // powered on, stable for 5 s (-1 unknown): the switch
    uint32_t seq = 0;
    bool   valid = false;
};

struct LoxoneConfig {
    String   host;                 // Miniserver (IP or name)
    uint16_t port = 80;            // web service (HTTP)
    String   user, pass;
    uint16_t interval = 60;        // heartbeat (s): seq goes up at least this often
    bool     push = false;         // web service pushes
    String   sw;                   // switch (name or UUID), "" = none
    uint8_t  lang = 0;             // texts (stateText, errText, warnText): 0 = as the remote control (NLD -> Dutch), 1 Dutch, 2 English
    String   target[LX_TOTAL];     // virtual input per key, "" = not pushed
};

static LoxoneConfig      loxCfg;
static uint32_t          loxCfgVer = 1;
static LoxSnap           loxSnap;               // built in the loop, read by the task (loxMux)
static SemaphoreHandle_t loxMux = nullptr;
static TaskHandle_t      loxTaskHandle = nullptr;
static volatile uint32_t loxEchoAt = 0;         // millis() of a command from Loxone (X-Source)
static volatile int      loxPowerCmdVal = -1;   // power command from Loxone: -1 none, 0 / 1 accepted, 2 refused
// status (written by the task)
static uint32_t loxPushes = 0, loxPushFail = 0;
static uint32_t loxLastOkMs = 0, loxLastErrMs = 0;
static String   loxLastError;                    // under loxMux
// jobs for the task: test and reading the controls of the Miniserver
static volatile uint8_t loxJob = 0;              // 1 test, 2 controls
static volatile bool    loxJobBusy = false;
static String   loxTestResult;                   // JSON, under loxMux
static String   loxControls;                     // error of the last read (JSON), under loxMux; "" = see the file

struct LoxLock {
    LoxLock()  { if (loxMux) xSemaphoreTake(loxMux, portMAX_DELAY); }
    ~LoxLock() { if (loxMux) xSemaphoreGive(loxMux); }
};

// ── settings ────────────────────────────────────────────────────────────────
static String loxMapJson(const LoxoneConfig& c)
{
    DynamicJsonDocument d(3072);
    for (int i = 0; i < LX_TOTAL; i++) if (c.target[i].length()) d[LOX_KEYS[i].key] = c.target[i];
    String s; serializeJson(d, s);
    return s;
}

// "{"kg":"Kachel_PelletsKg",...}" -> targets. Returns an error text, "" = ok.
static String loxParseMap(const String& json, LoxoneConfig& c)
{
    DynamicJsonDocument d(4096);
    if (deserializeJson(d, json) || !d.is<JsonObject>()) return "map must be a JSON object {\"key\":\"input\"}";
    String t[LX_TOTAL];
    for (JsonPair p : d.as<JsonObject>()) {
        int i = loxKeyIndex(p.key().c_str());
        if (i < 0 || i >= LX_TOTAL) return String("map: unknown key ") + p.key().c_str();
        String v = p.value().as<const char*>() ? p.value().as<const char*>() : "";
        v.trim();
        if (v.length() > 80) return String("map: name too long for ") + p.key().c_str();
        t[i] = v;
    }
    for (int i = 0; i < LX_TOTAL; i++) c.target[i] = t[i];
    return "";
}

static void loxLoadConfig()
{
    Preferences p;
    p.begin("loxone", true);
    loxCfg.host     = p.getString("host", "");
    loxCfg.port     = p.getUShort("port", 80);
    loxCfg.user     = p.getString("user", "");
    loxCfg.pass     = p.getString("pass", "");
    loxCfg.interval = p.getUShort("interval", 60);
    loxCfg.push     = p.getBool("push", false);
    loxCfg.sw       = p.getString("switch", "");
    loxCfg.lang     = p.getUChar("lang", 0);
    String map      = p.getString("map", "");
    p.end();
    if (map.length()) loxParseMap(map, loxCfg);
}

static void loxSaveConfig()
{
    Preferences p;
    p.begin("loxone", false);
    p.putString("host", loxCfg.host);
    p.putUShort("port", loxCfg.port);
    p.putString("user", loxCfg.user);
    p.putString("pass", loxCfg.pass);
    p.putUShort("interval", loxCfg.interval);
    p.putBool("push", loxCfg.push);
    p.putString("switch", loxCfg.sw);
    p.putUChar("lang", loxCfg.lang);
    p.putString("map", loxMapJson(loxCfg));
    p.end();
}

// ── the values ──────────────────────────────────────────────────────────────
static String loxFmt(int i, float v)
{
    char b[16];
    if (LOX_KEYS[i].dec == 0 || v == -1) snprintf(b, sizeof(b), "%ld", lroundf(v));
    else snprintf(b, sizeof(b), "%.1f", v);
    return String(b);
}

// Texts in Dutch? The language of the remote control (EEPROM 0x4F, 4 = NLD), as the web app
// does, unless the setting chooses one
int planStoveLanguage();   // stoveCache.h
static bool loxDutch() { return loxCfg.lang == 1 || (loxCfg.lang == 0 && planStoveLanguage() == 4); }

static String loxErrorText(bool nl)
{
    if (stoveState == UNKNOWN) return nl ? "LINK Geen antwoord van de kachel (seriële verbinding weg)" : "LINK No answer from the stove (serial link down)";
    if (confirmedCurrentError) return getErrorCode(confirmedCurrentError) + " " + (nl ? pc::errorDescNl(confirmedCurrentError) : pc::errorDesc(confirmedCurrentError));
    if (lastSlowStatesAt && pelletsEmptyOff())
        return nl ? "PELLETS Pellets op: de kachel schakelde zichzelf uit op het einde van de pelletreserve - vul de voorraadbak bij"
                  : String("PELLETS ") + PELLETS_EMPTY_DESC;
    return nl ? "geen" : "none";
}

// All values from the cached stove data (loop task: the same globals as /api/state)
static void loxBuild(LoxSnap& s)
{
    const bool fastOk = lastFastStatesAt != 0, slowOk = lastSlowStatesAt != 0, known = stoveState != UNKNOWN;
    float* v = s.v;
    for (int i = 0; i < LX_NUM; i++) v[i] = -1;
    v[LX_STATE] = stoveState;
    if (known) v[LX_POWER] = StoveIsOn ? 1 : 0;
    if (known) v[LX_BURN] = stoveState >= 3 && stoveState <= 5 ? 1 : 0;
    v[LX_LINK] = !stove.linkDown && fastOk ? 1 : 0;
    if (fastOk) v[LX_AGE] = (millis() - lastFastStatesAt) / 1000;
    v[LX_ERR] = !known ? -1 : confirmedCurrentError ? confirmedCurrentError : (slowOk && pelletsEmptyOff()) ? -2 : 0;
    v[LX_PEND] = known && cachedCurrentError > 0 ? cachedCurrentError : 0;
    if (v[LX_PEND] > 0 && cachedErrorCountdown >= 0) v[LX_PENDS] = cachedErrorCountdown;
    if (slowOk) { int n = 0; for (int i = 0; i < 5; i++) if (cachedErrors[i]) n++; v[LX_MEMERR] = n; }
    const bool nl = loxDutch();
    String rs = remoteSensorWarningText(nl);
    String wt, ws;   // the warnings in full and very short (a tile in the app)
    // pellets running low (the stove's level sensor, the pellet reserve): first, it needs action
    if (slowOk && !pelletsEmptyOff() && !pc::reserveRefilling(cachedPelletSensor) && (cachedLowPallet || pc::reserveSensorLow(cachedPelletSensor))) {
        char b[140];
        if (cachedPelletShutdown >= 0) {
            int m = (cachedPelletShutdown + 1) / 2;
            snprintf(b, sizeof(b), nl ? "Pellets op: de kachel schakelt over %d min uit (pelletreserve) - vul de voorraadbak bij"
                                      : "Pellets used up: the stove switches itself off in %d min (pellet reserve) - refill the hopper", m);
            wt = b;
            snprintf(b, sizeof(b), nl ? "Pellets: uit over %d min" : "Pellets: off in %d min", m); ws = b;
        } else {
            if (cachedPelletCounter > 0) snprintf(b, sizeof(b), nl ? "Pellets bijna op: pelletreserve-alarm over %d min - vul de voorraadbak bij"
                                                                   : "Pellets running low: pellet reserve alarm in %d min - refill the hopper", cachedPelletCounter);
            else snprintf(b, sizeof(b), "%s", nl ? "Pellets bijna op - vul de voorraadbak bij" : "Pellets running low - refill the hopper");
            wt = b; ws = nl ? "Pellets laag" : "Pellets low";
        }
    }
    if (rs.length()) { if (ws.length()) ws += " / "; ws += nl ? "Afstandsbed. niet gebruikt" : "Remote not used"; }
    {
        StaticJsonDocument<512> wd;
        JsonArray w = wd.to<JsonArray>();
        boardWarnings(w);   // e.g. chip temperature (boardInfo.h)
        if (rs.length()) { if (wt.length()) wt += " / "; wt += rs; }
        for (JsonObject o : w) {
            if (wt.length()) wt += " / ";
            if (ws.length()) ws += " / ";
            ws += o["id"] == "CHIP_TEMP_HIGH" ? (nl ? "Controller te warm" : "Controller too hot") : o["id"].as<const char*>();
#if BOARD_HAS_CHIP_TEMP
            if (nl && o["id"] == "CHIP_TEMP_HIGH") {
                char d[120]; snprintf(d, sizeof(d), "Chip van de controller op %.0f °C (grens ESP32-S3: 85 °C): controleer de verluchting van de behuizing", chipTempC);
                wt += d; continue;
            }
#endif
            wt += o["desc"].as<const char*>();
        }
    }
    v[LX_WARN] = wt.length() ? 1 : 0;
    v[LX_WREMOTE] = rs.length() ? 1 : 0;
    if (slowOk) {
        v[LX_ROOM] = ambTemp; v[LX_TSTOVE] = ambTempStove; v[LX_TREMOTE] = ambTempRemoteControl; v[LX_SETP] = setPointTemp;
        v[LX_ESON] = energySavingStop != 0; v[LX_ESSTOP] = energySavingStop; v[LX_ESSTART] = -energySavingStart;
        v[LX_AUGER] = pc::augerPercent(cachedAugerSpeed);
        v[LX_LOWPEL] = cachedLowPallet != 0;
        v[LX_RESOFF] = pelletsEmptyOff() ? 1 : 0;
        if (pc::reserveSensorLow(cachedPelletSensor) && cachedPelletCounter >= 0) v[LX_RESMIN] = cachedPelletCounter;
        // phase 2 of the pellet reserve (RAM 0x91, half minutes), as pelletReserve.shutdownMinutes in /api/state
        if (cachedPelletShutdown >= 0 && !pelletsEmptyOff() && !pc::reserveRefilling(cachedPelletSensor)) v[LX_OFFMIN] = (cachedPelletShutdown + 1) / 2;
        v[LX_SVCH] = cachedHoursSinceLastService; v[LX_RUNH] = cachedTotalRuntime; v[LX_STARTS] = cachedNumberOfStarts;
    }
    if (fastOk) {
        v[LX_FUME] = fumesTemp; v[LX_FLAME] = flamePower; v[LX_FAN] = roomFanSpeed;
        if (cachedFlameActual >= 0) v[LX_FLAMEACT] = cachedFlameActual;
        v[LX_FANRUN] = roomFanIsRunning();
    }
    if (stoveState == 4 && cachedBrazierCountdown >= 0) v[LX_BRZMIN] = cachedBrazierCountdown;
    {
        double rem = pellets.remainingKg(), h = pellets.hoursAt(cachedAugerSpeed);
        int avgHours = 0; double ha = pellets.hoursAt(pelletsAvgAuger(&avgHours));
        if (rem >= 0) v[LX_KG] = rem;
        if (h >= 0) v[LX_HNOW] = h;
        if (ha >= 0 && avgHours >= 6) { v[LX_HAVG] = ha; v[LX_ALARMMIN] = lround(ha * 60); }
        double today = pelKgToday(), perDay = pelKgPerDay7(), month = pelCostPerMonth(), stock = pelStockBags();
        if (today >= 0) v[LX_KGTODAY] = today;
        if (perDay >= 0) v[LX_KGDAY] = perDay;
        if (month >= 0) v[LX_EURMONTH] = month;
        if (stock >= 0) v[LX_BAGS] = stock;
    }
    if (WiFi.status() == WL_CONNECTED) v[LX_RSSI] = WiFi.RSSI();
    s.txt[LX_STATETEXT - LX_NUM] = nl ? String(pc::stoveStateNameNl(stoveState)) : String(StoveStateStr);
    s.txt[LX_ERRTEXT - LX_NUM]   = loxErrorText(nl);
    s.txt[LX_WARNTEXT - LX_NUM]  = wt.length() ? wt : String(nl ? "geen" : "none");
    s.txt[LX_WARNSHORT - LX_NUM] = ws.length() ? ws : String("OK");
    // digital values: unknown is 0, not -1 (a digital virtual input in Loxone sees anything but 0
    // as on: "Vuur: aan" while the stove does not answer); link = 0 tells it is unknown
    for (int i : { LX_POWER, LX_BURN, LX_LINK, LX_WARN, LX_WREMOTE, LX_FANRUN, LX_ESON, LX_LOWPEL, LX_RESOFF })   // offMin stays -1
        if (v[i] < 0) v[i] = 0;
    v[LX_SEQ] = s.seq;
    s.valid = true;
}

static String loxLine(const LoxSnap& s)
{
    String out = "opb";
    out.reserve(420);
    for (int i = 0; i < LX_NUM; i++) { out += ' '; out += LOX_KEYS[i].key; out += '='; out += loxFmt(i, s.v[i]); }
    return out;
}

static String loxValueText(const LoxSnap& s, int i) { return i < LX_NUM ? loxFmt(i, s.v[i]) : s.txt[i - LX_NUM]; }

// Every second in the loop: new values, seq + 1 at a change of an event value or the heartbeat
void loxoneLoop()
{
    static unsigned long lastBuild = 0, lastBeat = 0;
    static float prevEvent[LX_NUM];
    static bool first = true;
    static int powerCand = -1; static unsigned long powerSince = 0;
    unsigned long now = millis();
    if (lastBuild && now - lastBuild < 1000) return;
    lastBuild = now;
    LoxSnap s;
    { LoxLock l; s.seq = loxSnap.seq; }
    loxBuild(s);
    bool changed = first;
    for (int i = 1; i < LX_NUM; i++) {
        if (LOX_KEYS[i].kind != LXK_EVENT) continue;
        if (prevEvent[i] != s.v[i]) changed = true;
        prevEvent[i] = s.v[i];
    }
    first = false;
    if (changed || now - lastBeat >= (unsigned long)loxCfg.interval * 1000UL) { s.seq++; lastBeat = now; s.v[LX_SEQ] = s.seq; }
    // the switch: powered on, unchanged for 5 s (no push for a state the stove passes through)
    int pw = s.v[LX_POWER] < 0 ? -1 : (int)s.v[LX_POWER];
    if (pw != powerCand) { powerCand = pw; powerSince = now; }
    s.powerStable = pw >= 0 && now - powerSince >= 5000 ? pw : -1;
    LoxLock l;
    loxSnap = s;
}

// ── network (task) ──────────────────────────────────────────────────────────
static String loxUrlEncode(const String& s)
{
    String o; o.reserve(s.length() + 8);
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') o += c;
        else { char b[4]; snprintf(b, sizeof(b), "%%%02X", (uint8_t)c); o += b; }
    }
    return o;
}

static bool loxFailing = false;   // task only: no connection / login refused; logs the first error and the recovery
static void loxSetError(const String& e, bool down = true)
{
    static uint32_t lastLog = 0;
    static String lastRefused; static uint32_t lastRefusedLog = 0;
    if (down) {
        if (!loxFailing || millis() - lastLog > 600000UL) { logPrintf(LOG_WARN, "loxone", "%s", e.c_str()); lastLog = millis(); }
        loxFailing = true;
    } else {   // an input the Miniserver does not accept: once per input every 10 min
        String who = e.substring(0, e.indexOf(" = "));
        if (who != lastRefused || millis() - lastRefusedLog > 600000UL) { logPrintf(LOG_WARN, "loxone", "%s", e.c_str()); lastRefused = who; lastRefusedLog = millis(); }
    }
    LoxLock l;
    loxLastError = e; loxLastErrMs = millis();
}
static void loxSetOk()
{
    if (loxFailing) LOGI("loxone", "Miniserver reachable again");
    loxFailing = false;
}

// GET on the Miniserver with Basic auth. Returns the HTTP status (< 0: no connection) and the
// code in its JSON answer ({"LL":{"control":..,"value":..,"Code":"200"}}), -1 when there is none.
static int loxGet(const LoxoneConfig& c, const String& path, int& llCode, String* value, String& err)
{
    WiFiClient net;
    HTTPClient http;
    llCode = -1;
    http.setConnectTimeout(2000);
    http.setTimeout(3000);
    http.setReuse(false);
    if (!http.begin(net, c.host, c.port, path)) { err = "invalid address " + c.host; return -1; }
    if (c.user.length()) http.setAuthorization(c.user.c_str(), c.pass.c_str());
    static const char* hdrs[] = { "Retry-After", "Server" };
    http.collectHeaders(hdrs, 2);
    uint32_t t0 = millis();
    int code = http.GET();
    uint32_t ms = millis() - t0;
    if (code <= 0) { err = String("Miniserver ") + c.host + ":" + c.port + " not reachable (" + http.errorToString(code) + ", after " + ms + " ms)"; http.end(); return code; }
    String body = http.getSize() > 4096 ? String("") : http.getString();   // the LL answer is small
    String retryAfter = http.header("Retry-After");
    http.end();
    if (code != 200) {
        // the Miniserver explains an error in its HTML answer: <errorcode>..</errorcode> <errordetail>..</errordetail>
        String detail;
        int a = body.indexOf("<errordetail>"), b = body.indexOf("</errordetail>");
        if (a >= 0 && b > a) detail = body.substring(a + 13, b);
        else { detail = body; detail.replace("\n", " "); detail.replace("\r", ""); if (detail.length() > 120) detail = detail.substring(0, 120) + "..."; }
        detail.trim();
        LOGD("loxone", "HTTP %d after %lu ms for %s: %s", code, (unsigned long)ms, path.c_str(), body.substring(0, 300).c_str());
        err = String("Miniserver answered HTTP ") + code + (detail.length() ? ": " + detail : String("")) + (retryAfter.length() ? " (Retry-After " + retryAfter + " s)" : String(""))
              + " after " + ms + " ms";
    }
    StaticJsonDocument<128> f; f["LL"]["Code"] = true; f["LL"]["code"] = true; f["LL"]["value"] = true;
    StaticJsonDocument<512> d;
    if (!deserializeJson(d, body, DeserializationOption::Filter(f))) {
        JsonVariant cv = d["LL"]["Code"]; if (cv.isNull()) cv = d["LL"]["code"];
        if (!cv.isNull()) llCode = cv.is<const char*>() ? atoi(cv.as<const char*>()) : cv.as<int>();
        if (value && !d["LL"]["value"].isNull()) *value = d["LL"]["value"].as<String>();
    }
    if (code == 401) err = "Miniserver refused: wrong user name or password (401)";
    else if (code == 403) err = "Miniserver refused: the user has no rights for this (403) - " + err;
    else if (llCode >= 0 && llCode != 200) err = String("Miniserver answered code ") + llCode + (llCode == 404 || llCode == 500 ? " (input not found? check the name / UUID)" : "");
    return code;
}

// one value to a virtual input: LXP_OK, LXP_REFUSED (the Miniserver answered, but not this
// input: only this one is retried later) or LXP_DOWN (no connection / login refused: stop the round)
enum { LXP_OK, LXP_REFUSED, LXP_DOWN };
static int loxPushOne(const LoxoneConfig& c, const String& target, const String& value, String& err)
{
    int ll; err = "";
    String path = "/jdev/sps/io/" + loxUrlEncode(target) + "/" + loxUrlEncode(value);
    int code = loxGet(c, path, ll, nullptr, err);
    if (code == 200 && (ll < 0 || ll == 200)) { loxPushes++; loxLastOkMs = millis(); loxSetOk(); LOGD("loxone", "push %s = %s", target.c_str(), value.c_str()); return LXP_OK; }
    loxPushFail++; if (!err.length()) err = "no answer";
    bool down = code <= 0 || code == 401 || code == 403;
    loxSetError("push " + target + " = " + value + ": " + err, down);
    return down ? LXP_DOWN : LXP_REFUSED;
}

// what was pushed last, per key (task only)
struct LoxSent { String text; float val = 0; uint32_t at = 0, refusedAt = 0; bool done = false; };

static void loxJobTest(const LoxoneConfig& c, const LoxSnap& s, LoxSent* sent);
static void loxJobControls(const LoxoneConfig& c);

static void loxTask(void*)
{
    static LoxSent sent[LX_TOTAL];
    LoxoneConfig c; uint32_t ver = 0;
    LoxSnap s;
    uint32_t lastFull = 0, backoffUntil = 0, graceUntil = 0, lastPush = 0;
    int switchSent = -1;   // -1 not pushed yet
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));
        if (ver != loxCfgVer) {   // new settings: all again
            LoxLock l; c = loxCfg; ver = loxCfgVer;
            for (auto& x : sent) { x.done = false; x.refusedAt = 0; }
            switchSent = -1;
        }
        { LoxLock l; s = loxSnap; }
        if (loxJob && !loxJobBusy) {
            uint8_t j = loxJob; loxJobBusy = true;
            if (j == 1) loxJobTest(c, s, sent); else loxJobControls(c);
            loxJob = 0; loxJobBusy = false;
        }
        if (!s.valid || otaActive || WiFi.status() != WL_CONNECTED || !c.host.length()) continue;
        uint32_t now = millis();
        if (!c.push || (backoffUntil && (int32_t)(now - backoffUntil) < 0) || now - lastPush < 1000) continue;
        lastPush = now;
        bool full = !lastFull || now - lastFull >= 300000UL;
        if (full) lastFull = now;
        // a power command from Loxone: accepted -> already known there, refused -> the real state at once
        int pcmd = loxPowerCmdVal;
        if (pcmd >= 0) {
            loxPowerCmdVal = -1;
            if (pcmd == 2) { switchSent = -2; graceUntil = now; }
            else { switchSent = pcmd; graceUntil = now + 20000; }
        }
        bool echo = loxEchoAt && now - loxEchoAt < 10000;
        String err;
        bool failed = false;
        static uint32_t switchRefusedAt = 0;
        if (c.sw.length() && s.powerStable >= 0 && (int32_t)(now - graceUntil) >= 0 && (s.powerStable != switchSent || full)
            && (!switchRefusedAt || now - switchRefusedAt >= 60000)) {
            int r = loxPushOne(c, c.sw, s.powerStable ? "On" : "Off", err);
            if (r == LXP_OK) { switchSent = s.powerStable; switchRefusedAt = 0; }
            else if (r == LXP_REFUSED) switchRefusedAt = now | 1;
            else failed = true;
        }
        for (int i = 0; i < LX_TOTAL && !failed; i++) {
            if (!c.target[i].length()) continue;
            const LoxKey& k = LOX_KEYS[i];
            String txt = loxValueText(s, i);
            LoxSent& x = sent[i];
            bool send;
            if (!x.done || full) send = true;
            else if (txt == x.text) send = false;
            else if (k.kind == LXK_SLOW) send = now - x.at >= k.minS * 1000UL && (fabsf(s.v[i] - x.val) >= k.delta || s.v[i] == -1 || x.val == -1);
            else send = true;
            if (!send || (x.refusedAt && now - x.refusedAt < 60000)) continue;   // a refused input: again after 60 s
            // set from Loxone a moment ago (fan, flame, set point, energy saving): it knows already
            if (x.done && echo && (i == LX_FAN || i == LX_FLAME || i == LX_SETP || i == LX_ESON || i == LX_ESSTOP || i == LX_ESSTART)) {
                x.text = txt; x.val = i < LX_NUM ? s.v[i] : 0; x.at = now; continue;
            }
            int r = loxPushOne(c, c.target[i], txt, err);
            if (r == LXP_OK) { x.text = txt; x.val = i < LX_NUM ? s.v[i] : 0; x.at = now; x.done = true; x.refusedAt = 0; }
            else if (r == LXP_REFUSED) x.refusedAt = now | 1;
            else failed = true;
        }
        if (failed) { backoffUntil = now + 15000; lastFull = full ? 0 : lastFull; }   // retry in 15 s; a full round again
        else backoffUntil = 0;
    }
}

// Test: credentials (structure file version), then every mapped value once.
// The switch is never pushed by the test: in Loxone it would send a power command.
static void loxJobTest(const LoxoneConfig& c, const LoxSnap& s, LoxSent* sent)
{
    DynamicJsonDocument d(4096);
    d["at"] = (uint32_t)time(nullptr);
    if (!c.host.length()) { d["ok"] = false; d["error"] = "no Miniserver address set"; }
    else {
        bool allOk = true;
        JsonObject w = d.createNestedObject("webService");
        int ll; String err, val;
        int code = loxGet(c, "/jdev/sps/LoxAPPversion3", ll, &val, err);
        w["http"] = code;
        w["ok"] = code == 200;
        if (val.length()) w["structureVersion"] = val;
        if (err.length()) { w["error"] = err; allOk = false; }
        if (code == 200 && s.valid) {
            JsonArray a = w.createNestedArray("pushes");
            for (int i = 0; i < LX_TOTAL; i++) {
                if (!c.target[i].length()) continue;
                String txt = loxValueText(s, i);
                bool ok = loxPushOne(c, c.target[i], txt, err) == LXP_OK;
                JsonObject o = a.createNestedObject();
                o["key"] = LOX_KEYS[i].key; o["target"] = c.target[i]; o["value"] = txt; o["ok"] = ok;
                if (!ok) { o["error"] = err; allOk = false; }
                else { sent[i].text = txt; sent[i].val = i < LX_NUM ? s.v[i] : 0; sent[i].at = millis(); sent[i].done = true; }
            }
            if (c.sw.length()) w["switch"] = "not pushed by the test (Loxone would send a power command)";
        }
        d["ok"] = allOk;
    }
    String out; serializeJson(d, out);
    LoxLock l; loxTestResult = out;
    LOGI("loxone", "test: %s", d["ok"].as<bool>() ? "ok" : "failed");
}

// The controls of the Miniserver from its structure file (/data/LoxAPP3.json, often hundreds of
// KB): read token by token, one control at a time (filtered: name, type, UUID, room), and
// written to SPIFFS as they come, one line each: RAM stays small, whatever the size of the
// installation. uuidAction is the UUID /jdev/sps/io takes. The web app searches the list
// (GET /api/loxone/controls?q=): the whole list is too much for a weak WiFi link.
//   /loxctl.txt    first line "#<count>\t<T = not all>\t<lastModified>", then "name\ttype\tuuid\troom";
//                  room is the name, or "@" + the first 8 characters of its UUID when the rooms
//                  came after the controls. At most the free space minus 32 KB (history, events)
//   /loxrooms.json {roomUuid: {"name": ..}} (rooms can come after the controls in the file)
static const char* LOX_CTL_FILE = "/loxctl.txt";
static const char* LOX_ROOMS_FILE = "/loxrooms.json";
struct LoxReader {
    WiFiClient* c = nullptr;
    int get()   // one character, -1 at the end / after 8 s without data
    {
        uint32_t t0 = millis();
        for (;;) {
            int ch = c->read();
            if (ch >= 0) return ch;
            if (!c->connected() && !c->available()) return -1;
            if (millis() - t0 > 8000) return -1;
            vTaskDelay(1);
        }
    }
    int skipWs() { int ch; do ch = get(); while (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t'); return ch; }
    String str()   // after the opening quote (keys and UUIDs: no escapes that matter)
    {
        String o; int ch;
        while ((ch = get()) >= 0 && ch != '"') {
            if (ch == '\\') { ch = get(); if (ch < 0) break; }
            if (o.length() < 96) o += (char)ch;
        }
        return o;
    }
};
static void loxField(String& line, const char* v)   // no tab / new line inside a field
{
    for (const char* p = v ? v : ""; *p; p++) line += (*p == '\t' || *p == '\n' || *p == '\r') ? ' ' : *p;
}

static void loxJobControls(const LoxoneConfig& c)
{
    String err, lastModified;
    unsigned n = 0;
    bool truncated = false;
    if (!spiffsMounted) err = "no file system on the controller";
    else if (!c.host.length()) err = "no Miniserver address set";
    else {
        WiFiClient net; HTTPClient http;
        http.setConnectTimeout(3000); http.setTimeout(8000);
        http.useHTTP10(true);   // no chunked transfer: the stream is parsed as it comes in
        if (!http.begin(net, c.host, c.port, "/data/LoxAPP3.json")) err = "invalid address";
        else {
            if (c.user.length()) http.setAuthorization(c.user.c_str(), c.pass.c_str());
            int code = http.GET();
            if (code != 200) err = code == 401 ? String("wrong user name or password (401)") : code <= 0 ? String("Miniserver not reachable") : String("HTTP ") + code;
            else {
                SPIFFS.remove(LOX_CTL_FILE); SPIFFS.remove(LOX_ROOMS_FILE);
                File f = SPIFFS.open(LOX_CTL_FILE, FILE_WRITE);
                if (!f) err = "file system full";
                else {
                    LoxReader r; r.c = http.getStreamPtr();
                    StaticJsonDocument<64> skip; skip.set(false);          // filter: read past, keep nothing
                    StaticJsonDocument<128> fCtl;
                    fCtl["name"] = true; fCtl["type"] = true; fCtl["uuidAction"] = true; fCtl["room"] = true;
                    StaticJsonDocument<64> fRooms; fRooms["*"]["name"] = true;
                    DynamicJsonDocument rooms(4096);
                    char hdr[56];
                    snprintf(hdr, sizeof(hdr), "#%-7u\t \t%-31s\n", 0u, "");   // fixed width: rewritten in place at the end
                    f.print(hdr);
                    // room for the history and the event log: never more than the free space - 32 KB
                    long budget = (long)SPIFFS.totalBytes() - (long)SPIFFS.usedBytes() - 32768;
                    size_t written = 0;
                    if (r.skipWs() != '{') err = "structure file not readable (no JSON object)";
                    while (!err.length() && !truncated) {
                        int ch = r.skipWs();
                        if (ch == ',') continue;
                        if (ch == '}' || ch < 0) break;
                        if (ch != '"') { err = "structure file not readable"; break; }
                        String key = r.str();
                        if (r.skipWs() != ':') { err = "structure file not readable"; break; }
                        if (key == "controls") {
                            if (r.skipWs() != '{') { err = "structure file: controls is not an object"; break; }
                            for (;;) {
                                if (truncated) break;
                                ch = r.skipWs();
                                if (ch == ',') continue;
                                if (ch != '"') break;               // '}' = end of the controls
                                String id = r.str();
                                if (r.skipWs() != ':') { err = "structure file not readable"; break; }
                                StaticJsonDocument<768> d;
                                DeserializationError e = deserializeJson(d, *r.c, DeserializationOption::Filter(fCtl), DeserializationOption::NestingLimit(40));
                                if (e) { err = String("structure file: ") + e.c_str(); break; }
                                const char* room = d["room"];
                                const char* rn = room ? rooms[room]["name"].as<const char*>() : nullptr;
                                String line;
                                loxField(line, d["name"]); line += '\t'; loxField(line, d["type"]); line += '\t';
                                loxField(line, d["uuidAction"] | id.c_str()); line += '\t';
                                if (rn) loxField(line, rn); else if (room) { line += '@'; line += String(room).substring(0, 8); }
                                line += '\n';
                                if ((long)(written + line.length()) > budget) { truncated = true; break; }   // not all: no space
                                if (f.print(line) != line.length()) { truncated = true; break; }
                                written += line.length();
                                if ((++n & 31) == 0) vTaskDelay(1);
                            }
                        } else {
                            DeserializationError e;
                            if (key == "rooms") e = deserializeJson(rooms, *r.c, DeserializationOption::Filter(fRooms), DeserializationOption::NestingLimit(40));
                            else if (key == "lastModified") { StaticJsonDocument<96> d; e = deserializeJson(d, *r.c); lastModified = d.as<String>(); }
                            else { StaticJsonDocument<16> d; e = deserializeJson(d, *r.c, DeserializationOption::Filter(skip), DeserializationOption::NestingLimit(40)); }
                            if (e == DeserializationError::NoMemory && key == "rooms") e = DeserializationError::Ok;   // not all rooms: their UUIDs then
                            if (e) { err = String("structure file (") + key + "): " + e.c_str(); break; }
                        }
                    }
                    f.close();
                    if (!err.length() && !n) err = "no controls in the structure file";
                    if (!err.length()) {
                        File rf = SPIFFS.open(LOX_ROOMS_FILE, FILE_WRITE);
                        if (rf) { serializeJson(rooms, rf); rf.close(); }
                        // the count in the first line: rewrite the file's head in place ("r+")
                        File h = SPIFFS.open(LOX_CTL_FILE, "r+");
                        if (h) { snprintf(hdr, sizeof(hdr), "#%-7u\t%c\t%-31.31s\n", n, truncated ? 'T' : ' ', lastModified.c_str()); h.seek(0); h.print(hdr); h.close(); }
                    }
                }
            }
            http.end();
        }
    }
    StaticJsonDocument<256> d;
    if (err.length()) {
        if (spiffsMounted) { SPIFFS.remove(LOX_CTL_FILE); SPIFFS.remove(LOX_ROOMS_FILE); }
        LOGW("loxone", "structure file: %s", err.c_str());
        d["ok"] = false; d["error"] = err;
    } else {
        if (truncated) LOGW("loxone", "structure file read: %u controls, not all (no more space on the controller)", n);
        else LOGI("loxone", "structure file read: %u controls", n);
        d["ok"] = true; d["count"] = n; d["truncated"] = truncated; d["lastModified"] = lastModified;
    }
    LoxLock l; loxControls = ""; serializeJson(d, loxControls);
}

// ── hooks from the routes ───────────────────────────────────────────────────
static bool loxFromLoxone() { return wm.server->header("X-Source").equalsIgnoreCase("loxone"); }

// POST /api/power (routesControl.h): a command from Loxone is not pushed back
void loxonePowerCmd(bool on, int httpCode)
{
    if (!loxFromLoxone()) return;
    loxPowerCmdVal = httpCode >= 300 ? 2 : (on ? 1 : 0);
}
// fan, flame, set point, energy saving set from Loxone: not pushed back
static std::function<void()> loxEcho(std::function<void()> fn)
{
    return [fn]() { fn(); if (loxFromLoxone()) loxEchoAt = millis() | 1; };
}

void loxoneSetup()
{
    loxLoadConfig();
    if (spiffsMounted && SPIFFS.exists("/loxctl.json")) SPIFFS.remove("/loxctl.json");   // left by a test build (27/11/2026)
    loxMux = xSemaphoreCreateMutex();
    // 8 KB: HTTPClient + the filtered structure file parser
    xTaskCreatePinnedToCore(loxTask, "loxone", 8192, nullptr, 1, &loxTaskHandle, 1);
    if (loxCfg.push) LOGI("loxone", "pushes to the Miniserver %s", loxCfg.host.c_str());
}

// ── routes ──────────────────────────────────────────────────────────────────
// GET /api/loxone -> the line (text/plain)
void handleRouteLoxone()
{
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    LoxSnap s;
    { LoxLock l; s = loxSnap; }
    if (!s.valid) loxBuild(s);
    wm.server->send(200, "text/plain; charset=utf-8", loxLine(s));
}

// GET  /api/loxone/config -> settings (without the password), status, keys with their values
// POST /api/loxone/config?host=&port=&user=&password=&interval=&push=&switch=&lang=&map=
//      lang: texts (stateText, errText, warnText) auto (as the remote control) / nl / en
//      only the given fields change; password "" keeps the stored one, "-" clears it;
//      map = JSON object {"key":"virtual input"}, replaces the whole map
void handleRouteLoxoneConfig()
{
    int code = 200;
    String result;
    if (wm.server->method() == HTTP_POST) {
        auto has = [](const char* k) { return wm.server->hasArg(k); };
        auto arg = [](const char* k) { String v = wm.server->arg(k); v.trim(); return v; };
        LoxoneConfig c;
        { LoxLock l; c = loxCfg; }
        String err;
        auto boolArg = [&](const char* k, bool& dst) { int b = pc::parseOnOff(arg(k).c_str()); if (b < 0) err = String(k) + " must be true / false"; else dst = b == 1; };
        auto portArg = [&](const char* k, uint16_t& dst, int lo, int hi) { int p = 0; if (!pc::parseIntRange(arg(k).c_str(), lo, hi, p)) err = String(k) + " must be " + lo + " - " + hi; else dst = p; };
        if (has("host")) c.host = arg("host");
        if (has("port")) portArg("port", c.port, 1, 65535);
        if (has("user")) c.user = arg("user");
        if (has("password")) { String p = wm.server->arg("password"); if (p == "-") c.pass = ""; else if (p.length()) c.pass = p; }
        if (has("interval")) portArg("interval", c.interval, 10, 3600);
        if (has("push")) boolArg("push", c.push);
        if (has("switch")) c.sw = arg("switch");
        if (has("lang")) { String l = arg("lang"); l.toLowerCase();
                           if (l == "auto") c.lang = 0; else if (l == "nl") c.lang = 1; else if (l == "en") c.lang = 2; else err = "lang must be auto / nl / en"; }
        if (has("map") && !err.length()) err = loxParseMap(wm.server->arg("map"), c);
        if (!err.length() && c.push && !c.host.length()) err = "host is required for the pushes";
        if (err.length()) { result = "failed - " + err; code = 400; }
        else {
            bool hostChanged = c.host != loxCfg.host;
            { LoxLock l; loxCfg = c; loxCfgVer++; }
            loxSaveConfig();
            if (hostChanged && spiffsMounted) { SPIFFS.remove(LOX_CTL_FILE); SPIFFS.remove(LOX_ROOMS_FILE); LoxLock l; loxControls = ""; }   // the list of another Miniserver
            result = "success";
            LOGI("loxone", "settings changed by %s: Miniserver %s, push %s", clientIp().c_str(), c.host.c_str(), c.push ? "on" : "off");
        }
    } else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    DynamicJsonDocument json(9216);
    if (result.length()) json["result"] = result;
    LoxoneConfig c; LoxSnap s; String lastErr;
    { LoxLock l; c = loxCfg; s = loxSnap; lastErr = loxLastError; }
    json["host"] = c.host;
    json["port"] = c.port;
    json["user"] = c.user;
    json["passwordSet"] = c.pass.length() > 0;
    json["interval"] = c.interval;
    json["push"] = c.push;
    json["switch"] = c.sw;
    json["lang"] = c.lang == 1 ? "nl" : c.lang == 2 ? "en" : "auto";
    json["langActive"] = loxDutch() ? "nl" : "en";   // the language of the texts now
    JsonObject m = json.createNestedObject("map");
    for (int i = 0; i < LX_TOTAL; i++) if (c.target[i].length()) m[LOX_KEYS[i].key] = c.target[i];
    JsonObject st = json.createNestedObject("status");
    st["seq"] = s.seq;
    st["pushes"] = loxPushes; st["pushFailed"] = loxPushFail;
    uint32_t now = millis();
    if (loxLastOkMs) st["lastOkSecondsAgo"] = (now - loxLastOkMs) / 1000; else st["lastOkSecondsAgo"] = nullptr;
    if (lastErr.length()) { st["lastError"] = lastErr; st["lastErrorSecondsAgo"] = (now - loxLastErrMs) / 1000; }
    else { st["lastError"] = nullptr; st["lastErrorSecondsAgo"] = nullptr; }
    if (s.powerStable >= 0) st["switchState"] = s.powerStable ? "On" : "Off"; else st["switchState"] = nullptr;
    JsonArray keys = json.createNestedArray("keys");   // all keys, kind and current value
    for (int i = 0; i < LX_TOTAL; i++) {
        JsonObject k = keys.createNestedObject();
        k["key"] = LOX_KEYS[i].key;
        k["kind"] = LOX_KEYS[i].kind == LXK_EVENT ? "event" : LOX_KEYS[i].kind == LXK_SLOW ? "slow" : "text";
        if (s.valid) k["value"] = loxValueText(s, i); else k["value"] = nullptr;
    }
    if (json.overflowed()) LOGE("http", "loxone config: JSON document too small");
    sendJson(code, json);
}

// POST /api/loxone/test -> starts a test (202); GET -> the last result ({"running":true} meanwhile)
void handleRouteLoxoneTest()
{
    if (wm.server->method() == HTTP_POST) {
        if (loxJob || loxJobBusy) { wm.server->send(409, "application/json; charset=utf-8", "{\"result\":\"failed - a test or a read of the Miniserver is running\"}"); return; }
        { LoxLock l; loxTestResult = ""; }
        loxJob = 1;
        wm.server->send(202, "application/json; charset=utf-8", "{\"result\":\"started\"}");
        return;
    }
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    String r;
    { LoxLock l; r = loxTestResult; }
    if (loxJob == 1 || (loxJobBusy && !r.length())) { wm.server->send(200, "application/json; charset=utf-8", "{\"running\":true}"); return; }
    wm.server->send(200, "application/json; charset=utf-8", r.length() ? r : String("{\"running\":false}"));
}

// POST /api/loxone/controls -> reads the structure file of the Miniserver (202)
// GET  /api/loxone/controls[?q=<text>][&ids=<uuid or name>,...]
//      -> {"ok":true,"count":603,"lastModified":..,"matches":[[name, type, uuid, room], ...]}
//      q: part of the name, room, type or UUID (at most 40 matches); ids: exact UUIDs / names
//      ({"running":true} while it reads, {"ok":false,"error":..} after a failed read)
void handleRouteLoxoneControls()
{
    if (wm.server->method() == HTTP_POST) {
        if (loxJob || loxJobBusy) { wm.server->send(409, "application/json; charset=utf-8", "{\"result\":\"failed - a test or a read of the Miniserver is running\"}"); return; }
        { LoxLock l; loxControls = ""; }
        loxJob = 2;
        wm.server->send(202, "application/json; charset=utf-8", "{\"result\":\"started\"}");
        return;
    }
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    if (loxJob == 2 || loxJobBusy) { wm.server->send(200, "application/json; charset=utf-8", "{\"running\":true}"); return; }
    String r;
    { LoxLock l; r = loxControls; }
    if (r.indexOf("\"ok\":false") >= 0) { wm.server->send(200, "application/json; charset=utf-8", r); return; }
    File f = spiffsMounted ? SPIFFS.open(LOX_CTL_FILE, FILE_READ) : File();
    if (!f) { wm.server->send(200, "application/json; charset=utf-8", "{\"ok\":false,\"count\":0}"); return; }
    String q = wm.server->arg("q"); q.trim(); q.toLowerCase();
    String ids = wm.server->arg("ids");
    DynamicJsonDocument rooms(4096);
    { File rf = SPIFFS.open(LOX_ROOMS_FILE, FILE_READ); if (rf) { deserializeJson(rooms, rf); rf.close(); } }
    DynamicJsonDocument out(8192);
    out["ok"] = true;
    String head = f.readStringUntil('\n');            // "#count\tlastModified"
    out["count"] = head.substring(1).toInt();
    {
        int t1 = head.indexOf('\t'), t2 = t1 >= 0 ? head.indexOf('\t', t1 + 1) : -1;
        out["truncated"] = t1 >= 0 && head.charAt(t1 + 1) == 'T';
        String lm = t2 >= 0 ? head.substring(t2 + 1) : String(""); lm.trim();
        if (lm.length()) out["lastModified"] = lm; else out["lastModified"] = nullptr;
    }
    JsonArray a = out.createNestedArray("matches");
    std::vector<String> want;
    for (int i = 0, j; ids.length() && i <= (int)ids.length(); i = j + 1) {
        j = ids.indexOf(',', i); if (j < 0) j = ids.length();
        String w = ids.substring(i, j); w.trim(); if (w.length()) want.push_back(w);
        if (want.size() >= 50) break;
    }
    int found = 0;
    while (f.available() && found < 40 && (q.length() || want.size())) {
        String line = f.readStringUntil('\n');
        int t1 = line.indexOf('\t'), t2 = line.indexOf('\t', t1 + 1), t3 = line.indexOf('\t', t2 + 1);
        if (t1 < 0 || t2 < 0 || t3 < 0) continue;
        String name = line.substring(0, t1), type = line.substring(t1 + 1, t2), uuid = line.substring(t2 + 1, t3), room = line.substring(t3 + 1);
        if (room.startsWith("@")) {   // the rooms came after the controls: first 8 characters of the UUID
            String pre = room.substring(1);
            for (JsonPair p : rooms.as<JsonObject>()) if (strncmp(p.key().c_str(), pre.c_str(), pre.length()) == 0) { room = p.value()["name"] | ""; break; }
        }
        bool hit = false;
        for (auto& w : want) if (w == uuid || w == name) { hit = true; break; }
        if (!hit && q.length()) { String all = name + "\t" + room + "\t" + type + "\t" + uuid; all.toLowerCase(); hit = all.indexOf(q) >= 0; }
        if (!hit) continue;
        JsonArray row = a.createNestedArray();
        row.add(name); row.add(type); row.add(uuid); row.add(room);
        found++;
        if ((found & 7) == 0) esp_task_wdt_reset();
    }
    f.close();
    out["more"] = found >= 40;
    sendJson(200, out);
}
