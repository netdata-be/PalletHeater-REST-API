#pragma once
// Status LEDs, the same calls on every board:
//   onboardLED  system state: on = WiFi connected, flashing = (re)connecting / full reset
//   led         toggled by a short press of the button
// DevKit: two plain LEDs (EasyLed). Boards without them (BOARD_HAS_ONBOARD_LED 0) get a
// no-op stand-in with the same API; the WS2812 driver (BOARD_HAS_WS2812) replaces it there
// (LED1 system = onboardLED, LED2 stove), statusLedLoop() is its hook in loop().
#include "board.h"

#if BOARD_HAS_ONBOARD_LED
#include "EasyLed.h"
EasyLed led(BOARD_STATUS_LED_PIN, EasyLed::ActiveLevel::High);
EasyLed onboardLED(BOARD_ONBOARD_LED_PIN, EasyLed::ActiveLevel::High);
static inline void statusLedLoop() {}
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
