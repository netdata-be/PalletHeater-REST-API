#pragma once
// Power source of the board (BOARD_HAS_POWER_SENSE): the TPS2116 power mux (U8) feeds the
// board from the stove's 5 V first and falls back to USB below ~4.3 V; its ST output on
// BOARD_POWER_SENSE_PIN is high on stove power, low on USB (10 kOhm pull-up on the board).
// Debounced here; a switchover is logged (LOGI) and stored in the event log (EVT_POWER).
// Included after events.h (evtStore) by boardInfo.h.
#include "board.h"

#if BOARD_HAS_POWER_SENSE

enum : int8_t { PWR_UNKNOWN = 0, PWR_STOVE = 1, PWR_USB = 2 };   // also the from / to of EVT_POWER
static int8_t   powerSrc = PWR_UNKNOWN;    // debounced source
static uint32_t powerSinceMs = 0;          // millis() of the last switchover (0 = since the start)
static uint32_t powerSwitches = 0;         // switchovers since the start
static int8_t   powerLogged = -1;          // last source in the event log (-1 not looked up yet, 0 none)

static const char* powerSrcName(int s) { return s == PWR_STOVE ? "stove" : s == PWR_USB ? "usb" : "unknown"; }

// Epoch time of the switch to the current source (the start if none since), 0 without a valid clock
static uint32_t powerSince()
{
    time_t now = time(nullptr);
    if (now < 1700000000) return 0;
    return (uint32_t)now - (millis() - powerSinceMs) / 1000;
}

static void powerSourceTick()
{
    // sample every 20 ms; a new level counts after 5 equal samples (100 ms). The mux switches
    // in ~8 us, so this only filters glitches on the line.
    static uint32_t lastSample = 0;
    static uint8_t  same = 0;
    static int8_t   seen = PWR_UNKNOWN;
    uint32_t ms = millis();
    if (powerSrc != PWR_UNKNOWN && ms - lastSample < 20) return;
    lastSample = ms;
    int8_t s = digitalRead(BOARD_POWER_SENSE_PIN) ? PWR_STOVE : PWR_USB;
    if (powerSrc == PWR_UNKNOWN) {   // first read at the start
        powerSrc = seen = s;
        LOGI("sys", "powered from %s", s == PWR_STOVE ? "the stove" : "USB");
    }
    if (s != seen) { seen = s; same = 0; }
    if (s != powerSrc && ++same >= 5) {
        LOGI("sys", "power source %s -> %s", powerSrc == PWR_STOVE ? "stove" : "USB", s == PWR_STOVE ? "stove" : "USB");
        powerSrc = s; powerSinceMs = ms; powerSwitches++;
    }
    // event log: once the clock is valid (as the other events). A switch while the controller
    // was off (e.g. the stove supply gone, later started on USB) is logged at the start.
    time_t tn = time(nullptr);
    if (tn < 1700000000) return;
    if (powerLogged < 0) {
        powerLogged = 0;
        for (int i = 0; i < evtCount; i++) {   // newest first
            const StateEvent& e = evts[(evtHead - 1 - i + 2 * EVT_SIZE) % EVT_SIZE];
            if (e.kind == EVT_POWER) { powerLogged = e.to; break; }
        }
    }
    if (powerSrc == powerLogged) return;
    if (powerLogged > 0) {   // not the first ever: nothing to compare with
        StateEvent e = { (uint32_t)tn, EVT_POWER, powerLogged, powerSrc, 0 };
        evtStore(e);
    }
    powerLogged = powerSrc;
}

#endif
