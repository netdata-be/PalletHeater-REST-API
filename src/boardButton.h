#pragma once
// The board's button (BOOT / IO0, BOARD_HAS_BUTTON_MENU): several functions on one button,
// with feedback on the LEDs (statusLed.h) and the buzzer (buzzer.h). The DevKit keeps its
// short press and 5 s full reset (main.cpp).
//   1x short      button click; mutes a sounding alarm; clears the red error on LED1
//   2x short      LEDs on / off (setting, kept)
//   3x short      buzzer on / off (setting, kept; a rising / falling tone confirms)
//   hold 1 s      LED1 white: "held"; released before the countdown: nothing
//   hold 3 s      the full reset counts down: both LEDs amber, faster towards the end, a tick
//                 every second; released before the end: aborted
//   hold 8 s      full reset (WiFi settings and SPIFFS erased, restart)
// The decisions are pc::ButtonDecoder (lib/pelletcore, unit tested).
#include "board.h"

#if BOARD_HAS_BUTTON_MENU

static pc::ButtonDecoder btn;

static void boardButtonTick()
{
    uint32_t now = millis();
    uint8_t n;
    pc::ButtonDecoder::Event ev = btn.update(resetButton.isPressed(), now, n);
    // LED feedback while held
    pc::ButtonDecoder::Stage st = btn.stage(now);
    static int lastSecond = -1;
    if (st == pc::ButtonDecoder::COUNTDOWN) {
        ledResetAt = now + btn.resetInMs(now);
        ledHold = 2;
        int sec = (int)(btn.resetInMs(now) / 1000);
        if (sec != lastSecond) {   // a tick every second of the countdown
            if (lastSecond < 0) LOGW("sys", "button held: full reset in %lu s unless released", (unsigned long)(btn.resetInMs(now) / 1000 + 1));
            lastSecond = sec;
            buzzerPlay(pc::beepPresetIndex("tick"));
        }
    } else {
        ledHold = st == pc::ButtonDecoder::HELD ? 1 : 0;
        lastSecond = -1;
    }
    switch (ev) {
    case pc::ButtonDecoder::PRESSES:
        if (n == 1) buttonPressed();   // stoveControl.h
        else if (n == 2) {
            ledCfg.enabled = !ledCfg.enabled;
            ledSaveSettings();
            ledConfirmUntil = now + 400;
            buzzerEvent(BZ_BUTTON);
            LOGI("sys", "button pressed twice: status LEDs %s", ledCfg.enabled ? "on" : "off");
        } else if (n == 3) {
            bool on = buzzerToggle();
            ledConfirmUntil = now + 400;
            LOGI("sys", "button pressed three times: buzzer %s", on ? "on" : "off");
        } else LOGI("sys", "button pressed %u times: no function", n);
        break;
    case pc::ButtonDecoder::ABORTED:
        ledHold = 0;
        buzzerPlay(pc::beepPresetIndex("falling"));
        LOGI("sys", "button released: full reset aborted");
        break;
    case pc::ButtonDecoder::RESET:
        ledHold = 0;
        fullReset();   // stoveControl.h: logs, erases, restarts
        break;
    default: break;
    }
}

#endif
