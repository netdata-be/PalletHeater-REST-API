#pragma once
// The hardware this firmware is built for: pins and what the board has. One place for
// both boards, selected at build time (platformio.ini):
//   -DBOARD_OPB_S3   OpenPelletBridge board, ESP32-S3-MINI-1-N8 (hardware/, envs opb-s3*)
//   (none)           ESP32 DevKit with the stove interface on a breadboard (envs esp32doit-devkit-v1-*)
// GPIO table: hardware/TODO.md, "GPIO: DevKit (now) -> board".
// Capability flags are always defined, 0 or 1: test them with #if, not #ifdef.
// Board name: also in scripts/buildinfo.py (BUILD_BOARD), keep both the same.
#include <Arduino.h>

#if defined(BOARD_OPB_S3)

#if !CONFIG_IDF_TARGET_ESP32S3
#error "BOARD_OPB_S3 needs an ESP32-S3 build (board = esp32-s3-devkitc-1)"
#endif
#define BOARD_NAME              "opb-s3"
#define BOARD_HW_REV            "1.0"
// Stove UART (UART2), through optocouplers U5 (TX) / U6 (RX)
#define BOARD_STOVE_RX_PIN      18
#define BOARD_STOVE_TX_PIN      17
// Signal polarity through the optocouplers: TX low -> DATA low, DATA low -> RX low by design.
// Flip to true if the loopback test at bring-up shows it inverted.
#define BOARD_STOVE_UART_INVERT false
#define BOARD_HAS_RX_ENABLE     0        // the optocouplers need no enable
#define BOARD_BUTTON_PIN        0        // BOOT / USER button, also a strapping pin: input only, never drive it
#define BOARD_HAS_ONBOARD_LED   0        // LED1 (WS2812) takes over
#define BOARD_HAS_WS2812        1
#define BOARD_WS2812_PIN        8        // through SN74LV1T34 level shifter
#define BOARD_WS2812_COUNT      2        // LED1 system, LED2 stove
#define BOARD_HAS_BUZZER        1
#define BOARD_BUZZER_PIN        21       // PWM ~4 kHz (resonance) via Q1
#define BOARD_HAS_POWER_SENSE   1
#define BOARD_POWER_SENSE_PIN   4        // TPS2116 ST: high = stove, low = USB (10 kOhm pull-up on the board)

#else   // ESP32 DevKit

#define BOARD_NAME              "esp32-devkit"
// no BOARD_HW_REV: hand-wired, no revision
#define BOARD_STOVE_RX_PIN      16
#define BOARD_STOVE_TX_PIN      17
#define BOARD_STOVE_UART_INVERT false
#define BOARD_HAS_RX_ENABLE     1
#define BOARD_RX_ENABLE_PIN     18       // high = the LED of the RX optocoupler off
#define BOARD_BUTTON_PIN        13
#define BOARD_HAS_ONBOARD_LED   1
#define BOARD_ONBOARD_LED_PIN   2
#define BOARD_STATUS_LED_PIN    14
#define BOARD_HAS_WS2812        0
#define BOARD_HAS_BUZZER        0
#define BOARD_HAS_POWER_SENSE   0

#endif

// Pin set-up at the very start of setup(), before anything else touches the pins
static inline void boardPinsSetup()
{
#if BOARD_HAS_RX_ENABLE
    pinMode(BOARD_RX_ENABLE_PIN, OUTPUT);
    digitalWrite(BOARD_RX_ENABLE_PIN, HIGH);   // the LED of the optocoupler is off
#endif
#if BOARD_HAS_BUZZER
    pinMode(BOARD_BUZZER_PIN, OUTPUT);         // silent until the buzzer driver takes over
    digitalWrite(BOARD_BUZZER_PIN, LOW);
#endif
#if BOARD_HAS_POWER_SENSE
    pinMode(BOARD_POWER_SENSE_PIN, INPUT);     // pull-up on the board
#endif
}
