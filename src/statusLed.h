#pragma once
// Status LEDs, the same calls on every board:
//   onboardLED  system state: on = WiFi connected, flashing = (re)connecting / full reset
//   led         toggled by a short press of the button
// DevKit: two plain LEDs (EasyLed). Boards without them (BOARD_HAS_ONBOARD_LED 0) get a
// no-op stand-in with the same API; the WS2812 driver (BOARD_HAS_WS2812) replaces it there
// (LED1 system = onboardLED, LED2 stove), statusLedLoop() is its hook in loop(),
// statusLedSetup() (WS2812 only) the one in setup().
#include "board.h"

#if BOARD_HAS_ONBOARD_LED
#include "EasyLed.h"
EasyLed led(BOARD_STATUS_LED_PIN, EasyLed::ActiveLevel::High);
EasyLed onboardLED(BOARD_ONBOARD_LED_PIN, EasyLed::ActiveLevel::High);
static inline void statusLedLoop() {}
#elif BOARD_HAS_WS2812
// Two WS2812B (ws2812.h): LED1 system, LED2 stove. An esp_timer draws them every 25 ms from
// the states below, so they keep going while setup() or a firmware update blocks the loop.
// The states come from the loop (statusLedApi.h: ledsStateTick), the settings from NVS "leds"
// (GET/POST /api/leds). led / onboardLED both act on LED1:
//   onboardLED.on()     WiFi connected        onboardLED.flash(n, on, off)  n amber flashes
//   led.toggle()        short white blink (button), also acknowledges the error state
#include <esp_timer.h>
#include <Preferences.h>
#include "ws2812.h"

extern volatile bool otaActive;   // constants.h
bool fwInstallBusy();             // fwUpdate.h

enum LedState : uint8_t {
    LS_OFF,
    // LED1, system
    LS_BOOT, LS_CONNECTING, LS_PORTAL, LS_CONNECTED, LS_UPDATE, LS_ERROR,
    // LED2, stove
    LS_STANDBY, LS_STARTING, LS_WORKING, LS_CLEANING, LS_EXTINGUISHING, LS_ALARM, LS_LINK_DOWN,
    // overlays
    LS_WARNING, LS_BUTTON, LS_FLASH,
    LS_COUNT
};
// Patterns: 16 steps of 125 ms (2 s), bit 15 first, 1 = lit
static const uint16_t LP_SOLID = 0xFFFF, LP_SLOW = 0xFF00, LP_FAST = 0xAAAA, LP_DOUBLE = 0xA000, LP_BLIP = 0x8000;
struct LedLook { const char* name; uint8_t r, g, b; uint16_t pattern; };
// The one table of colours and patterns (docs/api.md, GET /api/leds). Calm while all is well:
// solid or off; blinking means something is in progress or needs attention.
static const LedLook LED_LOOK[LS_COUNT] = {
    {"off",            0,   0,   0,   0},
    {"booting",        0,   0,   255, LP_SOLID},    // blue: start-up, until WiFi is known
    {"connecting",     0,   0,   255, LP_SLOW},     // blue, 1 s on / 1 s off: WiFi (re)connecting
    {"portal",         160, 0,   255, LP_DOUBLE},   // purple double flash: config portal (access point)
    {"connected",      0,   255, 0,   LP_SOLID},    // green: WiFi connected
    {"update",         0,   200, 255, LP_FAST},     // cyan, 4 Hz: firmware update running
    {"error",          255, 0,   0,   LP_DOUBLE},   // red double flash: crash, brown-out or rollback at start (button clears)
    {"standby",        0,   255, 0,   LP_BLIP},     // green blip every 2 s: stove in standby
    {"starting",       255, 70,  0,   LP_SLOW},     // orange, 1 s on / off: starting, pellet loading, ignition
    {"working",        255, 70,  0,   LP_SOLID},    // orange: working
    {"cleaning",       0,   80,  255, LP_SOLID},    // blue: brazier cleaning
    {"extinguishing",  0,   80,  255, LP_SLOW},     // blue, 1 s on / off: final cleaning
    {"alarm",          255, 0,   0,   LP_FAST},     // red, 4 Hz: alarm, stove error, pellets used up
    {"linkDown",       255, 0,   128, LP_SLOW},     // magenta, 1 s on / off: the stove does not answer
    {"warning",        255, 200, 0,   LP_DOUBLE},   // yellow double flash in the first 0.5 s of every 2 s, over the stove state
    {"button",         255, 255, 255, LP_SOLID},    // white 150 ms: button pressed
    {"flash",          255, 120, 0,   LP_SOLID},    // amber: onboardLED.flash() (reconnect, full reset)
};
// Brightness cap in the driver: 100 % in the settings = 40 % PWM. USB fuse F1 (0.5 A) must
// hold WiFi + buzzer + both LEDs (full white ~36 mA each at 100 %, hardware/TODO.md).
static const uint8_t LED_MAX_PWM = 102;

struct LedSettings { bool enabled = true; uint8_t brightness = 25; bool night = false; uint8_t nightFrom = 22, nightTo = 7; };
static LedSettings ledCfg;
static volatile uint8_t ledSys = LS_BOOT;        // LS_BOOT / LS_CONNECTING / LS_PORTAL / LS_CONNECTED (loop)
static volatile uint8_t ledStove = LS_OFF;       // LED2 state (loop)
static volatile bool ledStoveWarn = false;       // warning overlay on LED2 (loop)
static volatile bool ledError = false;           // LED1 error until the button is pressed (loop)
static volatile bool ledBootUpdate = false;      // install from GitHub after a restart (fwBootInstall)
static volatile bool ledBleUpdate = false;       // update over Bluetooth (loop)
static volatile bool ledNight = false;           // night hours: LEDs off (loop)
static volatile uint32_t ledButtonUntil = 0, ledFlashStart = 0, ledFlashUntil = 0;
// The button held (boardButton.h), shown even with the LEDs off or at night:
//   ledHold 1 = held (LED1 white), 2 = full reset counting down to ledResetAt (both amber,
//   blinking faster towards the end); ledConfirmUntil: LED1 white for a toggle by the button
static volatile uint8_t ledHold = 0;
static volatile uint32_t ledResetAt = 0, ledConfirmUntil = 0;
static volatile uint16_t ledFlashOn = 0, ledFlashOff = 0;
static esp_timer_handle_t ledTimer = nullptr;

static inline bool ledBefore(uint32_t now, uint32_t until) { return (int32_t)(until - now) > 0; }
// LED1 by priority: update, config portal, error, WiFi
static uint8_t ledSysState()
{
    if (otaActive || ledBootUpdate || ledBleUpdate || fwInstallBusy()) return LS_UPDATE;
    if (ledSys == LS_PORTAL) return LS_PORTAL;
    if (ledError) return LS_ERROR;
    return ledSys;
}
// ... with the short overlays on top: flash(), button
static uint8_t ledSysNow(uint32_t now)
{
    if (ledBefore(now, ledFlashUntil)) return (now - ledFlashStart) % (ledFlashOn + ledFlashOff) < ledFlashOn ? LS_FLASH : LS_OFF;
    if (ledBefore(now, ledButtonUntil)) return LS_BUTTON;
    return ledSysState();
}
static void ledRgb(uint8_t* px, uint8_t r, uint8_t g, uint8_t b, uint8_t bright)
{
    const uint8_t c[3] = {r, g, b};
    for (int i = 0; i < 3; i++) {
        uint8_t v = (uint32_t)c[i] * bright * LED_MAX_PWM / (255 * 100);
        px[i] = c[i] && !v ? 1 : v;   // keep the hue at low brightness
    }
}
static void ledPut(uint8_t* px, uint8_t s, uint8_t step)
{
    const LedLook& l = LED_LOOK[s];
    if (l.pattern & (0x8000 >> step)) ledRgb(px, l.r, l.g, l.b, ledCfg.brightness);
}
// The button overlay (ledHold, ledConfirmUntil); false = none, draw the states
static bool ledButtonOverlay(uint8_t* px, uint32_t now)
{
    uint8_t bright = ledCfg.brightness < 25 ? 25 : ledCfg.brightness;   // feedback must be seen
    if (ledHold == 2) {
        int32_t left = (int32_t)(ledResetAt - now);
        if (left < 0) left = 0;
        uint32_t period = 100 + left / 10;   // 5 s before: 0.6 s, at the end: 0.1 s
        if (now % period < period / 2) { ledRgb(px, 255, 120, 0, bright); ledRgb(px + 3, 255, 120, 0, bright); }
        return true;
    }
    if (ledHold == 1 || ledBefore(now, ledConfirmUntil)) { ledRgb(px, 255, 255, 255, bright); return true; }
    return false;
}
// esp_timer task, every 25 ms: only short reads of the states, the RMT write does not wait
static void ledRender(void*)
{
    static uint8_t last[6];
    static uint32_t lastSent = 0;
    uint32_t now = millis();
    uint8_t px[6] = {0};
    if (ledButtonOverlay(px, now)) {}
    else if (ledCfg.enabled && !ledNight) {
        uint8_t step = (now / 125) & 15;
        ledPut(px, ledSysNow(now), step);
        uint8_t st = ledStove;
        if (ledStoveWarn && step < 4) ledPut(px + 3, LS_WARNING, step);   // the warning's window
        else ledPut(px + 3, st, step);
    }
    if (memcmp(px, last, sizeof(px)) || now - lastSent >= 1000) {   // once a second anyway
        ws2812Show(px);
        memcpy(last, px, sizeof(px));
        lastSent = now;
    }
}

static void ledLoadSettings()
{
    Preferences p;
    p.begin("leds", true);
    ledCfg.enabled = p.getBool("on", true);
    ledCfg.brightness = constrain(p.getUChar("bright", 25), 1, 100);
    ledCfg.night = p.getBool("night", false);
    ledCfg.nightFrom = p.getUChar("nfrom", 22) % 24;
    ledCfg.nightTo = p.getUChar("nto", 7) % 24;
    p.end();
}
static void ledSaveSettings()
{
    Preferences p;
    p.begin("leds", false);
    p.putBool("on", ledCfg.enabled);
    p.putUChar("bright", ledCfg.brightness);
    p.putBool("night", ledCfg.night);
    p.putUChar("nfrom", ledCfg.nightFrom);
    p.putUChar("nto", ledCfg.nightTo);
    p.end();
}

// First in setup(): the LEDs show "booting" from here on
static void statusLedSetup()
{
    ledLoadSettings();
    if (!ws2812Begin(BOARD_WS2812_PIN, BOARD_WS2812_COUNT)) return;
    ledRender(nullptr);
    const esp_timer_create_args_t a = {ledRender, nullptr, ESP_TIMER_TASK, "leds", false};
    if (esp_timer_create(&a, &ledTimer) == ESP_OK) esp_timer_start_periodic(ledTimer, 25000);
}
void ledsStateTick();   // statusLedApi.h: states from WiFi and the stove
static inline void statusLedLoop() { ledsStateTick(); }

// Install from GitHub after a restart (fwBootInstall, nothing else runs yet): LED1 "update"
// while this lives
struct StatusLedUpdate {
    StatusLedUpdate()  { ledBootUpdate = true; }
    ~StatusLedUpdate() { ledBootUpdate = false; }
};

struct WsLed {
    void on() { ledSys = LS_CONNECTED; }
    void off() {}
    void toggle() { ledError = false; ledButtonUntil = millis() + 150; }
    void flash(uint8_t count = 2, uint16_t onMs = 160, uint16_t offMs = 160, uint16_t = 200, uint16_t = 300)
    {
        ledFlashOn = onMs ? onMs : 1; ledFlashOff = offMs;
        ledFlashStart = millis();
        ledFlashUntil = ledFlashStart + (uint32_t)count * (ledFlashOn + ledFlashOff);
    }
};
WsLed led;
WsLed onboardLED;
#else
struct NoLed {
    void on() {}
    void off() {}
    void toggle() {}
    void flash(uint8_t = 2, uint16_t = 160, uint16_t = 160, uint16_t = 200, uint16_t = 300) {}
};
NoLed led;
NoLed onboardLED;
static inline void statusLedLoop() {}
#endif
