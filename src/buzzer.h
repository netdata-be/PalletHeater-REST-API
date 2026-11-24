#pragma once
// Buzzer of the OpenPelletBridge board: BZ1 (MLT-5020 magnetic transducer) through Q1 on
// BOARD_BUZZER_PIN (hardware/README.md). LEDC PWM: the frequency is the pitch (loudest near
// the ~4 kHz resonance), the duty the volume (50 % = loudest). Silent = duty 0 (pin low).
//
// A small task plays the patterns (pc::BeepPlayer, lib/pelletcore) and watches the stove for
// the events; nothing here blocks the loop. Events and their default pattern:
//   button     short press of the button                    click
//   success    a new firmware confirmed (otaVerify.h)       rising
//   reserve    pellet reserve alarm starts (cachedLowPallet) two_tone
//   countdown  the stove counts down to act on an E-error    tick, every 1 s / 0.5 s / 0.25 s
//              (RAM 0x30, see gError() in stoveCache.h)
//   alarm      error confirmed, alarm state, pellets used up alarm, until cleared or muted
// A higher priority interrupts a lower one (alarm > countdown > reserve > success > button).
// Muting (button press or stop while an error sounds) holds until the error is gone.
// Settings in NVS ("buzzer"), GET / POST /api/buzzer, MQTT (mqtt.h). Only on boards with
// BOARD_HAS_BUZZER; elsewhere the calls below are empty, so the hooks need no #if.
#include "board.h"

#if !BOARD_HAS_BUZZER

static inline void buzzerSetup() {}
static inline void buzzerButton() {}
static inline void buzzerRoutes() {}

#else

#include <Preferences.h>
#include <freertos/queue.h>

enum { BZ_BUTTON, BZ_SUCCESS, BZ_RESERVE, BZ_COUNTDOWN, BZ_ALARM, BZ_EVENTS };
static const char* const BZ_EVENT_NAMES[BZ_EVENTS] = { "button", "success", "reserve", "countdown", "alarm" };
static const char* const BZ_EVENT_DEFAULT[BZ_EVENTS] = { "click", "rising", "two_tone", "tick", "alarm" };
static const int BZ_PRIO_TEST = BZ_EVENTS + 1;   // event priority = its index + 1, a test above all
static const int BZ_LEDC_CH = 7, BZ_LEDC_BITS = 10;

struct BuzzerCfg {
    bool     enabled = true;
    uint8_t  volume = 100;              // %, 100 = 50 % duty
    uint8_t  evOn = (1 << BZ_EVENTS) - 1;
    int8_t   preset[BZ_EVENTS] = {};      // pc::beepPresets() index (set by buzzerLoad())
    bool     quiet = false;             // quiet hours on
    uint16_t quietFrom = 22 * 60, quietTo = 7 * 60;   // local time, minutes
    bool     quietAlarms = true;        // countdown and alarm still sound in the quiet hours
};
static BuzzerCfg buzzerCfg;
static portMUX_TYPE bzMux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t bzQueue = nullptr;
enum { BZQ_PLAY, BZQ_STOP, BZQ_EVENT, BZQ_DEMO };
struct BzReq { uint8_t kind; int8_t arg; };
static const char* volatile bzPlaying = nullptr;   // name of the preset that plays now
static volatile bool bzMuted = false;              // the error that sounds now was muted
static volatile bool bzErrorOn = false;            // countdown or alarm sounds (not muted)
static volatile bool buzzerSounding = false;       // tone on now (F1: USB current budget with the LEDs)
static String bzMqttPreset = "beep";               // MQTT select (buzzer_preset)

static const pc::BeepPreset* bzPreset(int i) { int n; const pc::BeepPreset* p = pc::beepPresets(n); return i >= 0 && i < n ? &p[i] : nullptr; }
static BuzzerCfg bzCfg() { portENTER_CRITICAL(&bzMux); BuzzerCfg c = buzzerCfg; portEXIT_CRITICAL(&bzMux); return c; }

static void buzzerLoad()
{
    Preferences p; p.begin("buzzer", true);
    BuzzerCfg c;
    c.enabled     = p.getBool("on", true);
    c.volume      = p.getUChar("vol", 100);
    c.evOn        = p.getUChar("ev", c.evOn);
    c.quiet       = p.getBool("qOn", false);
    c.quietFrom   = p.getUShort("qFrom", c.quietFrom);
    c.quietTo     = p.getUShort("qTo", c.quietTo);
    c.quietAlarms = p.getBool("qAlarm", true);
    for (int e = 0; e < BZ_EVENTS; e++) {   // by name: survives a change of the preset table
        char k[4] = { 'p', (char)('0' + e), 0 };
        int i = pc::beepPresetIndex(p.getString(k, BZ_EVENT_DEFAULT[e]).c_str());
        c.preset[e] = (int8_t)(i >= 0 ? i : pc::beepPresetIndex(BZ_EVENT_DEFAULT[e]));
    }
    p.end();
    portENTER_CRITICAL(&bzMux); buzzerCfg = c; portEXIT_CRITICAL(&bzMux);
}
static void buzzerSave(const BuzzerCfg& c)
{
    Preferences p; p.begin("buzzer", false);
    p.putBool("on", c.enabled); p.putUChar("vol", c.volume); p.putUChar("ev", c.evOn);
    p.putBool("qOn", c.quiet); p.putUShort("qFrom", c.quietFrom); p.putUShort("qTo", c.quietTo); p.putBool("qAlarm", c.quietAlarms);
    for (int e = 0; e < BZ_EVENTS; e++) { char k[4] = { 'p', (char)('0' + e), 0 }; p.putString(k, bzPreset(c.preset[e])->name); }
    p.end();
    portENTER_CRITICAL(&bzMux); buzzerCfg = c; portEXIT_CRITICAL(&bzMux);
}

static void bzSend(uint8_t kind, int arg = 0) { BzReq r = { kind, (int8_t)arg }; if (bzQueue) xQueueSend(bzQueue, &r, 0); }
static void buzzerPlay(int preset) { bzSend(BZQ_PLAY, preset); }   // a test: plays even when off / in quiet hours
static void buzzerStop() { bzSend(BZQ_STOP); }                     // also mutes an error that sounds
static void buzzerEvent(int ev) { bzSend(BZQ_EVENT, ev); }         // follows the settings
// Button press: mutes a sounding error, otherwise the button click
static void buzzerButton() { if (bzErrorOn) buzzerStop(); else buzzerEvent(BZ_BUTTON); }
// Buzzer on / off from the button (boardButton.h): saved, and confirmed with a rising /
// falling tone that plays either way. Returns the new state.
static bool buzzerToggle()
{
    BuzzerCfg c = bzCfg();
    c.enabled = !c.enabled;
    buzzerSave(c);
    buzzerPlay(pc::beepPresetIndex(c.enabled ? "rising" : "falling"));
    return c.enabled;
}

static bool bzQuietNow(const BuzzerCfg& c)
{
    if (!c.quiet) return false;
    time_t now = time(nullptr);
    if (now < 1700000000) return false;   // no valid time yet
    struct tm tm; localtime_r(&now, &tm);
    return pc::inQuietHours(tm.tm_hour * 60 + tm.tm_min, c.quietFrom, c.quietTo);
}
static bool bzAllowed(const BuzzerCfg& c, int ev, bool quiet)
{
    return c.enabled && (c.evOn & (1 << ev)) && (!quiet || (c.quietAlarms && ev >= BZ_COUNTDOWN));
}

static void buzzerTask(void*)
{
    pc::BeepPlayer pl;
    uint16_t outHz = 0; uint32_t outDuty = 0;
    bool trial = otaTrial;                 // a new firmware on trial: success once it is confirmed
    bool lowWas = false, quiet = false;
    uint32_t lastWatch = 0, lastQuiet = 0, errSince = 0, cdAt = 0, cdNext = 0, demoUntil = 0;
    int cdVal = -1;
    for (;;) {
        BzReq r;
        if (xQueueReceive(bzQueue, &r, pdMS_TO_TICKS(pl.active() ? 4 : 50)) == pdTRUE) {
            uint32_t now = millis();
            BuzzerCfg c = bzCfg();
            if (r.kind == BZQ_STOP) {
                if (bzErrorOn) { bzMuted = true; bzErrorOn = false; LOGI("sys", "buzzer: error muted until it clears"); }
                pl.stop(); demoUntil = 0;
            } else if (r.kind == BZQ_PLAY) {
                const pc::BeepPreset* p = bzPreset(r.arg);
                pl.play(p, BZ_PRIO_TEST, now, p && p->repeat == 0 ? 3 : -1);   // a looping preset: 3 passes
            } else if (r.kind == BZQ_DEMO) {
                demoUntil = now + 12000; cdNext = now;     // countdown test: 12 s
            } else if (r.kind == BZQ_EVENT && r.arg >= 0 && r.arg < BZ_EVENTS && bzAllowed(c, r.arg, quiet)) {
                pl.play(bzPreset(c.preset[r.arg]), r.arg + 1, now);
            }
        }
        uint32_t now = millis();
        if (now - lastWatch >= 100) {   // the events, from the cached stove values
            lastWatch = now;
            BuzzerCfg c = bzCfg();
            if (!lastQuiet || now - lastQuiet >= 10000) { lastQuiet = now; quiet = bzQuietNow(c); }
            if (trial && !otaTrial) { trial = false; if (bzAllowed(c, BZ_SUCCESS, quiet)) pl.play(bzPreset(c.preset[BZ_SUCCESS]), BZ_SUCCESS + 1, now); }
            bool data = lastFastStatesAt && stoveState != UNKNOWN;
            bool low = data && cachedLowPallet != 0;
            if (low && !lowWas && bzAllowed(c, BZ_RESERVE, quiet)) pl.play(bzPreset(c.preset[BZ_RESERVE]), BZ_RESERVE + 1, now);
            lowWas = low;
            // E-error countdown: the stove acts on an error after ~55 s (RAM 0x30, read every 5 s).
            // Not for an error shorter than one reading (E12 blips at brazier cleaning) nor in the
            // start-up check (state 1: E1 for ~45 s, a pre-warning).
            int err = data ? cachedCurrentError : 0;
            if (err > 0) { if (!errSince) errSince = now; } else errSince = 0;
            if (cachedErrorCountdown != cdVal) { cdVal = cachedErrorCountdown; cdAt = now; }
            int left = err > 0 ? pc::countdownLeft(cdVal, now - cdAt, 5000) : -1;
            bool countdown = err > 0 && now - errSince >= 5000 && left > 0 && !pc::alarmState(stoveState) && stoveState != 1;
            bool alarm = data && (pc::alarmState(stoveState) || (confirmedCurrentError > 0 && !countdown) || pelletsEmptyOff());
            if (!countdown && !alarm) bzMuted = false;
            bzErrorOn = !bzMuted && ((alarm && bzAllowed(c, BZ_ALARM, quiet)) || (countdown && bzAllowed(c, BZ_COUNTDOWN, quiet)));
            if (demoUntil && (int32_t)(now - demoUntil) >= 0) demoUntil = 0;
            if (demoUntil) left = (int)((demoUntil - now + 999) / 1000);
            if (alarm && !bzMuted && bzAllowed(c, BZ_ALARM, quiet)) {
                if (!(pl.active() && pl.prio >= BZ_ALARM + 1)) pl.play(bzPreset(c.preset[BZ_ALARM]), BZ_ALARM + 1, now, 0);
            } else if (pl.prio == BZ_ALARM + 1) pl.stop();
            if (demoUntil || (countdown && !alarm && !bzMuted && bzAllowed(c, BZ_COUNTDOWN, quiet))) {
                if ((int32_t)(now - cdNext) >= 0) {
                    pl.play(bzPreset(c.preset[BZ_COUNTDOWN]), demoUntil ? BZ_PRIO_TEST : BZ_COUNTDOWN + 1, now, 1);
                    cdNext = now + pc::countdownBeepMs(left);
                }
            } else cdNext = now;
        }
        uint16_t hz = pl.tick(now);
        uint32_t duty = hz ? pc::beepDuty(bzCfg().volume, BZ_LEDC_BITS) : 0;
        if (hz != outHz || duty != outDuty) {
            if (hz && hz != outHz) ledcChangeFrequency(BZ_LEDC_CH, hz, BZ_LEDC_BITS);
            ledcWrite(BZ_LEDC_CH, duty);
            outHz = hz; outDuty = duty;
        }
        buzzerSounding = duty != 0;
        bzPlaying = pl.active() ? pl.p->name : nullptr;
    }
}

static void buzzerSetup()
{
    buzzerLoad();
    ledcSetup(BZ_LEDC_CH, 4000, BZ_LEDC_BITS);
    ledcWrite(BZ_LEDC_CH, 0);
    ledcAttachPin(BOARD_BUZZER_PIN, BZ_LEDC_CH);   // takes the pin over from boardPinsSetup(), still low
    bzQueue = xQueueCreate(6, sizeof(BzReq));
    // 3 KB stack: localtime_r and the player; above the loop task (1) for an even rhythm
    xTaskCreatePinnedToCore(buzzerTask, "Buzzer", 3072, NULL, 2, NULL, 1);
    BuzzerCfg c = bzCfg();
    LOGI("sys", "buzzer %s, volume %u %%", c.enabled ? "on" : "off", c.volume);
}

// MQTT commands (mqtt.h): buzzer ON / OFF, buzzer_preset <name>, buzzer_play [PRESS | <name>],
// buzzer_stop. Returns an error text, "" = done. Plays only while the buzzer is on.
static String buzzerCommand(const String& name, const char* payload)
{
    BuzzerCfg c = bzCfg();
    if (name == "buzzer") {
        int on = pc::parseOnOff(payload);
        if (on < 0) return "invalid payload";
        c.enabled = on == 1; buzzerSave(c);
        LOGI("sys", "buzzer %s (MQTT)", on ? "on" : "off");
    } else if (name == "buzzer_preset") {
        if (pc::beepPresetIndex(payload) < 0) return "unknown preset";
        bzMqttPreset = payload;
    } else if (name == "buzzer_play") {
        int i = pc::beepPresetIndex(!*payload || strcmp(payload, "PRESS") == 0 ? bzMqttPreset.c_str() : payload);
        if (i < 0) return "unknown preset";
        if (!c.enabled) return "the buzzer is off";
        buzzerPlay(i);
    } else if (name == "buzzer_stop") buzzerStop();
    else return "unknown command";
    return "";
}

// GET  /api/buzzer -> settings, presets, what plays now
// POST /api/buzzer?enabled=&volume=0-100&quiet=&quietFrom=HH:MM&quietTo=HH:MM&quietAlarms=
//                  &<event>=true|false&<event>Preset=<preset>   (event: button, success, ...)
//      actions:    play=<preset> | test=<event> (its preset; countdown: 12 s demo) | stop=1
static void handleRouteBuzzer()
{
    int code = 200;
    DynamicJsonDocument json(1536);
    if (wm.server->method() == HTTP_POST) {
        auto has = [](const char* k) { return wm.server->hasArg(k); };
        auto arg = [](const char* k) { String v = wm.server->arg(k); v.trim(); return v; };
        BuzzerCfg c = bzCfg();
        String err;
        bool changed = false;
        auto boolArg = [&](const char* k, bool& dst) {
            if (!has(k)) return;
            int b = pc::parseOnOff(arg(k).c_str());
            if (b < 0) err = String(k) + " must be true / false"; else { dst = b == 1; changed = true; }
        };
        boolArg("enabled", c.enabled);
        boolArg("quiet", c.quiet);
        boolArg("quietAlarms", c.quietAlarms);
        if (has("volume")) { int v; if (!pc::parseIntRange(arg("volume").c_str(), 0, 100, v)) err = "volume must be 0 - 100"; else { c.volume = v; changed = true; } }
        if (has("quietFrom")) { int m = pc::clockMinutes(arg("quietFrom").c_str()); if (m < 0) err = "quietFrom must be HH:MM"; else { c.quietFrom = m; changed = true; } }
        if (has("quietTo"))   { int m = pc::clockMinutes(arg("quietTo").c_str());   if (m < 0) err = "quietTo must be HH:MM";   else { c.quietTo = m; changed = true; } }
        for (int e = 0; e < BZ_EVENTS; e++) {
            bool on = c.evOn & (1 << e);
            boolArg(BZ_EVENT_NAMES[e], on);
            c.evOn = on ? c.evOn | (1 << e) : c.evOn & ~(1 << e);
            String k = String(BZ_EVENT_NAMES[e]) + "Preset";
            if (has(k.c_str())) { int i = pc::beepPresetIndex(arg(k.c_str()).c_str()); if (i < 0) err = "unknown preset for " + k; else { c.preset[e] = i; changed = true; } }
        }
        int play = -1, test = -1;
        if (has("play") && (play = pc::beepPresetIndex(arg("play").c_str())) < 0) err = "unknown preset to play";
        if (has("test")) { for (int e = 0; e < BZ_EVENTS; e++) if (arg("test") == BZ_EVENT_NAMES[e]) test = e; if (test < 0) err = "unknown event to test"; }
        if (err.length()) { json["result"] = "failed - " + err; code = 400; }
        else {
            if (changed) { buzzerSave(c); LOGI("sys", "buzzer settings changed by %s", clientIp().c_str()); }
            if (has("stop")) buzzerStop();
            if (play >= 0) buzzerPlay(play);
            if (test == BZ_COUNTDOWN) bzSend(BZQ_DEMO);
            else if (test >= 0) buzzerPlay(c.preset[test]);
            json["result"] = "success";
        }
    } else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    BuzzerCfg c = bzCfg();
    char hm[6];
    json["enabled"] = c.enabled;
    json["volume"] = c.volume;
    JsonObject q = json.createNestedObject("quietHours");
    q["enabled"] = c.quiet;
    snprintf(hm, sizeof(hm), "%02u:%02u", c.quietFrom / 60, c.quietFrom % 60); q["from"] = String(hm);
    snprintf(hm, sizeof(hm), "%02u:%02u", c.quietTo / 60, c.quietTo % 60);     q["to"] = String(hm);
    q["alarms"] = c.quietAlarms;
    q["now"] = bzQuietNow(c);
    JsonObject ev = json.createNestedObject("events");
    for (int e = 0; e < BZ_EVENTS; e++) {
        JsonObject o = ev.createNestedObject(BZ_EVENT_NAMES[e]);
        o["enabled"] = (c.evOn & (1 << e)) != 0;
        o["preset"] = bzPreset(c.preset[e])->name;
    }
    JsonArray pr = json.createNestedArray("presets");
    int n; const pc::BeepPreset* p = pc::beepPresets(n);
    for (int i = 0; i < n; i++) pr.add(p[i].name);
    const char* now = bzPlaying;
    if (now) json["playing"] = now; else json["playing"] = nullptr;
    json["muted"] = (bool)bzMuted;
    String out; serializeJson(json, out);
    wm.server->send(code, "application/json", out);
}
static void buzzerRoutes() { wm.server->on("/api/buzzer", authed(handleRouteBuzzer)); }

#endif
