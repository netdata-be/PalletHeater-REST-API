#pragma once
// Telnet debug menu on port 23: the screens.
//
//   telnetServer.h  the TCP server (IPv4 + IPv6, one session, take-over), keys
//   telnetUi.h      output helpers, page stack, generic menu / picker / input / confirm / view
//   telnetMenu.h    this file: the screens, built from tables of TmItem
//
// Included by main.cpp after all other modules: the screens show what those modules
// keep (pellet estimate, events, history, firmware update, MQTT, HTTPS, WiFi) and
// reuse their functions for every action (doStoveOn / doStoveOff, setFlamePower,
// refreshTimerCache, fwStart, ...), so telnet behaves like the web app and the API.
// Switching the stove on is refused while the pellets are used up (pelletsEmptyOff),
// as on every other way in.
//
// Public: setupTelnet(), telnetStop(), telnetLoop() (main.cpp, routesDevice.h).
#include "telnetServer.h"
#include "telnetUi.h"
#include "telnetChart.h"

// ─────────────────────────────────────────────────────────────────────
// Header and alerts
// ─────────────────────────────────────────────────────────────────────

static const char* tmStateColor(int s)
{
    if (s == UNKNOWN || pc::alarmState(s)) return A_RED;
    if (s == WORKING) return A_GRN;
    if (s == OFF) return A_DIM;
    return A_YEL;
}

// Warnings shown under the header of every screen (and on the status page)
static void tmAlerts()
{
    if (lastFastStatesAt && (stove.linkDown || stoveState == UNKNOWN)) {
        tpl(A_RED " ! No answer from the stove (serial link down)" A_RST);
    } else {
        if (confirmedCurrentError) tpf(A_RED " ! Error %s: %s" A_RST A_EOL, pc::errorCode(confirmedCurrentError), pc::errorDesc(confirmedCurrentError));
        else if (lastSlowStatesAt && pelletsEmptyOff() && !pc::reserveRefilling(cachedPelletSensor)) tpl(A_RED " ! PELLETS: pellets used up, the stove switched itself off - refill the hopper" A_RST);
        if (cachedCurrentError > 0 && cachedCurrentError != confirmedCurrentError) {
            tpf(A_YEL " ! Stove reports %s (%s)", pc::errorCode(cachedCurrentError), pc::errorDesc(cachedCurrentError));
            if (cachedErrorCountdown >= 0) tpf(" - acts in %d s", cachedErrorCountdown);
            tpl(A_RST);
        }
        if (lastSlowStatesAt && pc::reserveRefilling(cachedPelletSensor)) {
            tpf(A_GRN " Pellets refilled: the stove confirms them (level sensor 0xA4 = %d, about %d s)%s" A_RST A_EOL,
                cachedPelletSensor, cachedPelletSensor * pc::RESERVE_STEP_S, pelletsEmptyOff() ? " - switching on is possible after that" : "");
        } else if (lastSlowStatesAt && !pelletsEmptyOff()) {
            if (cachedPelletShutdown >= 0) tpf(A_YEL " ! Pellet reserve: the stove switches itself off in about %d min" A_RST A_EOL, (cachedPelletShutdown + 1) / 2);
            else if (cachedLowPallet) tpl(A_YEL " ! Low pellets - refill the hopper" A_RST);
            else if (pc::reserveSensorLow(cachedPelletSensor) && cachedPelletCounter >= 0)
                tpf(A_YEL " ! Pellet level sensor low: low pellet alarm in about %d min" A_RST A_EOL, cachedPelletCounter);
        }
    }
    {   // warning: the stove regulates on its own sensor although the wireless sensor is on
        if (remoteSensorNotUsed())
            tpf(A_YEL " ! Stove regulates on its own sensor (%.1f C), not the remote (%.1f C)%s" A_RST A_EOL,
                ambTempStove, ambTempRemoteControl, cachedRemoteControlLastSeen < 0 ? ": remote not heard" : "");
    }
    if (fwState == FW_DOWNLOADING && fwTotal) tpf(A_CYN " Firmware update: downloading %u %%" A_RST A_EOL, (unsigned)(fwDone * 100 / fwTotal));
    if (fwState == FW_RESTARTING) tpl(A_CYN " Firmware update installed - restarting" A_RST);
}

static void tmHeader()
{
    char clk[10] = "--:--:--";
    time_t now = time(nullptr);
    if (now > 1700000000) { struct tm tm; localtime_r(&now, &tm); strftime(clk, sizeof(clk), "%H:%M:%S", &tm); }
    char left[72];
    snprintf(left, sizeof(left), " OpenPelletBridge  %s  %s", WiFi.getHostname() ? WiFi.getHostname() : "", BUILD_VERSION);
    tpf(A_INV "%-68.68s%9s " A_RST A_EOL, left, clk);
    if (!lastFastStatesAt) {
        tpl(A_DIM " Waiting for the first reading of the stove" A_RST);
    } else {
        tpf(" %s%-16s" A_RST, tmStateColor(stoveState), StoveStateStr);
        if (lastSlowStatesAt) tpf("  room %.1f C  set %d C", ambTemp, (int)setPointTemp);
        tpf("  flame %d", flamePower);
        if (cachedFlameActual >= 0 && cachedFlameActual != flamePower && StoveIsOn) tpf(" (runs %d)", cachedFlameActual);
        tpf("  fan %d", roomFanSpeed);
        double rem = pellets.remainingKg();
        if (rem >= 0) tpf("  pellets ~%.0f kg", rem);
        tpl();
    }
    tmAlerts();
    tpl();
}

// ─────────────────────────────────────────────────────────────────────
// Status (refreshes itself)
// ─────────────────────────────────────────────────────────────────────

static void tmErrorLines()
{
    if (stoveState == UNKNOWN && lastFastStatesAt) tkv("Error", "LINK - no answer from the stove (serial link down)");
    else if (confirmedCurrentError) tkv("Error", "%s - %s (stove)", pc::errorCode(confirmedCurrentError), pc::errorDesc(confirmedCurrentError));
    else if (lastSlowStatesAt && pelletsEmptyOff()) tkv("Error", "PELLETS - pellets used up, switched off by the stove (controller)");
    else tkv("Error", "none");
    if (stoveState != UNKNOWN && cachedCurrentError > 0) {
        String cd = cachedErrorCountdown >= 0 ? String(", the stove acts in ") + cachedErrorCountdown + " s" : String("");
        tkv("Reported now", "%s - %s%s", pc::errorCode(cachedCurrentError), pc::errorDesc(cachedCurrentError), cd.c_str());
    }
}

// Pellet reserve (installer menu PELLET RESERVE) and the level sensor
static void tmReserveLines()
{
    if (!lastSlowStatesAt) { tkv("Pellet reserve", "not read yet"); return; }
    String s = cachedPelletReserveOn < 0 ? String("?") : cachedPelletReserveOn ? String("enabled") : String("disabled");
    if (pc::reserveRefilling(cachedPelletSensor)) s += " - refilled, the stove confirms the pellets";
    else if (pelletsEmptyOff()) s += " - pellets used up, the stove switched itself off";
    else if (cachedPelletShutdown >= 0) s += String(" - running: the stove switches off in about ") + ((cachedPelletShutdown + 1) / 2) + " min";
    else if (pc::reserveSensorLow(cachedPelletSensor) && cachedPelletCounter >= 0) s += String(" - level sensor low, alarm in about ") + cachedPelletCounter + " min";
    else if (cachedLowPallet) s += " - low pellet alarm";
    tkv("Pellet reserve", "%s", s.c_str());
    tkv("Level sensor", "RAM 0xA4 = %d%s, counter RAM 0x89 = %d", cachedPelletSensor, pc::reserveRefilling(cachedPelletSensor) ? " (refill being confirmed)" : pc::reserveSensorLow(cachedPelletSensor) ? " (low)" : "", cachedPelletCounter);
}

// The estimate until the low pellet alarm (pellets.h, lib/pelletcore PelletTracker)
static void tmPelletLines(bool detail)
{
    double k = pellets.kgPerUnit(), rem = pellets.remainingKg();
    if (k <= 0) tkv("Pellets", "no estimate yet: needs a refill and the next low pellet alarm (%d cycle(s))", pellets.nCycles);
    else if (rem < 0) tkv("Pellets", "calibrated, waiting for the next low pellet alarm");
    else {
        double h = pellets.hoursAt(cachedAugerSpeed);
        String at = h >= 0 ? String(", ") + fmtAgo((uint32_t)(h * 3600)) + " at the current rate" : String(", stove not feeding now");
        tkv("Pellets", "~%.1f kg until the low pellet alarm%s", rem, at.c_str());
        int avgHours = 0;
        float avg = pelletsAvgAuger(&avgHours);
        double ha = pellets.hoursAt(avg);
        if (ha >= 0 && avgHours >= 6 && time(nullptr) > 1700000000)
            tkv("", "at the 7-day average: %s, alarm expected %s", fmtAgo((uint32_t)(ha * 3600)).c_str(), fmtTime(time(nullptr) + (time_t)(ha * 3600)).c_str());
        if (detail && cachedAugerSpeed > 0) tkv("Use now", "%.2f kg/h", k * cachedAugerSpeed * 3600.0);
    }
    if (!detail) return;
    if (k > 0) tkv("Calibration", "%d cycle(s), %.4f g per auger unit%s", pellets.nCycles, k * 1000.0,
                   pellets.nCycles >= 2 ? (String(", spread ") + String(pellets.spread() * 100.0, 1) + " %").c_str() : "");
    if (pellets.cycleStart >= 0) tkv("Since the last alarm", "%.1f kg added, %.0f auger units burned", pellets.cycleKg, pellets.units - pellets.cycleStart);
    tkv("Refill amount", "%.1f kg (counted when a refill is detected)", pellets.refillKg);
    if (pellets.lastRefillT) tkv("Last refill", "%s, %.1f kg", fmtTime(pellets.lastRefillT).c_str(), pellets.lastRefillKg);
    if (pellets.pendingKg > 0) tkv("Entered by hand", "%.1f kg, used for the refill that follows", pellets.pendingKg);
    tkv("Hopper", "%s", pellets.low ? "low (between the alarm and the refill)" : "above the sensor");
}

static void tmStatusRender()
{
    const bool fastOk = lastFastStatesAt != 0, slowOk = lastSlowStatesAt != 0;
    if (!fastOk) { tpl("   No data from the stove yet."); return; }
    tkv("Stove", "%s%s", StoveStateStr, stoveState == UNKNOWN ? "" : StoveIsOn ? " (on)" : " (off)");
    tmErrorLines();
    if (slowOk) {
        tkv("Temperatures", "room %s, stove sensor %s, remote %s", fmtTemp(ambTemp).c_str(), fmtTemp(ambTempStove).c_str(), fmtTemp(ambTempRemoteControl).c_str());
        tkv("Set point", "%d C   (wireless sensor %s)", (int)setPointTemp, wirelessSensorStr());
    }
    tkv("Fumes", "%d C, fume fan %d rpm", fumesTemp, cachedFumeFanRPM);
    tkv("Flame power", "set %d, runs at %s", flamePower, cachedFlameActual >= 0 ? String(cachedFlameActual).c_str() : "?");
    {
        int act = roomFanActual();
        tkv("Room fan", "set %d, runs at %s (power %d)%s", roomFanSpeed, act < 0 ? "?" : String(act).c_str(), cachedRoomFanControl,
            cachedHotForFan == 0 ? ", stove not hot enough yet" : "");
    }
    if (slowOk) tkv("Auger", "%.1f s per 12 s cycle (%d %% of the time)", cachedAugerSpeed, pc::augerPercent(cachedAugerSpeed));
    if (slowOk) {
        if (energySavingStop) {
            String rs = energySavingStart ? String(", restarts at ") + (int)(setPointTemp + energySavingStart) + " C" : String(", no automatic restart");
            tkv("Energy saving", "on: stops at %d C%s", (int)(setPointTemp + energySavingStop), rs.c_str());
        } else tkv("Energy saving", "off");
    }
    if (stoveState == WORKING && cachedBrazierCountdown >= 0) tkv("Brazier cleaning", "in %d min (every %d min)", cachedBrazierCountdown, cachedBrazierInterval);
    else if (stoveState == BRAZIER_CLEANING) tkv("Brazier cleaning", "running");
    tmPelletLines(false);
    tmReserveLines();
    if (cachedRemoteControlLastSeen >= 0) tkv("Remote control", "heard %d s ago", cachedRemoteControlLastSeen);
    else tkv("Remote control", "not heard recently");
    if (stoveTimeReadMs) {
        long d = (long)(ntpTimeNow() - stoveTimeNow());
        tkv("Stove clock", "%s (%ld s %s NTP)", fmtTime(stoveTimeNow(), "%d/%m/%Y %H:%M:%S").c_str(), labs(d), d >= 0 ? "behind" : "ahead of");
    }
    if (slowOk) tkv("Counters", "%d starts, %d h burning, %d h since the service", cachedNumberOfStarts, cachedTotalRuntime, cachedHoursSinceLastService);
    tkv("Data age", "fast %lu s, slow %s", (millis() - lastFastStatesAt) / 1000, slowOk ? (String((millis() - lastSlowStatesAt) / 1000) + " s").c_str() : "not read yet");
}
static const TmView TM_STATUS = { "Status", tmStatusRender, 2000, false };

// ─────────────────────────────────────────────────────────────────────
// Live values (scrolling, every 2 s)
// ─────────────────────────────────────────────────────────────────────

static const char* const TM_LIVE_GROUPS[] = {
    "Stove state", "Temperatures", "Flame power", "Fans", "Auger", "Pellets",
    "Energy saving", "Errors", "Remote control / display", "Network",
};
static const int TM_LIVE_N = sizeof(TM_LIVE_GROUPS) / sizeof(TM_LIVE_GROUPS[0]);
static uint16_t tmLiveMask = 0x000F;

static void tmLiveTick()
{
    tpf(A_BOLD "--- %s ---" A_RST "\r\n", fmtTime(time(nullptr), "%H:%M:%S").c_str());
    if (tmLiveMask & (1 << 0)) tpf("State:        %d (%s)\r\n", stoveState, StoveStateStr);
    if (tmLiveMask & (1 << 1)) {
        tpf("Temperatures: room %.1f  stove %.1f  remote %.1f  set point %d  fumes %d C\r\n", ambTemp, ambTempStove, ambTempRemoteControl, (int)setPointTemp, fumesTemp);
    }
    if (tmLiveMask & (1 << 2)) tpf("Flame power:  set %d, actual %d\r\n", flamePower, cachedFlameActual);
    if (tmLiveMask & (1 << 3)) {
        int act = roomFanActual();
        tpf("Fans:         room fan set %d, actual %d (power 0x0A = %d%s), fume fan %d rpm\r\n", roomFanSpeed, act, cachedRoomFanControl,
            roomFanIsRunning() == 0 ? ", stopped" : "", cachedFumeFanRPM);
    }
    if (tmLiveMask & (1 << 4)) tpf("Auger:        %.1f s (%d %%)\r\n", cachedAugerSpeed, pc::augerPercent(cachedAugerSpeed));
    if (tmLiveMask & (1 << 5)) {
        double rem = pellets.remainingKg(), h = pellets.hoursAt(cachedAugerSpeed);
        tpf("Pellets:      low alarm %s, sensor 0xA4 = %d, counter 0x89 = %d, reserve 0x91 = %d, estimate %s kg / %s h%s\r\n",
            cachedLowPallet ? "YES" : "no", cachedPelletSensor, cachedPelletCounter, cachedPelletShutdown,
            rem >= 0 ? String(rem, 1).c_str() : "?", h >= 0 ? String(h, 1).c_str() : "?", pelletsEmptyOff() ? ", USED UP" : "");
    }
    if (tmLiveMask & (1 << 6)) tpf("Energy save:  stop +%d, restart -%d\r\n", energySavingStop, energySavingStart);
    if (tmLiveMask & (1 << 7)) {
        tpf("Errors:       now %d (%s), confirmed %d, countdown %d s%s\r\n", cachedCurrentError, pc::errorCode(cachedCurrentError),
            confirmedCurrentError, cachedErrorCountdown, pelletsEmptyOff() ? ", PELLETS" : "");
    }
    if (tmLiveMask & (1 << 8)) {
        int menuId, paramId;
        { StoveBusLock lock; menuId = stove.read(ram, displayMenuAddr); paramId = stove.read(ram, selectedParamAddr); }
        const char* param = paramId >= 0 ? getSelectedParameterName(paramId) : "?";
        tpf("Remote:       heard %d s ago, menu %s, selected %s\r\n", cachedRemoteControlLastSeen,
            menuId >= 0 ? getRemoteMenuName(menuId) : "?", param ? param : "none");
    }
    if (tmLiveMask & (1 << 9)) {
        tpf("Network:      WiFi %d dBm, heap %u KB free (largest block %u KB)\r\n", WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0,
            (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));
    }
}
static const TmView TM_LIVE = { "Live values (every 2 s)", tmLiveTick, 2000, true };

static void tmLiveValue(int i, String& v)
{
    if (i < TM_LIVE_N) v = (tmLiveMask & (1 << i)) ? "[x]" : "[ ]";
}
static void tmLiveToggle(int i) { tmLiveMask ^= (1 << i); }
static void tmLiveAll(int) { tmLiveMask = tmLiveMask == (1 << TM_LIVE_N) - 1 ? 0 : (1 << TM_LIVE_N) - 1; }
static void tmLiveStart(int)
{
    if (!tmLiveMask) { tmMsgSet(false, "Select at least one group."); return; }
    tmView(&TM_LIVE);
}
static const TmItem TM_LIVE_ITEMS[] = {
    { "Stove state",    tmLiveValue, tmLiveToggle, "State number and name (RAM 0x21)", TI_TOGGLE },
    { "Temperatures",   tmLiveValue, tmLiveToggle, "Room, stove sensor, remote control, set point, fumes", TI_TOGGLE },
    { "Flame power",    tmLiveValue, tmLiveToggle, "Setting and the power the stove runs at now", TI_TOGGLE },
    { "Fans",           tmLiveValue, tmLiveToggle, "Room fan (set / actual / power) and fume fan", TI_TOGGLE },
    { "Auger",          tmLiveValue, tmLiveToggle, "On-time per 12 s cycle and the share of the time it runs", TI_TOGGLE },
    { "Pellets",        tmLiveValue, tmLiveToggle, "Low pellet alarm, level sensor, reserve counters, estimate", TI_TOGGLE },
    { "Energy saving",  tmLiveValue, tmLiveToggle, "Stop / restart offsets", TI_TOGGLE },
    { "Errors",         tmLiveValue, tmLiveToggle, "Error reported now, confirmed error, countdown", TI_TOGGLE },
    { "Remote control / display", tmLiveValue, tmLiveToggle, "Menu shown on the remote (reads 2 registers every 2 s)", TI_TOGGLE },
    { "Network",        tmLiveValue, tmLiveToggle, "WiFi signal and free memory", TI_TOGGLE },
    { "All / none",     nullptr, tmLiveAll, nullptr, TI_ACT },
    { "Start",          nullptr, tmLiveStart, "Prints the selected values every 2 s; any key stops", TI_VIEW },
};
static const TmMenu TM_LIVE_MENU = { "Live values - choose what to show", TM_LIVE_ITEMS, sizeof(TM_LIVE_ITEMS) / sizeof(TM_LIVE_ITEMS[0]), nullptr, 0 };

// ─────────────────────────────────────────────────────────────────────
// Control: power and settings
// ─────────────────────────────────────────────────────────────────────

static void tmPowerOnYes()
{
    getStoveState();
    int code = 0;
    String r = doStoveOn(code);   // refuses while the pellets are used up (PELLETS)
    getStoveState();
    tmMsgSet(code < 300, r);
}
static void tmPowerOffYes()
{
    getStoveState();
    int code = 0;
    String r = doStoveOff(code);
    getStoveState();
    tmMsgSet(code < 300, r);
}
static void tmPowerOpen(int)
{
    if (!lastFastStatesAt || stoveState == UNKNOWN) { tmMsgSet(false, "The stove does not answer - try again later."); return; }
    if (!StoveIsOn && pelletsEmptyOff()) {
        tmMsgSet(false, "Refused: pellets used up (the stove switched itself off at the end of the pellet reserve) - refill the hopper first.");
        return;
    }
    if (StoveIsOn) tmConfirmStart(String("   Switch the stove OFF?  (now: ") + StoveStateStr + ")", tmPowerOffYes);
    else           tmConfirmStart(String("   Switch the stove ON?  (now: ") + StoveStateStr + ")", tmPowerOnYes);
}

static bool tmFail(const char* what) { tmMsgSet(false, String(what) + " failed (no answer from the stove)."); return false; }

static void fmtC(int v, String& o) { o = String(v) + " C"; }
static void fmtNum(int v, String& o) { o = String(v); }
static void fmtFan(int v, String& o) { o = v ? String(v) : String("0 (off)"); }
static void fmtOnOff(int v, String& o) { o = v ? "on" : "off"; }
static void fmtEsStop(int v, String& o)
{
    o = v ? String("+") + v + " C  (stops at " + (int)(setPointTemp + v) + " C)" : String("0 = energy saving off");
}
static void fmtEsStart(int v, String& o)
{
    o = v ? String("-") + v + " C  (restarts at " + (int)(setPointTemp - v) + " C)" : String("0 = no automatic restart");
}
static const int TM_CQ[] = { 2, 3, 4, 5, 6, 7, 8, 76 };   // combustion quality values, label "-3" .. "+3", "+G"
static void fmtCq(int i, String& o) { const char* l = pc::combustionQualityLabel(TM_CQ[i]); o = l ? l : "?"; }
static void fmtPq(int v, String& o) { const char* l = pc::pelletQualityLabel(v); o = l ? l : "?"; }

static bool tmSetSetpoint(int v)
{
    if (!stove.write(eeprom, TempSetpointAddr, (byte)v)) return tmFail("Set point");
    setPointTemp = v;
    return true;
}
static bool tmSetFlame(int v) { if (!setFlamePower(v)) return tmFail("Flame power"); getFlamePower(); return true; }
static bool tmSetFan(int v) { if (!setRoomFanSpeed(v)) return tmFail("Room fan"); getRoomFanSpeed(); return true; }
static bool tmSetEsStop(int v) { if (!setEnergySavingStop(v)) return tmFail("Energy saving"); getEnergySaving(); return true; }
static bool tmSetEsStart(int v) { if (!setEnergySavingStart(v)) return tmFail("Energy saving"); getEnergySaving(); return true; }
static bool tmSetCq(int i)
{
    if (!stove.write(eeprom, combustionQualityAddr, TM_CQ[i])) return tmFail("Combustion quality");
    cachedCombustionQuality = TM_CQ[i];
    return true;
}
static bool tmSetPq(int v)
{
    if (!stove.write(eeprom, pelletQualityAddr, v)) return tmFail("Pellet quality");
    cachedPelletQuality = v;
    return true;
}
static bool tmSetBeep(int v)
{
    if (!stove.write(eeprom, beepEnabledAddr, v ? 1 : 0)) return tmFail("Beep");
    cachedBeepEnabled = v != 0;
    return true;
}
static void tmClockYes()
{
    setStoveDateTime();
    delay(200);
    getStoveDateTime();
    tmMsgSet(true, "Stove clock set to " + fmtTime(time(nullptr), "%d/%m/%Y %H:%M:%S") + ".");
}

static void tmCtlValue(int i, String& v)
{
    if (!lastSlowStatesAt && i != 0) { v = "-"; return; }
    switch (i) {
    case 0: v = stoveState == UNKNOWN || !lastFastStatesAt ? String("?") : String(StoveIsOn ? "on" : "off") + "  (" + StoveStateStr + ")"; break;
    case 1: fmtC((int)setPointTemp, v); break;
    case 2: v = String(flamePower); if (cachedFlameActual >= 0 && StoveIsOn) v += String("  (runs at ") + cachedFlameActual + ")"; break;
    case 3: fmtFan(roomFanSpeed, v); break;
    case 4: fmtEsStop(energySavingStop, v); break;
    case 5: fmtEsStart(energySavingStart, v); break;
    case 6: { const char* l = pc::combustionQualityLabel(cachedCombustionQuality); v = l ? l : "?"; break; }
    case 7: fmtPq(cachedPelletQuality, v); break;
    case 8: fmtOnOff(cachedBeepEnabled, v); break;
    case 9: if (stoveTimeReadMs) { long d = (long)(ntpTimeNow() - stoveTimeNow()); v = String(labs(d)) + " s " + (d >= 0 ? "behind" : "ahead of") + " NTP"; } break;
    }
}
static void tmCtlOpen(int i)
{
    if (i > 0 && !lastSlowStatesAt) { tmMsgSet(false, "The settings are not read from the stove yet."); return; }
    switch (i) {
    case 0: tmPowerOpen(i); break;
    case 1: tmPickStart("Set point (room temperature)", 7, 30, (int)setPointTemp, false, fmtC, tmSetSetpoint); break;
    case 2: tmPickStart("Flame power", 1, 4, flamePower, false, fmtNum, tmSetFlame, "The stove lowers the flame by itself near the set point"); break;
    case 3: tmPickStart("Room fan speed", 0, 3, roomFanSpeed, false, fmtFan, tmSetFan); break;
    case 4: tmPickStart("Energy saving: stop above the set point", 0, 3, energySavingStop, false, fmtEsStop, tmSetEsStop,
                        "The stove stops when the room is this much warmer than the set point (0 = off)"); break;
    case 5: tmPickStart("Energy saving: restart below the set point", 0, 3, energySavingStart, false, fmtEsStart, tmSetEsStart,
                        "After an energy saving stop: restart when the room is this much colder (0 = no restart)"); break;
    case 6: {
        int cur = 3;
        for (int k = 0; k < 8; k++) if (TM_CQ[k] == cachedCombustionQuality) cur = k;
        tmPickStart("Combustion quality", 0, 7, cur, false, fmtCq, tmSetCq, "Air for the combustion: -3 .. +3, +G");
        break;
    }
    case 7: tmPickStart("Pellet quality", 0, 10, cachedPelletQuality, false, fmtPq, tmSetPq, "Amount of pellets per auger cycle: -5 .. +5"); break;
    case 8: tmPickStart("Beep of the stove", 0, 1, cachedBeepEnabled ? 1 : 0, false, fmtOnOff, tmSetBeep); break;
    case 9:
        if (time(nullptr) <= 1700000000) { tmMsgSet(false, "No valid NTP time yet."); return; }
        tmConfirmStart("   Set the stove clock to " + fmtTime(time(nullptr), "%d/%m/%Y %H:%M:%S") + " (NTP)?", tmClockYes);
        break;
    }
}
static const TmItem TM_CTL_ITEMS[] = {
    { "Power",                 tmCtlValue, tmCtlOpen, "Switch on / off (asks for confirmation)", TI_ASK },
    { "Set point",             tmCtlValue, tmCtlOpen, "Room temperature to reach, 7 - 30 C", TI_EDIT },
    { "Flame power",           tmCtlValue, tmCtlOpen, "1 - 4", TI_EDIT },
    { "Room fan",              tmCtlValue, tmCtlOpen, "0 (off) - 3", TI_EDIT },
    { "Energy saving stop",    tmCtlValue, tmCtlOpen, "Stop the stove when the room is warmer than the set point", TI_EDIT },
    { "Energy saving restart", tmCtlValue, tmCtlOpen, "Restart after an energy saving stop", TI_EDIT },
    { "Combustion quality",    tmCtlValue, tmCtlOpen, "Combustion air: -3 .. +3, +G", TI_EDIT },
    { "Pellet quality",        tmCtlValue, tmCtlOpen, "Pellets per auger cycle: -5 .. +5", TI_EDIT },
    { "Beep",                  tmCtlValue, tmCtlOpen, "Beep of the stove on key presses", TI_EDIT },
    { "Sync stove clock",      tmCtlValue, tmCtlOpen, "Set the stove clock to the NTP time (the timers run on it)", TI_ACT },
};
static const TmMenu TM_CONTROL = { "Control", TM_CTL_ITEMS, sizeof(TM_CTL_ITEMS) / sizeof(TM_CTL_ITEMS[0]), nullptr, 2000 };

// ─────────────────────────────────────────────────────────────────────
// Timers PR1 - PR6
// ─────────────────────────────────────────────────────────────────────

static int  tmTimerIdx = 0;
static byte tmTimerOffset = 0;
static byte tmDays = 0;            // working copy while choosing the days
static char tmTimerTitle[24] = "";

static String tmDaysText(int mask)
{
    static const char* d[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    if ((mask & 0x7F) == 0x7F) return "every day";
    if ((mask & 0x7F) == 0x1F) return "Mon - Fri";
    if ((mask & 0x7F) == 0x60) return "Sat Sun";
    String s;
    for (int i = 0; i < 7; i++) if (mask & (1 << i)) { if (s.length()) s += " "; s += d[i]; }
    return s.length() ? s : String("no days");
}

static bool tmTimerWrite(byte offset, int v)
{
    bool ok = stove.write(eeprom, timerBases[tmTimerIdx] + offset, (byte)v);
    refreshTimerCache(tmTimerIdx);   // the cache used by the screens, BLE and the metrics
    if (!ok) tmFail("Timer");
    return ok;
}

static void tmTimersInfo()
{
    if (stoveTimeReadMs) tkv("Stove clock", "%s - the timers run on it", fmtTime(stoveTimeNow(), "%a %d/%m/%Y %H:%M").c_str());
}
static void tmTimerValue(int i, String& v)
{
    if (!lastSlowStatesAt) { v = "not read yet"; return; }
    v = cachedTimerEnabled[i] ? "on   " : A_DIM "off  ";
    v += timerValueToTimeStr(cachedTimerOnVal[i]) + " - " + timerValueToTimeStr(cachedTimerOffVal[i]);
    v += "  " + String(cachedTimerSetpoint[i]) + " C  flame " + cachedTimerFlame[i] + "  fan " + cachedTimerFan[i];
    v += "  " + tmDaysText(cachedTimerDowMask[i]);
    if (!cachedTimerEnabled[i]) v += A_RST;
}
extern const TmMenu TM_TIMER_EDIT;   // below (a menu that refers to a later one)
static void tmTimerOpen(int i)
{
    if (!lastSlowStatesAt) { tmMsgSet(false, "The timers are not read from the stove yet."); return; }
    tmTimerIdx = i;
    snprintf(tmTimerTitle, sizeof(tmTimerTitle), "Timer PR%d", i + 1);
    tmMenu(&TM_TIMER_EDIT);
}
static const TmItem TM_TIMERS_ITEMS[] = {
    { "PR1", tmTimerValue, tmTimerOpen, "Enter: change this program", TI_MENU },
    { "PR2", tmTimerValue, tmTimerOpen, "Enter: change this program", TI_MENU },
    { "PR3", tmTimerValue, tmTimerOpen, "Enter: change this program", TI_MENU },
    { "PR4", tmTimerValue, tmTimerOpen, "Enter: change this program", TI_MENU },
    { "PR5", tmTimerValue, tmTimerOpen, "Enter: change this program", TI_MENU },
    { "PR6", tmTimerValue, tmTimerOpen, "Enter: change this program", TI_MENU },
};
static const TmMenu TM_TIMERS = { "Timers (week programs)", TM_TIMERS_ITEMS, 6, tmTimersInfo, 0 };

static void fmtTimerTime(int v, String& o) { o = timerValueToTimeStr(v); if (v >= 144) o += "  (not used)"; }
static bool tmTimerPickApply(int v) { return tmTimerWrite(tmTimerOffset, v); }

static void tmTimerEditValue(int i, String& v)
{
    int t = tmTimerIdx;
    switch (i) {
    case 0: v = cachedTimerEnabled[t] ? "yes" : "no"; break;
    case 1: v = timerValueToTimeStr(cachedTimerOnVal[t]); break;
    case 2: v = timerValueToTimeStr(cachedTimerOffVal[t]); break;
    case 3: v = String(cachedTimerSetpoint[t]) + " C"; break;
    case 4: v = String(cachedTimerFlame[t]); break;
    case 5: fmtFan(cachedTimerFan[t], v); break;
    case 6: v = tmDaysText(cachedTimerDowMask[t]); break;
    }
}
extern const TmMenu TM_DAYS;
static void tmTimerEditOpen(int i)
{
    int t = tmTimerIdx;
    char title[40];
    switch (i) {
    case 0: {   // a toggle: written right away
        TmBus bus;
        bool on = !cachedTimerEnabled[t];
        if (tmTimerWrite(TIMER_ONOFF_OFFSET, on ? 1 : 0)) tmMsgSet(true, String("PR") + (t + 1) + (on ? " enabled." : " disabled."));
        break;
    }
    case 1: case 2:
        tmTimerOffset = i == 1 ? TIMER_ONTIME_OFFSET : TIMER_OFFTIME_OFFSET;
        snprintf(title, sizeof(title), "PR%d: %s (10 minute steps)", t + 1, i == 1 ? "switch on at" : "switch off at");
        tmPickStart(title, 0, 144, i == 1 ? cachedTimerOnVal[t] : cachedTimerOffVal[t], true, fmtTimerTime, tmTimerPickApply,
                    "Page up / down: 1 hour");
        break;
    case 3:
        tmTimerOffset = TIMER_SETPOINT_OFFSET;
        snprintf(title, sizeof(title), "PR%d: set point", t + 1);
        tmPickStart(title, 7, 30, cachedTimerSetpoint[t], false, fmtC, tmTimerPickApply);
        break;
    case 4:
        tmTimerOffset = TIMER_FLAME_OFFSET;
        snprintf(title, sizeof(title), "PR%d: flame power", t + 1);
        tmPickStart(title, 1, 4, cachedTimerFlame[t], false, fmtNum, tmTimerPickApply);
        break;
    case 5:
        tmTimerOffset = TIMER_FAN_OFFSET;
        snprintf(title, sizeof(title), "PR%d: room fan", t + 1);
        tmPickStart(title, 0, 3, cachedTimerFan[t], false, fmtFan, tmTimerPickApply);
        break;
    case 6:
        tmDays = (byte)(cachedTimerDowMask[t] & 0x7F);
        tmMenu(&TM_DAYS);
        break;
    }
}
static const TmItem TM_TIMER_EDIT_ITEMS[] = {
    { "Enabled",       tmTimerEditValue, tmTimerEditOpen, "Enter: switch the program on / off (written right away)", TI_TOGGLE },
    { "Switch on at",  tmTimerEditValue, tmTimerEditOpen, "Time the stove starts", TI_EDIT },
    { "Switch off at", tmTimerEditValue, tmTimerEditOpen, "Time the stove stops", TI_EDIT },
    { "Set point",     tmTimerEditValue, tmTimerEditOpen, "Room temperature during the program", TI_EDIT },
    { "Flame power",   tmTimerEditValue, tmTimerEditOpen, "Flame power during the program", TI_EDIT },
    { "Room fan",      tmTimerEditValue, tmTimerEditOpen, "Room fan during the program", TI_EDIT },
    { "Days",          tmTimerEditValue, tmTimerEditOpen, "Days of the week the program runs", TI_MENU },
};
extern const TmMenu TM_TIMER_EDIT = { tmTimerTitle, TM_TIMER_EDIT_ITEMS, 7, nullptr, 0 };

static void tmDayValue(int i, String& v) { v = (tmDays & (1 << i)) ? "[x]" : "[ ]"; }
static void tmDayToggle(int i) { tmDays ^= (1 << i); }
static void tmDaysAll(int) { tmDays = tmDays == 0x7F ? 0 : 0x7F; }
static void tmDaysSave(int)
{
    bool ok;
    { TmBus bus; ok = tmTimerWrite(TIMER_DOW_OFFSET, tmDays); }
    if (ok) tmMsgSet(true, String("PR") + (tmTimerIdx + 1) + ": " + tmDaysText(tmDays) + ".");
    tmPop();
}
static void tmDaysCancel(int) { tmMsgSet(true, "Cancelled - nothing written."); tmPop(); }
static const TmItem TM_DAYS_ITEMS[] = {
    { "Monday",        tmDayValue, tmDayToggle, nullptr, TI_TOGGLE },
    { "Tuesday",       tmDayValue, tmDayToggle, nullptr, TI_TOGGLE },
    { "Wednesday",     tmDayValue, tmDayToggle, nullptr, TI_TOGGLE },
    { "Thursday",      tmDayValue, tmDayToggle, nullptr, TI_TOGGLE },
    { "Friday",        tmDayValue, tmDayToggle, nullptr, TI_TOGGLE },
    { "Saturday",      tmDayValue, tmDayToggle, nullptr, TI_TOGGLE },
    { "Sunday",        tmDayValue, tmDayToggle, nullptr, TI_TOGGLE },
    { "All / none",    nullptr,    tmDaysAll,   nullptr, TI_ACT },
    { "Save",          nullptr,    tmDaysSave,  "Write the days to the stove", TI_ACT },
    { "Cancel",        nullptr,    tmDaysCancel, nullptr, TI_ACT },
};
extern const TmMenu TM_DAYS = { "Days of the week", TM_DAYS_ITEMS, 10, nullptr, 0 };

// ─────────────────────────────────────────────────────────────────────
// Pellets: estimate, refill, calibration (pellets.h; no stove writes)
// ─────────────────────────────────────────────────────────────────────

static void tmPelletsInfo()
{
    tmPelletLines(true);
    tmReserveLines();
    {
        double today = pelKgToday(), perDay = pelKgPerDay7(), month = pelCostPerMonth();
        String use = today >= 0 ? String(today, 1) + " kg today" : String("today: not calibrated yet");
        if (perDay >= 0) use += ", " + String(perDay, 1) + " kg per day (7 d)";
        if (month >= 0) use += ", about EUR " + String(month, 0) + " per month";
        tkv("Use", "%s", use.c_str());
    }
    if (pellets.nCycles) {
        tkv("Measured cycles", "%s", "");
        for (int i = pellets.nCycles - 1; i >= 0 && i >= pellets.nCycles - 5; i--) {
            const pc::PelletCycle& c = pellets.cycles[i];
            tpf("      %s  %5.1f kg  %8.0f units  %.4f g/unit" A_EOL, fmtTime(c.t, "%d/%m/%Y").c_str(), c.kg, c.units, c.units > 0 ? c.kg / c.units * 1000.0 : 0.0);
        }
    }
}
static void tmPelletsAdded(const String& s)
{
    float v = s.toFloat();
    if (v <= 0 || v > 100) { tmMsgSet(false, "Must be 0 - 100 kg."); return; }
    if (!pellets.low) { pelCost.take(v); pelCostSave(); }   // while low: taken at the refill itself
    pellets.addKg(v, (uint32_t)time(nullptr));
    pelletsSave();
    LOGI("stove", "pellets: %.1f kg added by hand (%s) by %s", v, pellets.low ? "used for the refill when the sensor clears" : "topped up", tmSource());
    tmMsgSet(true, String(v, 1) + " kg added" + (pellets.low ? " - counted when the level sensor clears." : "."));
}
static void tmPelletsRefillKg(const String& s)
{
    float v = s.toFloat();
    if (v < 1 || v > 100) { tmMsgSet(false, "Must be 1 - 100 kg."); return; }
    pellets.refillKg = v;
    pelletsSave();
    LOGI("stove", "pellets: refill amount set to %.1f kg by %s", v, tmSource());
    tmMsgSet(true, "Refill amount " + String(v, 1) + " kg.");
}
static void tmPelletsResetYes()
{
    pellets.resetCalibration();
    pelletsSave();
    LOGI("stove", "pellets: calibration reset by %s", tmSource());
    tmMsgSet(true, "Calibration reset.");
}
static void tmPelletPrice(const String& s)
{
    String t = s; t.replace(",", ".");
    float v = t.toFloat();
    if (v < 0 || v > 200) { tmMsgSet(false, "Must be 0 - 200 EUR (0 = not kept)."); return; }
    pelCost.pricePerBag = v; pelCostSave();
    LOGI("stove", "pellets: price %.2f per bag set by %s", v, tmSource());
    tmMsgSet(true, v > 0 ? "Price saved." : "Price cleared.");
}
static void tmPelletBagKg(const String& s)
{
    String t = s; t.replace(",", ".");
    float v = t.toFloat();
    if (v < 1 || v > 100) { tmMsgSet(false, "Must be 1 - 100 kg."); return; }
    pelCost.bagKg = v; pelCostSave(); tmMsgSet(true, "Bag weight saved.");
}
static void tmPelletStock(const String& s)
{
    String t = s; t.replace(",", ".");
    if (!t.length() || t == "-" || t == "off") { pelCost.stockBags = -1; pelCostSave(); tmMsgSet(true, "Stock no longer kept."); return; }
    float v = t.toFloat();
    if (v < 0 || v > 10000) { tmMsgSet(false, "Must be 0 - 10000 bags, or empty / '-' to stop keeping it."); return; }
    pelCost.stockBags = v; pelCostSave();
    LOGI("stove", "pellets: stock set to %.1f bag(s) by %s", v, tmSource());
    tmMsgSet(true, "Stock saved; every refill takes from it.");
}
static void tmPelletsOpen(int i)
{
    switch (i) {
    case 3: tmInputStart("Price of a bag (EUR, 0 = not kept)", tmPelletPrice, 8); break;
    case 4: tmInputStart("Weight of a bag (kg)", tmPelletBagKg, 8); break;
    case 5: tmInputStart("Bags in stock (empty or '-' = not kept)", tmPelletStock, 8, "Every refill (detected or entered) takes its kg from the stock"); break;
    case 0: tmInputStart("Pellets added by hand (kg)", tmPelletsAdded, 8,
                         "Topped up without a low pellet alarm, or another amount than the refill amount for the coming refill"); break;
    case 1: tmInputStart("Refill amount (kg)", tmPelletsRefillKg, 8, "Counted when a refill is detected (the level sensor clears after an alarm)"); break;
    case 2: tmConfirmStart("   Forget the measured cycles? The estimate starts again after the next two alarms.", tmPelletsResetYes); break;
    }
}
static void tmPelletsValue(int i, String& v)
{
    switch (i) {
    case 1: v = String(pellets.refillKg, 1) + " kg"; break;
    case 3: v = pelCost.pricePerBag > 0 ? "EUR " + String(pelCost.pricePerBag, 2) : String("-"); break;
    case 4: v = String(pelCost.bagKg, 1) + " kg"; break;
    case 5: v = pelCost.stockBags >= 0 ? String(pelCost.stockBags, 1) + " bags" : String("-"); break;
    }
}
static const TmItem TM_PELLETS_ITEMS[] = {
    { "Pellets added (kg)", nullptr,        tmPelletsOpen, "Enter the kg you added by hand", TI_EDIT },
    { "Refill amount",      tmPelletsValue, tmPelletsOpen, "Kg counted for an automatically detected refill", TI_EDIT },
    { "Reset calibration",  nullptr,        tmPelletsOpen, "Forget the measured cycles (asks for confirmation)", TI_ASK },
    { "Price per bag",      tmPelletsValue, tmPelletsOpen, "For the cost per day / month", TI_EDIT },
    { "Bag weight",         tmPelletsValue, tmPelletsOpen, "Kg in one bag", TI_EDIT },
    { "Bags in stock",      tmPelletsValue, tmPelletsOpen, "Every refill takes its kg from it", TI_EDIT },
};
static const TmMenu TM_PELLETS = { "Pellets", TM_PELLETS_ITEMS, 6, tmPelletsInfo, 5000 };

// ─────────────────────────────────────────────────────────────────────
// Errors, events and history
// ─────────────────────────────────────────────────────────────────────

static void tmErrorsInfo()
{
    tmErrorLines();
    if (!lastSlowStatesAt) { tkv("Error memory", "not read yet"); return; }
    for (int i = 0; i < 5; i++) {
        char l[20]; snprintf(l, sizeof(l), "Memory %d", i + 1);
        if (cachedErrors[i]) tkv(l, "%s - %s", pc::errorCode(cachedErrors[i]), pc::errorDesc(cachedErrors[i]));
        else tkv(l, "-");
    }
}

static void tmEventsRender()
{
    if (!evtCount) { tpl("   No events yet."); return; }
    for (int i = 0; i < evtCount && i < 30; i++) {   // newest first
        const StateEvent& e = evts[(evtHead - 1 - i + 2 * EVT_SIZE) % EVT_SIZE];
        String t = fmtTime(e.t, "%d/%m %H:%M");
        switch (e.kind) {
        case EVT_STATE:
            tpf("   %s  state   %s -> %s" A_EOL, t.c_str(), e.from < 0 ? "?" : pc::stoveStateName(e.from), pc::stoveStateName(e.to));
            break;
        case EVT_ERROR:
            if (e.code) tpf("   %s  " A_RED "error   %s - %s" A_RST A_EOL, t.c_str(), pc::errorCode(e.code), pc::errorDesc(e.code));
            else tpf("   %s  error   cleared" A_EOL, t.c_str());
            break;
        case EVT_PELLET:
            tpf("   %s  pellet  %s %d -> %d" A_EOL, t.c_str(), e.code == palletSensorAddr ? "level sensor (0xA4)" : "reserve counter (0x89)", e.from, e.to);
            break;
        case EVT_LINK:
            tpf("   %s  link    serial link to the stove %s" A_EOL, t.c_str(), e.to ? "back" : "lost");
            break;
#if BOARD_HAS_POWER_SENSE
        case EVT_POWER:
            tpf("   %s  power   %s -> %s" A_EOL, t.c_str(), e.from == 2 ? "USB" : "stove", e.to == 2 ? "USB" : "stove");
            break;
#endif
        }
    }
}
static const TmView TM_EVENTS = { "Event log (newest first)", tmEventsRender, 0, false };

static void tmHistoryRender()
{
    if (!histCount) { tpl("   No history yet (needs NTP time and data from the stove)."); return; }
    tpl(A_DIM "   Time   State             Room  Set  Fumes  Flame  Fan  Auger" A_RST);
    uint32_t lastSlot = 0;
    int rows = 0;
    for (int i = 0; i < histCount && rows < 18; i++) {   // newest first, one row per 10 min
        const HistSample& s = hist[(histHead - 1 - i + 2 * HIST_SIZE) % HIST_SIZE];
        uint32_t slot = s.t / 600;
        if (i && slot == lastSlot) continue;
        lastSlot = slot;
        rows++;
        String fl = String(s.flame) + (s.flameActual >= 0 ? "/" + String(s.flameActual) : String(""));
        String fan = String(s.fan) + (s.fanActual >= 0 ? "/" + String(s.fanActual) : String(""));
        String aug = s.auger10 >= 0 ? String(pc::augerPercent(s.auger10 / 10.0f)) + " %" : String("-");
        tpf("   %s  %-16.16s %5.1f  %3d  %5d  %5s  %3s  %5s" A_EOL, fmtTime(s.t, "%H:%M").c_str(), pc::stoveStateName(s.state),
            s.room10 / 10.0, s.setpoint, s.fume, fl.c_str(), fan.c_str(), aug.c_str());
    }
    tpl(A_DIM "   Flame and fan: set / actual. The web app shows 24 h and 7 days." A_RST);
}
static const TmView TM_HISTORY = { "History (newest first, one row per 10 min)", tmHistoryRender, 0, false };

static void tmClearErrorsYes()
{
    bool ok = true;
    for (int i = 0; i < 5; i++) {
        setError(i, 0);
        int v = getErrorMemory(i);
        if (v >= 0) cachedErrors[i] = v;
        if (v != 0) ok = false;
    }
    tmMsgSet(ok, ok ? "Error memory cleared." : "Not every slot could be cleared (no answer from the stove).");
}
static void tmErrorsOpen(int i)
{
    switch (i) {
    case 0: tmView(&TM_EVENTS); break;
    case 1: tmView(&TM_HISTORY); break;
    case 2: tmConfirmStart("   Clear all 5 slots of the error memory of the stove?", tmClearErrorsYes); break;
    }
}
static const TmItem TM_ERRORS_ITEMS[] = {
    { "Event log",          nullptr, tmErrorsOpen, "State changes, errors, pellet sensor and serial link (kept over restarts)", TI_VIEW },
    { "History",            nullptr, tmErrorsOpen, "The last 3 hours: temperatures, flame, fan, auger", TI_VIEW },
    { "Clear error memory", nullptr, tmErrorsOpen, "Write 0 to the 5 error memory slots (asks for confirmation)", TI_ASK },
};
static const TmMenu TM_ERRORS = { "Errors and history", TM_ERRORS_ITEMS, 3, tmErrorsInfo, 3000 };

// ─────────────────────────────────────────────────────────────────────
// Network: WiFi, IPv4 / IPv6, hostname, IP settings (NVS "net", see wifiInfo.h)
// ─────────────────────────────────────────────────────────────────────

static void tmWifiRender()
{
    bool up = WiFi.status() == WL_CONNECTED;
    tkv("WiFi", "%s", up ? "connected" : "not connected");
    tkv("Network", "%s (access point %s, channel %d)", WiFi.SSID().c_str(), up ? WiFi.BSSIDstr().c_str() : "-", up ? WiFi.channel() : 0);
    int rssi = up ? WiFi.RSSI() : 0;
    tkv("Signal", "%d dBm (%d %%), TX power %.1f dBm", rssi, pc::rssiQuality(rssi), WiFi.getTxPower() * 0.25f);
    tkv("IPv4", "%s / %s, gateway %s, DNS %s", WiFi.localIP().toString().c_str(), WiFi.subnetMask().toString().c_str(),
        WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str());
    String ll = WiFi.localIPv6().toString();
    if (ll.length() && ll != "0000:0000:0000:0000:0000:0000:0000:0000") tkv("IPv6", "%s (link-local)", ll.c_str());
    for (auto& g : ipv6Global) if (g.length()) tkv("IPv6", "%s", g.c_str());
    tkv("Hostname", "%s (%s.local)", WiFi.getHostname(), WiFi.getHostname());
    tkv("MAC", "%s", WiFi.macAddress().c_str());
    tkv("Power save", "%s", WiFi.getSleep() ? "on (modem sleep)" : "off");
    tkv("Drops", "%lu since the start", (unsigned long)wifiDisconnects);
    if (wifiLastDisconnectMs) tkv("Last drop", "%s ago: %s (%u)", fmtAgo((millis() - wifiLastDisconnectMs) / 1000).c_str(),
                                  WiFi.disconnectReasonName((wifi_err_reason_t)wifiLastReason), (unsigned)wifiLastReason);
    tkv("Network watchdog", "%lu repair(s) since the start%s", (unsigned long)netwdRepairs, netwdDownAt ? " - network down now, repairing" : "");
    if (netSwitchMsg.length()) tkv("Last change", "%s", netSwitchMsg.c_str());
    if (netPendingConfirm) tkv("Static IP", "waiting for confirmation, %lu s left", (NET_CONFIRM_MS - (millis() - netPendingSince)) / 1000);
    tkv("Telnet", "port %d, %s", TN_PORT, tnListenText());
    tkv("HTTP over IPv6", "%s", http6Listening ? "listening on port 80" : "not listening");
}
static const TmView TM_WIFI = { "WiFi and IP status", tmWifiRender, 3000, false };

static String tmIp, tmMask, tmGw;   // the static IP wizard

static void tmNetInfo()
{
    String nc = netGet("netcfg", "dhcp");
    tkv("Running", "%s, hostname %s", WiFi.localIP().toString().c_str(), WiFi.getHostname());
    if (nc.startsWith("static,")) tkv("Saved", "static: %s", nc.substring(7).c_str());
    else tkv("Saved", "DHCP");
    tkv("", "hostname %s", netGet("hostname", "openpelletbridge").c_str());
    tpl(A_DIM "   Saved settings take effect after a restart." A_RST);
}

static void tmRestartYes()
{
    tpl(A_RST "\r\n   Restarting the controller - connect again in about 20 s.");
    tnFlush();
    delay(300);
    restartWith("telnet menu");
}

static void tmHostnameDone(const String& s)
{
    if (!pc::validHostname(s.c_str())) { tmMsgSet(false, "Hostname: 1 - 32 letters, digits or '-', not starting or ending with '-'."); return; }
    Preferences p; p.begin("net", false); p.putString("hostname", s); p.end();
    LOGI("wifi", "hostname set to '%s' by %s (after a restart)", s.c_str(), tmSource());
    tmMsgSet(true, "Hostname '" + s + "' saved - takes effect after a restart.");
}

static void tmDhcpYes()
{
    Preferences p; p.begin("net", false); p.putString("netcfg", "dhcp"); p.putBool("netpend", false); p.end();
    LOGI("wifi", "IP settings: DHCP, set by %s (after a restart)", tmSource());
    tmMsgSet(true, "DHCP saved - takes effect after a restart.");
}

static void tmStaticDns(const String& s);
static void tmStaticGw(const String& s);
static void tmStaticMask(const String& s);
static bool tmIpOk(const String& s) { uint32_t x; return pc::ipv4FromStr(s.c_str(), x); }
static void tmStaticIp(const String& s)
{
    tmIp = s.length() ? s : WiFi.localIP().toString();
    if (!tmIpOk(tmIp)) { tmMsgSet(false, "Invalid IP address: " + tmIp); return; }
    tmInputStart(("Netmask [" + WiFi.subnetMask().toString() + "]").c_str(), tmStaticMask, 15, "Enter: keep the value in brackets");
}
static void tmStaticMask(const String& s)
{
    tmMask = s.length() ? s : WiFi.subnetMask().toString();
    if (!tmIpOk(tmMask)) { tmMsgSet(false, "Invalid netmask: " + tmMask); return; }
    tmInputStart(("Gateway [" + WiFi.gatewayIP().toString() + "]").c_str(), tmStaticGw, 15, "Enter: keep the value in brackets");
}
static void tmStaticGw(const String& s)
{
    tmGw = s.length() ? s : WiFi.gatewayIP().toString();
    if (!tmIpOk(tmGw)) { tmMsgSet(false, "Invalid gateway: " + tmGw); return; }
    tmInputStart(("DNS server [" + tmGw + "]").c_str(), tmStaticDns, 15, "Enter: the gateway");
}
static void tmStaticDns(const String& s)
{
    String dns = s.length() ? s : tmGw;
    const char* e = pc::checkStaticIp(tmIp.c_str(), tmMask.c_str(), tmGw.c_str(), dns.c_str());
    if (e) { tmMsgSet(false, String("Not saved: ") + e + "."); return; }
    String cfg = "static," + tmIp + "," + tmGw + "," + tmMask + "," + dns;   // the order setup_wifi() reads
    Preferences p; p.begin("net", false);
    bool changed = p.getString("netcfg", "dhcp") != cfg;
    p.putString("netcfg", cfg);
    if (changed) p.putBool("netpend", true);   // wifiInfo.h: back to DHCP when nobody reaches the new address
    p.end();
    LOGI("wifi", "IP settings: static %s, set by %s (after a restart)", tmIp.c_str(), tmSource());
    tmConfirmStart("   Static IP " + tmIp + " saved.\n\n   Restart now to use it? After the restart, open the web app or telnet to\n   " + tmIp +
                   " within 5 minutes; otherwise the controller goes back to DHCP.", tmRestartYes);
}

static void tmNetOpen(int i)
{
    switch (i) {
    case 0: tmConfirmStart("   Use DHCP (after a restart)?", tmDhcpYes); break;
    case 1: tmInputStart(("IP address [" + WiFi.localIP().toString() + "]").c_str(), tmStaticIp, 15, "Enter: keep the value in brackets"); break;
    case 2: tmConfirmStart("   Restart the controller now?", tmRestartYes); break;
    }
}
static const TmItem TM_IP_ITEMS[] = {
    { "Use DHCP",         nullptr, tmNetOpen, "Address from the router", TI_ACT },
    { "Static IP",        nullptr, tmNetOpen, "IP, netmask, gateway, DNS; checked before it is saved", TI_EDIT },
    { "Restart to apply", nullptr, tmNetOpen, "Restart the controller (asks for confirmation)", TI_ASK },
};
static const TmMenu TM_IP = { "IP settings", TM_IP_ITEMS, 3, tmNetInfo, 0 };

static void tmNetworkOpen(int i)
{
    switch (i) {
    case 0: tmView(&TM_WIFI); break;
    case 1: tmInputStart(("Hostname [" + netGet("hostname", "openpelletbridge") + "]").c_str(), tmHostnameDone, 32,
                         "Letters, digits and '-'; the name in <hostname>.local"); break;
    case 2: tmMenu(&TM_IP); break;
    }
}
static void tmNetworkValue(int i, String& v)
{
    if (i == 0) v = WiFi.status() == WL_CONNECTED ? WiFi.SSID() + ", " + String((int)WiFi.RSSI()) + " dBm" : String("not connected");
    if (i == 1) v = netGet("hostname", "openpelletbridge");
    if (i == 2) v = netGet("netcfg", "dhcp").startsWith("static,") ? "static" : "DHCP";
}
static const TmItem TM_NETWORK_ITEMS[] = {
    { "WiFi and IP status", tmNetworkValue, tmNetworkOpen, "Signal, IPv4 and IPv6 addresses, drops, network watchdog", TI_VIEW },
    { "Hostname",           tmNetworkValue, tmNetworkOpen, "Saved in the controller, used after a restart", TI_EDIT },
    { "IP settings",        tmNetworkValue, tmNetworkOpen, "DHCP or static IP, used after a restart", TI_MENU },
};
static const TmMenu TM_NETWORK = { "Network", TM_NETWORK_ITEMS, 3, nullptr, 0 };

// ─────────────────────────────────────────────────────────────────────
// Device: firmware update, services, system, log levels, restart
// ─────────────────────────────────────────────────────────────────────

static void tmFirmwareInfo()
{
    pc::Version v;
    bool parsed = pc::parseVersion(BUILD_VERSION, v);
    bool dev = !parsed || v.ahead > 0 || v.dirty;
    tkv("This firmware", "%s%s", BUILD_VERSION, dev ? " (development build)" : "");
    tkv("Built", "%s (%s), for a %s", BUILD_TIME, BUILD_ENV, BUILD_STOVE_MODEL);
    if (otaTrial) tkv("Trial", A_YEL "on trial, confirmed in %lu s with WiFi (a restart before that goes back)" A_RST, (unsigned long)otaTrialSecondsLeft());
    if (otaRolledBack.length()) tkv("Rolled back", A_RED "the update to %s did not start properly" A_RST, otaRolledBack.c_str());
    int st = fwState;
    String state = FW_STATE_STR[st];
    if ((st == FW_DOWNLOADING || st == FW_RESTARTING) && fwTotal) state += String(" ") + (unsigned)(fwDone * 100 / fwTotal) + " %";
    tkv("Update", "%s", state.c_str());
    if (st != FW_CHECKING && fwLatest.length()) {
        pc::Version l;
        const char* what = "unknown (this build has no release version)";
        if (parsed && pc::parseVersion(fwLatest.c_str(), l)) {
            int c = pc::compareVersion(l, v);
            what = c > 0 ? "an update" : c == 0 && !v.dirty ? "the same version" : "older than this build";
        }
        tkv("Latest release", "%s, published %s - %s", fwLatest.c_str(), fwPublished.substring(0, 10).c_str(), what);
        if (!fwAssetUrl.length()) tkv("", "this release has no firmware image");
    } else tkv("Latest release", "%s", st == FW_CHECKING ? "checking ..." : "not checked yet");
    if (fwCheckedAt) tkv("Checked", "%s ago", fmtAgo((millis() - fwCheckedAt) / 1000).c_str());
    if (st == FW_ERROR && fwError.length()) tpf(A_RED "   %-18s %s" A_RST A_EOL, "Last error", fwError.c_str());
    if (fwLastInstall.length()) tkv("Last install", "%s", fwLastInstall.c_str());
    if (fwLastAttempt.length()) tkv("Unfinished install", "%s", fwLastAttempt.c_str());
    tkv("Memory", "%u KB free, largest block %u KB (an update needs 45 / 20 KB)", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));
}
static void tmFwInstallYes()
{
    if (!fwStart(true)) tmMsgSet(false, "Could not start: " + (fwError.length() ? fwError : String("an update or check is running")));
    else tmMsgSet(true, "Installing - the controller restarts when the new firmware is in place.");
}
static void tmFwOpen(int i)
{
    switch (i) {
    case 0:
        if (fwBusy || otaActive) { tmMsgSet(false, "An update or check is already running."); return; }
        if (fwStart(false)) tmMsgSet(true, "Checking GitHub ...");
        else tmMsgSet(false, "Could not start: " + fwError);
        break;
    case 1:
        if (fwBusy || otaActive) { tmMsgSet(false, "An update or check is already running."); return; }
        if (!fwAssetUrl.length()) { tmMsgSet(false, "No release with firmware known - check first."); return; }
        if (pc::updateAvailable(BUILD_VERSION, fwLatest.c_str()))
            tmConfirmStart("   Install " + fwLatest + " (now " + BUILD_VERSION + ") and restart?\n   The stove keeps running; the controller is away for about a minute.", tmFwInstallYes);
        else
            tmConfirmStart("   " + fwLatest + " is not newer than " + BUILD_VERSION + ".\n   Install it anyway (reinstall / go back) and restart?", tmFwInstallYes);
        break;
    case 2: {
        fwAutoCheck = !fwAutoCheck;
        Preferences p; p.begin("fwupd", false); p.putBool("auto", fwAutoCheck); p.end();
        LOGI("update", "automatic update check %s by %s", fwAutoCheck ? "on" : "off", tmSource());
        tmMsgSet(true, fwAutoCheck ? "Automatic check on." : "Automatic check off.");
        break;
    }
    }
}
static void tmFwValue(int i, String& v) { if (i == 2) v = fwAutoCheck ? (httpsTlsOn ? "on (at start-up only, HTTPS is on)" : "on (daily)") : "off"; }
static const TmItem TM_FW_ITEMS[] = {
    { "Check for an update",  nullptr,   tmFwOpen, "Ask GitHub for the latest release (takes a few seconds)", TI_ACT },
    { "Install latest",       nullptr,   tmFwOpen, "Download, verify and install, then restart (asks for confirmation)", TI_ASK },
    { "Automatic check",      tmFwValue, tmFwOpen, "Enter: switch on / off", TI_TOGGLE },
};
static const TmMenu TM_FIRMWARE = { "Firmware update (GitHub releases)", TM_FW_ITEMS, 3, tmFirmwareInfo, 1000 };

static void tmServicesRender()
{
    if (mqttCfg.enabled) {
        tkv("MQTT", "%s, broker %s:%u", mqtt.connected() ? "connected" : "not connected", mqttCfg.host.c_str(), mqttCfg.port);
        tkv("", "%lu messages sent, %lu commands%s%s", (unsigned long)mqttPublishes, (unsigned long)mqttCommands,
            mqttLastError.length() ? ", last error: " : "", mqttLastError.c_str());
    } else tkv("MQTT", "off");
    if (httpsEnabled) tkv("HTTPS", "%s, %lu sessions, %lu requests, %lu refused (memory)", httpsListening ? "listening on port 443" : "starting",
                          (unsigned long)httpsStats.sessions, (unsigned long)httpsStats.requests, (unsigned long)httpsStats.refused);
    else tkv("HTTPS", "off");
    tkv("HTTP over IPv6", "%s, %lu connections", http6Listening ? "on" : "not listening", (unsigned long)http6Connections);
    tkv("Bluetooth", "%s", bleEnabled ? (bleIsClientConnected() ? "on, app connected" : "on") : "off");
    tkv("API key", "%s (not used by telnet and Bluetooth)", apiKeyRequired ? "required" : "not required");
    tkv("Telnet", "%s, this session from %s, %lu session(s) since the start, %lu taken over", tnListenText(), tnS.peer,
        (unsigned long)tnSessions, (unsigned long)tnTakeovers);
    if (tnW.fd >= 0) tkv("", "%s waits to take over", tnW.peer);
}
static const TmView TM_SERVICES = { "Services", tmServicesRender, 2000, false };

// ── Services: MQTT, HTTPS, Bluetooth, API key, telnet ─────────────────
// The same setters as the web app (mqttApply, httpsSet*, bleSaveEnabled, apiKey*,
// telnetSetEnabled); nothing here writes to the stove.

// MQTT: every change is applied at once, like Save in the web app
static void tmMqttApply(const MqttConfig& c, const char* what)
{
    String err = mqttApply(c, tmSource());
    if (err.length()) tmMsgSet(false, "Not changed: " + err + ".");
    else tmMsgSet(true, String(what) + " saved; MQTT " + (c.enabled ? "reconnects." : "is off."));
}
static void tmMqttInfo()
{
    if (!mqttCfg.enabled) tkv("Status", "off");
    else tkv("Status", "%s%s%s", mqtt.connected() ? A_GRN "connected" A_RST : A_YEL "not connected" A_RST,
             mqttLastError.length() ? " - last error: " : "", mqttLastError.c_str());
    tkv("Messages", "%lu sent, %lu commands received", (unsigned long)mqttPublishes, (unsigned long)mqttCommands);
    tkv("Topics", "%s/state/..., %s/set/...", mqttCfg.prefix.c_str(), mqttCfg.prefix.c_str());
}
static void tmMqttValue(int i, String& v)
{
    switch (i) {
    case 0: v = mqttCfg.enabled ? "on" : "off"; break;
    case 1: v = mqttCfg.host.length() ? mqttCfg.host : String("-"); break;
    case 2: v = String(mqttCfg.port); break;
    case 3: v = mqttCfg.user.length() ? mqttCfg.user : String("-"); break;
    case 4: v = mqttCfg.pass.length() ? "set" : "-"; break;
    case 5: v = mqttCfg.prefix; break;
    case 6: v = mqttCfg.discovery ? "on" : "off"; break;
    case 7: v = mqttCfg.discoveryPrefix; break;
    }
}
static void tmMqttHost(const String& t)   { MqttConfig c = mqttCfg; c.host = t; tmMqttApply(c, "Broker"); }
static void tmMqttPort(const String& t)
{
    int p = 0;
    if (!pc::parseIntRange(t.c_str(), 1, 65535, p)) { tmMsgSet(false, "Port must be 1 - 65535."); return; }
    MqttConfig c = mqttCfg; c.port = p; tmMqttApply(c, "Port");
}
static void tmMqttUser(const String& t)   { MqttConfig c = mqttCfg; c.user = t; tmMqttApply(c, "User"); }
static void tmMqttPass(const String& t)
{
    if (!t.length()) { tmMsgSet(true, "Password unchanged."); return; }
    MqttConfig c = mqttCfg; c.pass = t == "-" ? String("") : t; tmMqttApply(c, t == "-" ? "Empty password" : "Password");
}
static void tmMqttPrefix(const String& t) { MqttConfig c = mqttCfg; c.prefix = t; while (c.prefix.endsWith("/")) c.prefix.remove(c.prefix.length() - 1); tmMqttApply(c, "Topic prefix"); }
static void tmMqttDiscPre(const String& t) { MqttConfig c = mqttCfg; c.discoveryPrefix = t; tmMqttApply(c, "Discovery prefix"); }
static void tmMqttOpen(int i)
{
    MqttConfig c = mqttCfg;
    switch (i) {
    case 0: c.enabled = !c.enabled; tmMqttApply(c, "MQTT"); break;
    case 1: tmInputStart("MQTT broker (host name or IP)", tmMqttHost, 63); break;
    case 2: tmInputStart("MQTT port", tmMqttPort, 5, "Usually 1883"); break;
    case 3: tmInputStart("MQTT user (empty = none)", tmMqttUser, 63); break;
    case 4: tmInputSecretStart("MQTT password", tmMqttPass, 63, "Empty = unchanged, '-' = no password. Telnet is not encrypted."); break;
    case 5: tmInputStart("Topic prefix", tmMqttPrefix, 63, "e.g. openpelletbridge; no # or +"); break;
    case 6: c.discovery = !c.discovery; tmMqttApply(c, "Home Assistant discovery"); break;
    case 7: tmInputStart("Discovery prefix", tmMqttDiscPre, 63, "Home Assistant uses homeassistant"); break;
    }
}
static const TmItem TM_MQTT_ITEMS[] = {
    { "MQTT",                     tmMqttValue, tmMqttOpen, "Enter: switch on / off", TI_TOGGLE },
    { "Broker",                   tmMqttValue, tmMqttOpen, "Host name or IP address of the MQTT broker", TI_EDIT },
    { "Port",                     tmMqttValue, tmMqttOpen, "TCP port of the broker", TI_EDIT },
    { "User",                     tmMqttValue, tmMqttOpen, "Empty = no login", TI_EDIT },
    { "Password",                 tmMqttValue, tmMqttOpen, "Typed as *; telnet itself is not encrypted", TI_EDIT },
    { "Topic prefix",             tmMqttValue, tmMqttOpen, "<prefix>/state/..., <prefix>/set/...", TI_EDIT },
    { "Home Assistant discovery", tmMqttValue, tmMqttOpen, "Enter: switch on / off", TI_TOGGLE },
    { "Discovery prefix",         tmMqttValue, tmMqttOpen, "Topic root Home Assistant listens on", TI_EDIT },
};
static const TmMenu TM_MQTT = { "MQTT", TM_MQTT_ITEMS, 8, tmMqttInfo, 3000 };

// HTTPS
static void tmHttpsInfo()
{
    if (!httpsEnabled) tkv("Status", "off");
    else tkv("Status", "%s, %lu sessions, %lu requests, %lu refused (memory)", httpsListening ? A_GRN "listening on port 443" A_RST : A_YEL "starting" A_RST,
             (unsigned long)httpsStats.sessions, (unsigned long)httpsStats.requests, (unsigned long)httpsStats.refused);
    if (httpsCrtLen) {
        tkv("Certificate for", "%s", httpsSan.c_str());
        tkv("SHA-256", "%s", httpsFingerprint().c_str());
    }
    if (httpsMsg.length()) tkv("Last problem", "%s", httpsMsg.c_str());
    tpl(A_DIM "   HTTPS needs about 34 KB of memory per session; off is the safer choice on this board." A_RST);
}
static void tmHttpsValue(int i, String& v)
{
    switch (i) {
    case 0: v = httpsEnabled ? "on" : "off"; break;
    case 1: v = httpsRedirect ? "on" : "off"; break;
    case 2: v = httpsAlt.length() ? httpsAlt : String("-"); break;
    }
}
static void tmHttpsAlt(const String& t)
{
    String err = httpsSetAltNames(t, tmSource());
    if (err.length()) tmMsgSet(false, "Not changed: " + err + ".");
    else tmMsgSet(true, "Extra names saved; the certificate is made again (the browser warns once more).");
}
static void tmHttpsNewYes() { httpsRequestNewCert(tmSource()); tmMsgSet(true, "A new key and certificate are being made (a few seconds)."); }
static void tmHttpsOpen(int i)
{
    switch (i) {
    case 0: httpsSetEnabled(!httpsEnabled, tmSource()); tmMsgSet(true, httpsEnabled ? "HTTPS on." : "HTTPS off: its memory is released."); break;
    case 1: httpsSetRedirect(!httpsRedirect, tmSource()); tmMsgSet(true, httpsRedirect ? "Web app pages on HTTP now go to HTTPS." : "No redirect to HTTPS."); break;
    case 2: tmInputStart("Extra names in the certificate", tmHttpsAlt, 120, "Comma-separated host names / IPv4 addresses, empty = none (hostname and IP are always in)"); break;
    case 3: tmConfirmStart("   Make a new key and certificate? Browsers warn again and a saved\n   certificate (trusted on a device) no longer matches.", tmHttpsNewYes); break;
    }
}
static const TmItem TM_HTTPS_ITEMS[] = {
    { "HTTPS",                    tmHttpsValue, tmHttpsOpen, "Enter: switch on / off (port 443, self-signed certificate)", TI_TOGGLE },
    { "Redirect HTTP to HTTPS",   tmHttpsValue, tmHttpsOpen, "Enter: switch on / off (web app pages only, the API stays on HTTP)", TI_TOGGLE },
    { "Extra names",              tmHttpsValue, tmHttpsOpen, "Extra host names / IPs in the certificate", TI_EDIT },
    { "New key and certificate",  nullptr,      tmHttpsOpen, "Asks for confirmation", TI_ASK },
};
static const TmMenu TM_HTTPS = { "HTTPS", TM_HTTPS_ITEMS, 4, tmHttpsInfo, 3000 };

// API key
static void tmKeyInfo()
{
    tkv("Status", "%s", apiKeyRequired ? "required for the web app and the API" : "not required");
    tkv("Also", "the OTA upload password");
    tpl(A_DIM "   Telnet and Bluetooth do not use the API key." A_RST);
}
static void tmKeyValue(int i, String& v) { if (i == 0) v = apiKeyRequired ? "required" : "off"; }
static void tmKeyShowYes() { tmShowText("API key", "   " + apiKey + "\n\n   Header: X-API-Key: <key>\n"); }
static void tmKeyRotateYes() { apiKeyRotate(tmSource()); tmMsgSet(true, "New API key made; open 'Show key' to read it."); }
static void tmKeyOpen(int i)
{
    switch (i) {
    case 0: apiKeySetRequired(!apiKeyRequired, tmSource());
            tmMsgSet(true, apiKeyRequired ? "API key required: the web app asks for it once per browser." : "API key no longer required.");
            break;
    case 1: tmConfirmStart("   Show the API key? Telnet is not encrypted: anyone on the network\n   who listens can read it.", tmKeyShowYes); break;
    case 2: tmConfirmStart("   Make a new API key? The web app, Home Assistant / scripts and\n   OTA uploads need the new key afterwards.", tmKeyRotateYes); break;
    }
}
static const TmItem TM_KEY_ITEMS[] = {
    { "Key required",  tmKeyValue, tmKeyOpen, "Enter: switch on / off", TI_TOGGLE },
    { "Show key",      nullptr,    tmKeyOpen, "Asks first: telnet is not encrypted", TI_ASK },
    { "New key",       nullptr,    tmKeyOpen, "Asks for confirmation", TI_ASK },
};
static const TmMenu TM_APIKEY = { "API key", TM_KEY_ITEMS, 3, tmKeyInfo, 0 };

// Services menu
static bool tmTelnetOffPending = false;   // switched off from telnetLoop, not inside this session's key handler
static void tmTelnetOffYes() { tmTelnetOffPending = true; }
static void tmSvcValue(int i, String& v)
{
    switch (i) {
    case 0: v = mqttCfg.enabled ? (mqtt.connected() ? "on, connected" : "on, not connected") : "off"; break;
    case 1: v = httpsEnabled ? (httpsListening ? "on" : "on, starting") : "off"; break;
    case 2: { bool saved = bleSavedEnabled();
              v = String(saved ? "on" : "off") + (saved != bleEnabled ? String(" (after a restart; now ") + (bleEnabled ? "on)" : "off)") : String(""));
              break; }
    case 3: v = apiKeyRequired ? "required" : "off"; break;
    case 4: v = "on"; break;
    }
}
static void tmSvcOpen(int i)
{
    switch (i) {
    case 0: tmMenu(&TM_MQTT); break;
    case 1: tmMenu(&TM_HTTPS); break;
    case 2: { bool on = !bleSavedEnabled(); bleSaveEnabled(on, tmSource());
              tmMsgSet(true, String("Bluetooth ") + (on ? "on" : "off") + " saved; it applies after a restart (Device > Restart controller).");
              break; }
    case 3: tmMenu(&TM_APIKEY); break;
    case 4: tmConfirmStart("   Switch telnet off? This session ends. Switch it on again in the web app\n   (Device card) or with POST /api/telnet?enabled=true.", tmTelnetOffYes); break;
    case 5: tmView(&TM_SERVICES); break;
    }
}
static const TmItem TM_SVC_ITEMS[] = {
    { "MQTT",        tmSvcValue, tmSvcOpen, "Broker, login, topics, Home Assistant discovery", TI_MENU },
    { "HTTPS",       tmSvcValue, tmSvcOpen, "On / off, redirect, certificate", TI_MENU },
    { "Bluetooth",   tmSvcValue, tmSvcOpen, "Enter: switch on / off (applies after a restart)", TI_TOGGLE },
    { "API key",     tmSvcValue, tmSvcOpen, "Required or not, show, make a new one", TI_MENU },
    { "Telnet",      tmSvcValue, tmSvcOpen, "Switch this menu off (asks first)", TI_ASK },
    { "Overview",    nullptr,    tmSvcOpen, "All services with their counters, refreshes by itself", TI_VIEW },
};
static const TmMenu TM_SVC = { "Services", TM_SVC_ITEMS, 6, nullptr, 3000 };

static void tmSystemRender()
{
    tkv("Uptime", "%s", uptimeStr().c_str());
    tkv("Started after", "%s", diagResetText().c_str());
    tkv("Starts", "%lu, %lu after a crash / watchdog / brown-out", (unsigned long)diagBoots, (unsigned long)diagCrashes);
    if (coreDumpText.length()) tkv("Crash dump", "present (web app or /api/coredump)");
    tkv("Memory", "%u KB free, lowest %u KB, largest block %u KB", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024),
        (unsigned)(ESP.getMaxAllocHeap() / 1024));
    tkv("Controller", "%s (%s), built %s", BUILD_VERSION, BUILD_BRANCH, BUILD_TIME);
#ifdef BOARD_HW_REV
    tkv("Board", "%s, hardware rev. %s", BOARD_NAME, BOARD_HW_REV);
#else
    tkv("Board", "%s", BOARD_NAME);
#endif
#if BOARD_HAS_POWER_SENSE
    tkv("Power", "%s, %lu switchover(s) since the start", powerSrc == PWR_STOVE ? "stove supply" : powerSrc == PWR_USB ? "USB" : "unknown",
        (unsigned long)powerSwitches);
#endif
#if BOARD_HAS_CHIP_TEMP
    if (!isnan(chipTempC)) tkv("Chip temp.", "%s%.1f °C" A_RST, chipTempHigh ? A_YEL : "", chipTempC);
#endif
    tkv("Stove", "%s, firmware %s, serial number %ld", BUILD_STOVE_MODEL, cachedFirmwareVersion, cachedSerialNumber);
    tkv("Stove bus", "%s, %lu transactions, %lu retries, %lu read / %lu write errors, %lu skipped", stove.linkDown ? "link DOWN" : "link up",
        (unsigned long)stove.transactions, (unsigned long)stove.retries, (unsigned long)stove.readErrors, (unsigned long)stove.writeErrors,
        (unsigned long)stove.skipped);
    if (spiffsMounted) tkv("SPIFFS", "%u of %u KB used", (unsigned)(SPIFFS.usedBytes() / 1024), (unsigned)(SPIFFS.totalBytes() / 1024));
    tkv("History", "%d samples (24 h), %d hours (7 days), %d events", histCount, weekCount, evtCount);
}
static const TmView TM_SYSTEM = { "System", tmSystemRender, 2000, false };

// Log levels per module (liveLog.h), the items are made from the module table
static TmItem tmLogItems[LOG_MODULES];
static TmMenu tmLogMenu = { "Log levels (kept over restarts)", tmLogItems, 0, nullptr, 0 };
static int    tmLogIdx = 0;
static void fmtLevel(int v, String& o) { o = LOG_LEVEL_NAMES[v]; }
static bool tmSetLevel(int v)
{
    logModules[tmLogIdx].level = (uint8_t)v;
    logSaveLevel(logModules[tmLogIdx].tag, (uint8_t)v);
    LOGI("sys", "log level %s = %s (by %s)", logModules[tmLogIdx].tag, LOG_LEVEL_NAMES[v], tmSource());
    return true;
}
static void tmLogValue(int i, String& v) { v = LOG_LEVEL_NAMES[logModules[i].level]; }
static void tmLogOpen(int i)
{
    tmLogIdx = i;
    char t[40]; snprintf(t, sizeof(t), "Log level of '%s'", logModules[i].tag);
    tmPickStart(t, LOG_OFF, LOG_DEBUG, logModules[i].level, false, fmtLevel, tmSetLevel, logModules[i].what);
}
static void tmLogMenuOpen()
{
    for (int i = 0; i < LOG_MODULES; i++) tmLogItems[i] = { logModules[i].tag, tmLogValue, tmLogOpen, logModules[i].what, TI_EDIT };
    tmLogMenu.n = LOG_MODULES;
    tmMenu(&tmLogMenu);
}

static void tmDeviceOpen(int i)
{
    switch (i) {
    case 0: tmMenu(&TM_FIRMWARE); break;
    case 1: tmMenu(&TM_SVC); break;
    case 2: tmView(&TM_SYSTEM); break;
    case 3: tmLogMenuOpen(); break;
    case 4: tmConfirmStart("   Restart the controller now? (The stove keeps running.)", tmRestartYes); break;
    }
}
static void tmDeviceValue(int i, String& v)
{
    if (i == 0) {
        v = BUILD_VERSION;
        if (fwState == FW_CHECKING || fwState == FW_DOWNLOADING) v += String(" - ") + FW_STATE_STR[fwState];
        else if (fwAssetUrl.length() && pc::updateAvailable(BUILD_VERSION, fwLatest.c_str())) v += A_CYN " - " + fwLatest + " available" A_RST;
    }
    if (i == 2) v = "up " + uptimeStr();
}
static const TmItem TM_DEVICE_ITEMS[] = {
    { "Firmware update",     tmDeviceValue, tmDeviceOpen, "Check GitHub for a newer release and install it", TI_MENU },
    { "Services",            tmDeviceValue, tmDeviceOpen, "MQTT, HTTPS, Bluetooth, API key, telnet", TI_MENU },
    { "System",              tmDeviceValue, tmDeviceOpen, "Uptime, restarts, memory, stove bus counters", TI_VIEW },
    { "Log levels",          tmDeviceValue, tmDeviceOpen, "How much each module writes to the firmware log", TI_MENU },
    { "Restart controller",  tmDeviceValue, tmDeviceOpen, "Restart the ESP32 (asks for confirmation)", TI_ASK },
};
static const TmMenu TM_DEVICE = { "Device", TM_DEVICE_ITEMS, 5, nullptr, 0 };

// ─────────────────────────────────────────────────────────────────────
// Debug: firmware log, bus debug, raw register access
// ─────────────────────────────────────────────────────────────────────

static uint32_t tmLogCursor = 0;
static bool     tmLogFirst = true;
static void tmLogTick()
{
    if (!logRing) { tpl("Live log not available (memory)."); return; }
    char piece[256];
    for (int i = 0; i < 8; i++) {   // at most 2 KB per tick
        uint32_t from; bool lost; size_t n;
        portENTER_CRITICAL(&logMux);
        n = logRing->read(tmLogCursor, piece, sizeof(piece), from, lost);
        portEXIT_CRITICAL(&logMux);
        if (lost) tpl(A_YEL "... (older lines overwritten)" A_RST);
        if (!n) break;
        size_t skip = 0;
        if (tmLogFirst) {   // start at a line start
            while (skip < n && piece[skip] != '\n') skip++;
            if (skip < n) skip++;
            if (skip >= n) skip = 0;
            tmLogFirst = false;
        }
        tpText(piece + skip, n - skip);
        tmLogCursor = from + n;
    }
}
static const TmView TM_LOG = { "Firmware log (follows new lines)", tmLogTick, 300, true };

static void tmDebugTextOf(String& out)   // the bus debug buffer, collected by the Stove library
{
    out = stoveDebugBuffer.length() ? stoveDebugBuffer : String("(empty - switch the bus frame debug on, or do a raw read)\n");
}

static uint8_t tmRaw[12];   // raw write: up to 4 x (type, address, value)
static int     tmRawN = 0;

// "00 1a", "ram 1a", "eeprom 7d" -> type, address
static bool tmParseRead(const String& s, unsigned& type, unsigned& addr)
{
    char t[12] = ""; char a[8] = "";
    if (sscanf(s.c_str(), "%11s %7s", t, a) != 2) return false;
    if (!strcasecmp(t, "ram")) type = ram;
    else if (!strcasecmp(t, "eeprom") || !strcasecmp(t, "ee")) type = eeprom;
    else type = strtoul(t, nullptr, 16);
    char* end = nullptr;
    addr = strtoul(a, &end, 16);
    return end && !*end && addr <= 0xFF && (type == ram || type == eeprom);
}
static String tmRegText(byte type, byte addr, int v)
{
    String t = type == eeprom ? getEepromValueTxt(addr, v) : getRamValueTxt(addr, v);
    return t.length() ? " (" + t + ")" : String("");
}
static void tmRawReadDone(const String& s)
{
    unsigned type = 0, addr = 0;
    if (!tmParseRead(s, type, addr)) { tmMsgSet(false, "Use hex: type address, e.g. '00 1a' or 'eeprom 7d' (type 00 = RAM, 20 = EEPROM)."); return; }
    bool prev = stove.isDebugEnabled();
    stove.setDebug(true);
    stoveDebugBuffer = "";
    int v = stove.read((byte)type, (byte)addr);
    stove.setDebug(prev);
    char head[64];
    snprintf(head, sizeof(head), "%s 0x%02X: ", type == eeprom ? "EEPROM" : "RAM", addr);
    String out = stoveDebugBuffer + "\n" + head;
    if (v >= 0) { char r[24]; snprintf(r, sizeof(r), "0x%02X = %d", v, v); out += r; out += tmRegText(type, addr, v); }
    else out += "FAILED (no answer)";
    tmShowText("Raw read", out + "\n");
}

static void tmRawWriteYes()
{
    bool prev = stove.isDebugEnabled();
    stove.setDebug(true);
    stoveDebugBuffer = "";
    bool ok = true;
    unsigned long t0 = millis(), at[4];
    for (int i = 0; i < tmRawN; i++) {   // back to back (the bus lock is held): for tests where the timing matters
        at[i] = millis() - t0;
        if (!stove.write(tmRaw[i * 3], tmRaw[i * 3 + 1], tmRaw[i * 3 + 2])) ok = false;
    }
    stove.setDebug(prev);
    String out = stoveDebugBuffer;
    if (tmRawN > 1) { out += "Sent at (ms):"; for (int i = 0; i < tmRawN; i++) out += " " + String(at[i]); out += "\n"; }
    out += ok ? "Result: SUCCESS\n" : "Result: FAILED\n";
    tmShowText("Raw write", out);
}
static void tmRawWriteDone(const String& in)
{
    // One or more writes "type addr val", e.g. '00 21 01' or '00 24 07 00 08 02'
    unsigned v[12]; int n = 0;
    String rest = in;
    while (rest.length() && n < 12) {
        int sp = rest.indexOf(' ');
        String tok = sp < 0 ? rest : rest.substring(0, sp);
        rest = sp < 0 ? String("") : rest.substring(sp + 1); rest.trim();
        char* end = nullptr;
        unsigned long x = strtoul(tok.c_str(), &end, 16);
        if (!tok.length() || (end && *end) || x > 0xFF) { n = -1; break; }
        v[n++] = (unsigned)x;
    }
    bool typesOk = n > 0;
    for (int i = 0; i + 2 < n; i += 3) if (v[i] != ram && v[i] != eeprom) typesOk = false;
    if (n <= 0 || n % 3 || rest.length() || !typesOk) {
        tmMsgSet(false, "Use hex: type address value, e.g. '00 21 01'; up to 4 writes ('00 24 07 00 08 02').");
        return;
    }
    tmRawN = n / 3;
    String q = "   Write to the stove:\n";
    for (int i = 0; i < tmRawN; i++) {
        tmRaw[i * 3] = v[i * 3]; tmRaw[i * 3 + 1] = v[i * 3 + 1]; tmRaw[i * 3 + 2] = v[i * 3 + 2];
        char l[48];
        snprintf(l, sizeof(l), "     %s 0x%02X = 0x%02X (%u)", v[i * 3] == eeprom ? "EEPROM" : "RAM", v[i * 3 + 1], v[i * 3 + 2], v[i * 3 + 2]);
        q += l + tmRegText(v[i * 3], v[i * 3 + 1], v[i * 3 + 2]) + "\n";
    }
    q += "   A wrong value can upset the stove.";
    tmConfirmStart(q, tmRawWriteYes);
}

static void tmDebugValue(int i, String& v) { if (i == 1) v = stove.isDebugEnabled() ? "on" : "off"; }
static void tmDebugOpen(int i)
{
    switch (i) {
    case 0: tmLogCursor = logRing && logRing->total > 2048 ? logRing->total - 2048 : 0; tmLogFirst = tmLogCursor != 0; tmView(&TM_LOG); break;
    case 1:
        stove.setDebug(!stove.isDebugEnabled());
        LOGI("sys", "bus frame debug %s by %s", stove.isDebugEnabled() ? "on" : "off", tmSource());
        tmMsgSet(true, stove.isDebugEnabled() ? "Bus frame debug on: every frame goes to the bus debug buffer (and the log at bus=debug)." : "Bus frame debug off.");
        break;
    case 2: { String t; tmDebugTextOf(t); tmShowText("Bus debug buffer", t); break; }
    case 3: stoveDebugBuffer = ""; tmMsgSet(true, "Bus debug buffer cleared."); break;
    case 4: tmInputStart("Raw read: type address (hex)", tmRawReadDone, 20, "e.g. '00 1a' (RAM 0x1A) or '20 7d' / 'eeprom 7d' (EEPROM 0x7D)"); break;
    case 5: tmInputStart("Raw write: type address value (hex)", tmRawWriteDone, 40, "e.g. '00 21 01'; up to 4 writes sent back to back; asks for confirmation"); break;
    }
}
static const TmItem TM_DEBUG_ITEMS[] = {
    { "Firmware log",      nullptr,      tmDebugOpen, "The log of the controller, live (levels: Device > Log levels)", TI_VIEW },
    { "Bus frame debug",   tmDebugValue, tmDebugOpen, "Keep every stove bus frame in the bus debug buffer", TI_TOGGLE },
    { "Bus debug buffer",  nullptr,      tmDebugOpen, "The last frames (about 3 KB)", TI_VIEW },
    { "Clear the buffer",  nullptr,      tmDebugOpen, nullptr, TI_ACT },
    { "Raw read",          nullptr,      tmDebugOpen, "Read one register and show the frames", TI_EDIT },
    { "Raw write",         nullptr,      tmDebugOpen, "Write registers directly (asks for confirmation)", TI_ASK },
};
static const TmMenu TM_DEBUG = { "Debug", TM_DEBUG_ITEMS, 6, nullptr, 0 };

// ─────────────────────────────────────────────────────────────────────
// Main menu, help, session
// ─────────────────────────────────────────────────────────────────────

static void tmMainOpen(int i)
{
    switch (i) {
    case 0:  tmView(&TM_STATUS); break;
    case 1:  tmView(&TM_CHART); break;
    case 2:  tmMenu(&TM_LIVE_MENU, TM_LIVE_N + 1); break;   // cursor on Start
    case 3:  tmMenu(&TM_CONTROL); break;
    case 4:  tmMenu(&TM_TIMERS); break;
    case 5:  tmMenu(&TM_PELLETS); break;
    case 6:  tmMenu(&TM_ERRORS); break;
    case 7:  tmMenu(&TM_NETWORK); break;
    case 8:  tmMenu(&TM_DEVICE); break;
    case 9:  tmMenu(&TM_DEBUG); break;
    case 10: tmExit(); break;
    }
}
static void tmMainValue(int i, String& v)
{
    switch (i) {
    case 5: { double r = pellets.remainingKg(); if (r >= 0) v = "~" + String(r, 1) + " kg until the alarm"; break; }
    case 6: if (confirmedCurrentError) v = pc::errorCode(confirmedCurrentError);
            else if (lastSlowStatesAt && pelletsEmptyOff()) v = "PELLETS";
            break;
    case 8: if (fwAssetUrl.length() && pc::updateAvailable(BUILD_VERSION, fwLatest.c_str())) v = "update " + fwLatest + " available"; break;
    }
}
static const TmItem TM_MAIN_ITEMS[] = {
    { "Status",             tmMainValue, tmMainOpen, "Everything at a glance, refreshes by itself", TI_VIEW },
    { "Chart",              tmMainValue, tmMainOpen, "History as a line chart: temperatures, fumes, auger, flame, fume fan (3 h - 7 days)", TI_VIEW },
    { "Live values",        tmMainValue, tmMainOpen, "Chosen values printed every 2 s (scrolls)", TI_MENU },
    { "Control",            tmMainValue, tmMainOpen, "Power, set point, flame, fan, energy saving, burn settings, clock", TI_MENU },
    { "Timers",             tmMainValue, tmMainOpen, "Week programs PR1 - PR6", TI_MENU },
    { "Pellets",            tmMainValue, tmMainOpen, "Estimate until the low pellet alarm, pellet reserve, refill, calibration", TI_MENU },
    { "Errors and history", tmMainValue, tmMainOpen, "Current error, error memory, event log, last 3 hours", TI_MENU },
    { "Network",            tmMainValue, tmMainOpen, "WiFi, IPv4 / IPv6, hostname, IP settings", TI_MENU },
    { "Device",             tmMainValue, tmMainOpen, "Firmware update, services, system, log levels, restart", TI_MENU },
    { "Debug",              tmMainValue, tmMainOpen, "Firmware log, stove bus frames, raw register read / write", TI_MENU },
    { "Exit",               tmMainValue, tmMainOpen, "Close the session (q)", TI_ACT },
};
static const TmMenu TM_MAIN = { "Main menu", TM_MAIN_ITEMS, sizeof(TM_MAIN_ITEMS) / sizeof(TM_MAIN_ITEMS[0]), nullptr, 5000 };

static void tmHelpRender()
{
    tpl("   Keys");
    tpl("     up / down, tab         move");
    tpl("     enter, right, space    open / select");
    tpl("     1 - 9, a - h           open an item directly");
    tpl("     esc, left, backspace   back");
    tpl("     q                      back; in the main menu: exit");
    tpl("     r                      redraw");
    tpl("     ?                      this help");
    tpl();
    tpl("   Values: up / down (or left / right) change, enter writes, esc cancels.");
    tpl("   The stove is only written after enter in a value, y in a question, or");
    tpl("   enter on a toggle (timer enabled). Every write is logged with this session");
    tpl("   as its source. Switching on is refused while the pellets are used up.");
    tpl();
    tpl("   One session at a time. Who connects while the menu is in use can take the");
    tpl("   session over. A session without a key for 30 min is closed.");
    tpl("   Telnet is not encrypted and not protected by the API key; switch it off");
    tpl("   in the web app (Device card) or with POST /api/telnet?enabled=false.");
}
static const TmView TM_HELP = { "Help", tmHelpRender, 0, false };
static void tmHelp() { tmView(&TM_HELP); }

static void tmExit()
{
    tp(A_RST "\r\n   Goodbye.\r\n");
    tnEndSession("exit");
}

// ── Called by the server (telnetServer.h) ─────────────────────────────
static void tmStart()
{
    tmDepth = 0;
    tmMsg = "";
    tmPush(PK_MENU, &TM_MAIN);
}
static void tmEnd()
{
    tmDepth = 0;
    tmText = "";
    tmInput.buf = "";
    tmConfirm.question = "";
}
static void tmKey(int k)
{
    if (!tmDepth) return;
    TmPage& p = tmTop();
    switch (p.kind) {
    case PK_MENU:    tmMsg = ""; tmKeyMenu(p, k); break;
    case PK_PICK:    tmKeyPick(k); break;
    case PK_INPUT:   tmKeyInput(k); break;
    case PK_CONFIRM: tmKeyConfirm(k); break;
    case PK_VIEW:    tmKeyView((const TmView*)p.def, k); break;
    }
}
static void tmTick()
{
    if (!tmDepth) return;
    TmPage& p = tmTop();
    uint16_t every = 0;
    if (p.kind == PK_MENU) every = ((const TmMenu*)p.def)->refreshMs;
    else if (p.kind == PK_VIEW) every = ((const TmView*)p.def)->refreshMs;
    if (!every || millis() - tmLastDraw < every) return;
    if (p.kind == PK_VIEW && ((const TmView*)p.def)->stream) { ((const TmView*)p.def)->render(); tmLastDraw = millis(); }
    else tmDraw();
}

// ─────────────────────────────────────────────────────────────────────
// Public interface (main.cpp, routesDevice.h)
// ─────────────────────────────────────────────────────────────────────

// Start listening (again); the socket opens as soon as WiFi is up
void setupTelnet()
{
    tnRunning = true;
    tnRetryAt = 0;
}
// Close the session and stop listening (POST /api/telnet?enabled=false)
void telnetStop() { tnStop(); }
// From loop()
void telnetLoop()
{
    if (tmTelnetOffPending) { tmTelnetOffPending = false; LOGI("sys", "telnet switched off from the telnet menu"); telnetSetEnabled(false); return; }
    tnLoop();
}
