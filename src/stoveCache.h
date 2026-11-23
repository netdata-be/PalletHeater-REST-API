#pragma once
// Reading the stove into the cache: NTP time, register reads, the fast / slow
// refresh and the sensor task that runs it on the second core.
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

bool getNTPtime(int sec)
{
    {
        uint32_t start = millis();
        struct tm ntp_tm = {0};
        do
        {
            time(&now);
            localtime_r(&now, &ntp_tm);
            delay(10);
        } while (((millis() - start) <= (1000 * sec)) && (ntp_tm.tm_year < (2016 - 1900)));
        if (ntp_tm.tm_year <= (2016 - 1900))
            return false; // the NTP call was not successful

        currentDst = ntp_tm.tm_isdst;
        {
            char time_output[30];
            strftime(time_output, sizeof(time_output), "%a %d-%m-%y %T", localtime(&now));
            static int lastDst = -1;
            if (currentDst != lastDst) LOGI("sys", "clock: %s, daylight saving time %s", time_output, currentDst ? "on" : "off");
            else LOGD("sys", "clock synced: %s", time_output);
            lastDst = currentDst;
        }
    }
    return true;
}



// Read a register into a cached value. On a serial error the previous value is
// kept, so a failed read never shows up as -1 / 255 in the API, BLE or metrics.
template <typename T>
static bool readKeep(T& dst, byte type, byte addr, float scale = 1.0f)
{
    int v = stove.read(type, addr);
    if (v < 0) return false;
    dst = (T)(v * scale);
    return true;
}

// millis() of the last completed sensor refresh, exposed in /api/state
// lastFastStatesAt / lastSlowStatesAt: constants.h (also used by metrics.h, included earlier)

void getAmbTemp() // Get room temperature
{
    if (!readKeep(ambTemp, ram, ambTempAddr, 0.5f)) LOGW("stove", "room temperature (RAM 0x%02X) could not be read - keeping %.1f °C", ambTempAddr, ambTemp);
}

void readFirmwareVersion(char* out, size_t len)
{
    int variant0 = stove.read(eeprom, fwVariantChar0Addr);
    int variant1 = stove.read(eeprom, fwVariantChar1Addr);
    int product  = stove.read(eeprom, fwProductAddr);
    int major    = stove.read(eeprom, fwMajorAddr);
    int minor    = stove.read(eeprom, fwMinorAddr);
    int patch    = stove.read(eeprom, fwPatchAddr);
    if (variant0 < 0 || variant1 < 0 || product < 0 || major < 0 || minor < 0 || patch < 0)
        return;  // keep the previous version string

    snprintf(out, len, "FW-%c%c-%02d-V-%02d-%02d-%02d",
             (char)variant0, (char)variant1, product, major, minor, patch);
}

// Serial number (MATR on the nameplate). Fixed, so read once until it succeeds.
void readSerialNumber()
{
    if (cachedSerialNumber >= 0) return;
    int b0 = stove.read(eeprom, serialNumberAddr);
    int b1 = stove.read(eeprom, serialNumberAddr + 1);
    int b2 = stove.read(eeprom, serialNumberAddr + 2);
    if (b0 < 0 || b1 < 0 || b2 < 0) return;
    cachedSerialNumber = ((long)b0 << 16) | (b1 << 8) | b2;
}

int getFumeFanRPM()
{
    return pc::fumeFanRpm(stove.read(ram, fumeFanRPMAddr));
}

int getRemoteControlLastSeen()
{
    int ttl = stove.read(ram, remoteControlTTLAddr);
    if (ttl > 0)
    {
        ttl = 240 - ttl;
    }
    else
    {
        ttl = -1;
    }
    return ttl;
}

void getAmbTempStove() // Get room temperature from stove sensor
{
    if (!readKeep(ambTempStove, ram, ambTempStoveAddr, 0.5f)) LOGW("stove", "stove sensor temperature (RAM 0x%02X) could not be read - keeping %.1f °C", ambTempStoveAddr, ambTempStove);
}

void getAmbTempRemoteControl() // Get room temperature from stove sensor
{
    if (!readKeep(ambTempRemoteControl, ram, ambTempRemoteAddr, 0.5f)) LOGW("stove", "remote control temperature (RAM 0x%02X) could not be read - keeping %.1f °C", ambTempRemoteAddr, ambTempRemoteControl);
}

int numberOfStarts()
{
    int hi = stove.read(eeprom, numberOfStartsHiAddr);
    int lo = stove.read(eeprom, numberOfStartsLoAddr);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 8) + lo;
}

bool stoveBeepIsEnabled() // Get the beep setting
{
    // Use a local: this used to write into the global ambTemp, which made the
    // room temperature show the beep flag (1 = enabled -> "1 °C").
    int val = stove.read(eeprom, beepEnabledAddr);
    if (val < 0) return cachedBeepEnabled;  // keep last known value on a serial error
    return val != 0;
}

int arePalletsLow()
{
    readKeep(cachedPelletSensor, ram, palletSensorAddr);
    readKeep(cachedPelletCounter, ram, palletLevelAddr);
    readKeep(cachedPelletReserveOn, eeprom, pelletReserveEnabledAddr);
    // Pellet reserve, phase 2: once RAM 0x89 reached 0, RAM 0x91 counts down in steps of
    // 30 s until the stove switches off (seen 17/11/2026, "PELLET RESERVE xx'" on the
    // remote). Only meaningful in that phase, so only read then.
    if (pc::reserveSensorLow(cachedPelletSensor) && cachedPelletCounter == 0) readKeep(cachedPelletShutdown, ram, 0x91);
    else cachedPelletShutdown = -1;
    return pc::pelletsLow(cachedPelletSensor, cachedPelletCounter);
}

void getTempSetpoint()
{
    readKeep(setPointTemp, eeprom, TempSetpointAddr);
}

void getFumeTemp() // Get flue gas temperature
{
    readKeep(fumesTemp, ram, fumesTempAddr);
}

// The stove stores its clock in EEPROM as BCD (e.g. 16 -> 0x16).
static void writeBcdToStove(int number, byte address)
{
    byte value = pc::toBcd(number);
    stove.write(eeprom, address, value);
    delay(20);
}

// Write a local date/time to the stove clock. The stove has no DST support,
// so it simply gets the local wall-clock time.
void setStoveDateTimeTm(const struct tm& t)
{
    StoveBusLock lock;
    writeBcdToStove(t.tm_mday,        eepromDateDayAddr);
    writeBcdToStove(t.tm_mon + 1,     eepromDateMonthAddr);
    writeBcdToStove(t.tm_year % 100,  eepromDateYearAddr);
    writeBcdToStove(t.tm_hour,        eepromTimeHourAddr);
    writeBcdToStove(t.tm_min,         eepromTimeMinAddr);
    // Day of week: 1 = Monday ... 7 = Sunday (manual, CLOCK/CHRONO). This used to be
    // written with strftime("%W"), which is the week number, so chrono programs ran
    // on the wrong days.
    writeBcdToStove(t.tm_wday == 0 ? 7 : t.tm_wday, eepromTimeDOWAddr);
}

void setStoveDateTime()   // sync to NTP time
{
    getNTPtime(2);
    struct tm t;
    localtime_r(&now, &t);
    setStoveDateTimeTm(t);
}

void getRoomFanSpeed()
{
    readKeep(roomFanSpeed, eeprom, roomFanEepromAddr);
}

void getFlamePower() // Get the flame power (1 - 4)
{
    readKeep(flamePower, eeprom, flamePowerEepromAddr);
}

void getEnergySaving() 
{
    // EEPROM holds the start offset as a positive number; the API exposes it negative
    readKeep(energySavingStart, eeprom, energySavingStartAddr, -1.0f);
    readKeep(energySavingStop,  eeprom, energySavingStopAddr);
}


int getErrorMemory(int pos);
bool getStoveDateTime();

// Re-read one timer program into the cache used by BLE and /metrics.
// Only updates the cache when all 7 registers were read successfully.
static bool refreshTimerCache(int i)
{
    byte base = timerBases[i];
    int v[7] = {
        stove.read(eeprom, base + TIMER_ONOFF_OFFSET),
        stove.read(eeprom, base + TIMER_ONTIME_OFFSET),
        stove.read(eeprom, base + TIMER_OFFTIME_OFFSET),
        stove.read(eeprom, base + TIMER_SETPOINT_OFFSET),
        stove.read(eeprom, base + TIMER_FLAME_OFFSET),
        stove.read(eeprom, base + TIMER_FAN_OFFSET),
        stove.read(eeprom, base + TIMER_DOW_OFFSET),
    };
    for (int k = 0; k < 7; k++) if (v[k] < 0) return false;
    cachedTimerEnabled[i]  = v[0];
    cachedTimerOnVal[i]    = v[1];
    cachedTimerOffVal[i]   = v[2];
    cachedTimerSetpoint[i] = v[3];
    cachedTimerFlame[i]    = v[4];
    cachedTimerFan[i]      = v[5];
    cachedTimerDowMask[i]  = v[6];
    return true;
}


// ── What changed? ─────────────────────────────────────────────────────
// Logs the stove values when they change (old -> new), not every reading.
// Settings someone changes (set point, flame, fan, energy saving, ...) are info;
// measured values (temperatures, actual flame / fan, fume fan, auger) are debug,
// temperatures only when they move by at least the given step.
static void logStoveChanges(bool slow)
{
    static bool primed = false, primedSlow = false;
    static float lRoom, lStoveT, lRemote, lAuger;
    static int lSet, lFlame, lFlameAct, lFan, lFanAct, lFanPwr, lFume, lRpm, lEsStart, lEsStop, lLowPel, lBrazier, lHot, lWireless, lCq, lPq;
    static bool lBeep;
    const char* fanName[] = { "off", "1", "2", "3" };
    auto fanStr = [&](int v) { return v >= 0 && v <= 3 ? fanName[v] : "?"; };

    if (!primed) {
        lFlame = flamePower; lFlameAct = cachedFlameActual; lFan = roomFanSpeed; lFanAct = roomFanActual();
        lFanPwr = cachedRoomFanControl; lFume = fumesTemp; lRpm = cachedFumeFanRPM; lBrazier = cachedBrazierCountdown; lHot = cachedHotForFan;
        primed = true;
        LOGI("stove", "first reading: %s, flame power %d (actual %d), room fan %s, fumes %d °C, fume fan %d rpm",
             pc::stoveStateName(stoveState), flamePower, cachedFlameActual, fanStr(roomFanSpeed), (int)fumesTemp, cachedFumeFanRPM);
    } else {
        if (flamePower != lFlame) { LOGI("stove", "flame power setting %d -> %d", lFlame, flamePower); lFlame = flamePower; }
        if (cachedFlameActual != lFlameAct) { LOGD("stove", "flame power actual %d -> %d (setting %d)", lFlameAct, cachedFlameActual, flamePower); lFlameAct = cachedFlameActual; }
        if (roomFanSpeed != lFan) { LOGI("stove", "room fan setting %s -> %s", fanStr(lFan), fanStr(roomFanSpeed)); lFan = roomFanSpeed; }
        int fa = roomFanActual();
        if (fa != lFanAct) { LOGD("stove", "room fan actual %s -> %s (power RAM 0x0A = %d)", fanStr(lFanAct), fanStr(fa), cachedRoomFanControl); lFanAct = fa; }
        if (cachedRoomFanControl != lFanPwr && fa == lFanAct) { LOGD("stove", "room fan power (RAM 0x0A) %d -> %d", lFanPwr, cachedRoomFanControl); }
        lFanPwr = cachedRoomFanControl;
        if (abs((int)fumesTemp - lFume) >= 5) { LOGD("stove", "fume temperature %d -> %d °C", lFume, (int)fumesTemp); lFume = fumesTemp; }
        if ((cachedFumeFanRPM == 0) != (lRpm == 0) || abs(cachedFumeFanRPM - lRpm) >= 200) {
            if (cachedFumeFanRPM == 0) LOGD("stove", "fume fan stopped (was %d rpm)", lRpm);
            else if (lRpm == 0) LOGD("stove", "fume fan started: %d rpm", cachedFumeFanRPM);
            else LOGD("stove", "fume fan %d -> %d rpm", lRpm, cachedFumeFanRPM);
            lRpm = cachedFumeFanRPM;
        }
        if (cachedHotForFan != lHot && cachedHotForFan >= 0 && lHot >= 0 && (cachedHotForFan == 0) != (lHot == 0)) {
            LOGD("stove", "stove %s for the room fan (RAM 0x04 %d -> %d)", cachedHotForFan ? "hot enough" : "too cold", lHot, cachedHotForFan);
        }
        lHot = cachedHotForFan;
        if (cachedBrazierCountdown != lBrazier) {
            if (cachedBrazierCountdown >= 0 && stoveState == 4 && (cachedBrazierCountdown % 10 == 0 || cachedBrazierCountdown <= 3))
                LOGD("stove", "brazier cleaning in %d min", cachedBrazierCountdown);
            lBrazier = cachedBrazierCountdown;
        }
    }
    if (!slow) return;

    if (!primedSlow) {
        lRoom = ambTemp; lStoveT = ambTempStove; lRemote = ambTempRemoteControl; lAuger = cachedAugerSpeed;
        lSet = (int)setPointTemp; lEsStart = energySavingStart; lEsStop = energySavingStop; lLowPel = cachedLowPallet;
        lBeep = cachedBeepEnabled; lWireless = cachedWirelessSensor; lCq = cachedCombustionQuality; lPq = cachedPelletQuality;
        primedSlow = true;
        LOGI("stove", "settings: set point %d °C, energy saving %s, room %.1f °C (stove sensor %.1f, remote %.1f)",
             (int)setPointTemp, energySavingStop ? (String("stop +") + energySavingStop + " / restart " + energySavingStart + " °C").c_str() : "off",
             ambTemp, ambTempStove, ambTempRemoteControl);
        return;
    }
    if ((int)setPointTemp != lSet) { LOGI("stove", "set point %d -> %d °C", lSet, (int)setPointTemp); lSet = (int)setPointTemp; }
    if (energySavingStop != lEsStop || energySavingStart != lEsStart) {
        if (!energySavingStop) LOGI("stove", "energy saving off");
        else LOGI("stove", "energy saving: stop at set point +%d, restart at %d °C", energySavingStop, energySavingStart);
        lEsStop = energySavingStop; lEsStart = energySavingStart;
    }
    if (cachedBeepEnabled != lBeep) { LOGI("stove", "beep %s", cachedBeepEnabled ? "on" : "off"); lBeep = cachedBeepEnabled; }
    if (cachedWirelessSensor != lWireless && cachedWirelessSensor >= 0) { LOGI("stove", "wireless sensor (INSTELL WIRELESS) %s", cachedWirelessSensor ? "on" : "off"); lWireless = cachedWirelessSensor; }
    if (cachedCombustionQuality != lCq) { const char* l = pc::combustionQualityLabel(cachedCombustionQuality); LOGI("stove", "combustion quality -> %s", l ? l : "?"); lCq = cachedCombustionQuality; }
    if (cachedPelletQuality != lPq) { const char* l = pc::pelletQualityLabel(cachedPelletQuality); LOGI("stove", "pellet quality -> %s", l ? l : "?"); lPq = cachedPelletQuality; }
    if (cachedLowPallet != lLowPel) {
        if (cachedLowPallet) LOGW("stove", "pellets low - refill the hopper");
        else LOGI("stove", "pellet level OK again");
        lLowPel = cachedLowPallet;
    }
    if (fabsf(ambTemp - lRoom) >= 0.5f) { LOGD("stove", "room temperature %.1f -> %.1f °C (set point %d)", lRoom, ambTemp, (int)setPointTemp); lRoom = ambTemp; }
    if (fabsf(ambTempStove - lStoveT) >= 0.5f) { LOGD("stove", "stove sensor %.1f -> %.1f °C", lStoveT, ambTempStove); lStoveT = ambTempStove; }
    if (fabsf(ambTempRemoteControl - lRemote) >= 0.5f) { LOGD("stove", "remote control sensor %.1f -> %.1f °C", lRemote, ambTempRemoteControl); lRemote = ambTempRemoteControl; }
    if (fabsf(cachedAugerSpeed - lAuger) >= 0.5f) { LOGD("stove", "auger %.1f -> %.1f rpm", lAuger, cachedAugerSpeed); lAuger = cachedAugerSpeed; }
}

// ── Read planner ─────────────────────────────────────────────────────────────────────────
// Every value has its own read interval (one table below), instead of a fast round every
// second and a slow round every ~16 s that held the bus for seconds and read the timers and
// the error memory as often as the temperatures. The sensor task reads one group at a time
// (registers that belong together: a 16-bit counter, a timer program), holding the bus lock
// only for that group, so a web route waits at most one group.
//   - which group: the one that is due with the highest priority, then the most overdue
//   - intervals per state: "active" (start-up, burning, cleaning, alarm) or "idle" (off, stand-by)
//   - nothing due: spare bus time reads the setting / rare / static group read longest ago (at
//     least a quarter of its interval old), while the bus is busy less than PLAN_BUS_CAP
//   - due at once: everything after a (re)connect of the serial link (serial number and stove
//     firmware are only read then and once a day), a register after our own write to it, the
//     error memory on a new error / alarm, the counters at a start
//   - the remote control's menu (RAM 0x1A, every 2 s): while a menu is open the settings are read
//     every 5 s and, in the chrono menu or a menu that is not known, a timer program every 2 s;
//     back on the main screen all settings and timers are read once more
//   - while a Bluetooth client is connected the intervals are 4 x longer (shared radio)
// GET /api/plan shows every group with its interval, age and read count.

enum PlanPrio : uint8_t { P_LIVE = 1, P_REGULAR = 2, P_SETTING = 3, P_RARE = 4, P_STATIC = 5 };
struct PlanGroup {
    const char* name;
    uint8_t     prio;
    uint16_t    activeS, idleS;   // interval while the stove is active / idle (seconds)
    bool      (*read)();          // the reads, with the bus lock held; false = a read failed
    uint16_t    regs[8];          // (bank << 8) | address, for "due after a write"; 0xFFFF = end
    uint32_t    lastMs;           // last read (0 = never)
    uint32_t    okMs;             // last read that succeeded (0 = never): the cache holds a real value
    bool        due;              // read as soon as possible
    bool        sinceConnect;     // read since the last (re)connect
    uint32_t    reads, fails;
};
#define R_(a) ((uint16_t)(a))
#define E_(a) ((uint16_t)(0x100 | (a)))
#define END_  0xFFFF

static int  cachedRemoteMenu = -1;     // RAM 0x1A, the menu the remote control shows (0 = main screen)
static int  cachedStoveLanguage = -1;  // EEPROM 0x4F (static group: at a (re)connect and after a write)
static bool planFullDone = false;      // every group read once since the (re)connect

// ── Group reads ──
static bool gState()
{
    getStoveState();
    return stoveState != UNKNOWN;
}
static void planDueRare(const char* name);
static bool gError()
{
    bool ok = readKeep(cachedCurrentError, ram, currentErrorAddr);
    static int prevErr = 0;
    static uint32_t errSince = 0;
    if (cachedCurrentError > 0) readKeep(cachedErrorCountdown, ram, 0x30);   // countdown to the alarm
    else cachedErrorCountdown = -1;
    if (cachedCurrentError > 0 && prevErr == 0) {
        errSince = millis();
        LOGW("stove", "stove reports %s (%s): it acts on it in %d s unless it clears (RAM 0x30)",
             pc::errorCode(cachedCurrentError), pc::errorDesc(cachedCurrentError), cachedErrorCountdown);
        planDueRare("error memory");
    } else if (cachedCurrentError == 0 && prevErr > 0) {
        LOGI("stove", "%s cleared after %lu s", pc::errorCode(prevErr), (unsigned long)((millis() - errSince) / 1000));
    }
    prevErr = cachedCurrentError;
    return ok;
}
static bool gFume()     { return readKeep(fumesTemp, ram, fumesTempAddr); }
static bool gAuger()    { return readKeep(cachedAugerSpeed, ram, augerSpeedAddr, 0.1f); }
static bool gFlameAct() { return readKeep(cachedFlameActual, ram, flamePowerActualAddr); }
static bool gRpm()      { int v = getFumeFanRPM(); if (v >= 0) cachedFumeFanRPM = v; return v >= 0; }
static void planMenuChanged(int from, int to);
static bool gMenu()
{
    int m = stove.read(ram, displayMenuAddr);
    if (m < 0) return false;
    if (m != cachedRemoteMenu) { int from = cachedRemoteMenu; cachedRemoteMenu = m; if (from >= 0) planMenuChanged(from, m); }
    return true;
}
static bool gRoom()     { getAmbTemp(); return true; }
static bool gRoomSensors() { getAmbTempStove(); getAmbTempRemoteControl(); return true; }
static bool gFan()
{
    bool a = readKeep(cachedRoomFanControl, ram, roomFanControlAddr);
    bool b = readKeep(cachedRoomFanRunning, ram, roomFanRunningAddr);
    bool c = readKeep(cachedHotForFan, ram, hotForFanAddr);
    return a && b && c;
}
static bool gRemoteSeen() { cachedRemoteControlLastSeen = getRemoteControlLastSeen(); return true; }
// Level sensor every 5 s; the reserve counter with it while the sensor is not 0, the shut-down
// countdown in the last phase only (the pellet reserve, docs/discovered-values.md)
static bool gPellet()
{
    bool ok = readKeep(cachedPelletSensor, ram, palletSensorAddr);
    static uint32_t counterAt = 0;
    if (cachedPelletSensor != 0 || !counterAt || millis() - counterAt > 60000) {
        if (readKeep(cachedPelletCounter, ram, palletLevelAddr)) counterAt = millis();
    }
    if (pc::reserveSensorLow(cachedPelletSensor) && cachedPelletCounter == 0) readKeep(cachedPelletShutdown, ram, 0x91);
    else cachedPelletShutdown = -1;
    cachedLowPallet = pc::pelletsLow(cachedPelletSensor, cachedPelletCounter);
    return ok;
}
static bool gBrazier()  { return readKeep(cachedBrazierCountdown, ram, brazierCountdownAddr); }
static bool gSetMain()
{
    bool a = readKeep(setPointTemp, eeprom, TempSetpointAddr);
    bool b = readKeep(flamePower, eeprom, flamePowerEepromAddr);
    bool c = readKeep(roomFanSpeed, eeprom, roomFanEepromAddr);
    return a && b && c;
}
static bool gEnergySaving() { getEnergySaving(); return true; }
static bool gSetOther()
{
    {   // INSTELL WIRELESS, bit 0 of RAM 0xB6: 1 = off, 0 = on; on a read error keep the previous value
        int sel = stove.read(ram, wirelessSensorAddr);
        if (sel >= 0) cachedWirelessSensor = (sel & 0x01) ? 0 : 1;
    }
    readKeep(cachedCombustionQuality, eeprom, combustionQualityAddr);
    readKeep(cachedPelletQuality,     eeprom, pelletQualityAddr);
    readKeep(cachedPelletReserveOn,   eeprom, pelletReserveEnabledAddr);
    cachedBeepEnabled = stoveBeepIsEnabled();
    return true;
}
template <int I> static bool gTimer() { return refreshTimerCache(I); }
static bool gErrMem()
{
    bool ok = true;
    for (int i = 0; i < 5; i++) { int e = getErrorMemory(i); if (e >= 0) cachedErrors[i] = e; else ok = false; }
    return ok;
}
static bool gCounters()
{
    int starts = numberOfStarts();
    if (starts >= 0) cachedNumberOfStarts = starts;
    {   // 16-bit: reading only the low byte showed 4 instead of 260 once it passed 255
        int lo = stove.read(eeprom, hoursSinceLastServiceAddr);
        int hi = stove.read(eeprom, hoursSinceLastServiceAddrHigh);
        if (lo >= 0 && hi >= 0) cachedHoursSinceLastService = lo | (hi << 8);
    }
    int lo = stove.read(eeprom, totalRuntimeAddrLow);
    int hi = stove.read(eeprom, totalRuntimeAddrHigh);
    if (lo >= 0 && hi >= 0) cachedTotalRuntime = lo | (hi << 8);
    return starts >= 0;
}
static bool gClock()    { return getStoveDateTime(); }
static bool gBrazierInterval() { return readKeep(cachedBrazierInterval, eeprom, brazierIntervalAddr); }
static bool gStatic()
{
    readFirmwareVersion(cachedFirmwareVersion, sizeof(cachedFirmwareVersion));   // keeps the old one on an error
    readKeep(cachedStoveLanguage, eeprom, languageAddr);
    long prev = cachedSerialNumber;
    cachedSerialNumber = -1;   // read it again (fixed, but after a reconnect it may be another board)
    readSerialNumber();
    if (cachedSerialNumber < 0) { cachedSerialNumber = prev; return false; }
    return true;
}

// ── The table: one place for every interval ──
static PlanGroup planGroups[] = {
    // name                  prio        active idle  read            registers (for "due after a write")
    { "state",               P_LIVE,      1,    1,   gState,         { R_(stoveStateAddr), END_ } },
    { "error",               P_LIVE,      1,    2,   gError,         { R_(currentErrorAddr), R_(0x30), END_ } },
    { "fumes",               P_LIVE,      2,   15,   gFume,          { R_(fumesTempAddr), END_ } },
    { "auger",               P_LIVE,      2,   30,   gAuger,         { R_(augerSpeedAddr), END_ } },
    { "flame actual",        P_LIVE,      2,   30,   gFlameAct,      { R_(flamePowerActualAddr), END_ } },
    { "fume fan",            P_LIVE,      2,   30,   gRpm,           { R_(fumeFanRPMAddr), END_ } },
    { "remote menu",         P_LIVE,      2,    2,   gMenu,          { R_(displayMenuAddr), END_ } },
    { "room temperature",    P_REGULAR,  10,   10,   gRoom,          { R_(ambTempAddr), END_ } },
    { "room sensors",        P_REGULAR,  15,   15,   gRoomSensors,   { R_(ambTempStoveAddr), R_(ambTempRemoteAddr), END_ } },
    { "room fan",            P_REGULAR,   5,   30,   gFan,           { R_(roomFanControlAddr), R_(roomFanRunningAddr), R_(hotForFanAddr), END_ } },
    { "remote seen",         P_REGULAR,   5,    5,   gRemoteSeen,    { R_(remoteControlTTLAddr), END_ } },
    { "pellet sensor",       P_REGULAR,   5,    5,   gPellet,        { R_(palletSensorAddr), R_(palletLevelAddr), R_(0x91), END_ } },
    { "brazier cleaning",    P_REGULAR,  30,  300,   gBrazier,       { R_(brazierCountdownAddr), END_ } },
    { "set point / flame / fan", P_SETTING, 10, 10,  gSetMain,       { E_(TempSetpointAddr), E_(flamePowerEepromAddr), E_(roomFanEepromAddr), END_ } },
    { "energy saving",       P_SETTING,  30,   30,   gEnergySaving,  { E_(energySavingStopAddr), E_(energySavingStartAddr), END_ } },
    { "other settings",      P_SETTING,  60,   60,   gSetOther,      { R_(wirelessSensorAddr), E_(combustionQualityAddr), E_(pelletQualityAddr), E_(pelletReserveEnabledAddr), E_(beepEnabledAddr), END_ } },
    { "timer PR1",           P_RARE,     60,   60,   gTimer<0>,      { E_(0x3C), E_(0x3D), E_(0x3E), E_(0x3F), E_(0x40), E_(0x41), E_(0x43), END_ } },
    { "timer PR2",           P_RARE,     60,   60,   gTimer<1>,      { E_(0x50), E_(0x51), E_(0x52), E_(0x53), E_(0x54), E_(0x55), E_(0x57), END_ } },
    { "timer PR3",           P_RARE,     60,   60,   gTimer<2>,      { E_(0x58), E_(0x59), E_(0x5A), E_(0x5B), E_(0x5C), E_(0x5D), E_(0x5F), END_ } },
    { "timer PR4",           P_RARE,     60,   60,   gTimer<3>,      { E_(0x60), E_(0x61), E_(0x62), E_(0x63), E_(0x64), E_(0x65), E_(0x67), END_ } },
    { "timer PR5",           P_RARE,     60,   60,   gTimer<4>,      { E_(0x68), E_(0x69), E_(0x6A), E_(0x6B), E_(0x6C), E_(0x6D), E_(0x6F), END_ } },
    { "timer PR6",           P_RARE,     60,   60,   gTimer<5>,      { E_(0x70), E_(0x71), E_(0x72), E_(0x73), E_(0x74), E_(0x75), E_(0x77), END_ } },
    { "error memory",        P_RARE,    300,  300,   gErrMem,        { E_(0xE0), E_(0xE1), E_(0xE2), E_(0xE3), E_(0xE4), END_ } },
    { "counters",            P_RARE,    600,  600,   gCounters,      { E_(numberOfStartsHiAddr), E_(numberOfStartsLoAddr), E_(0xE7), E_(0xE8), E_(0xEA), E_(0xEB), END_ } },
    { "stove clock",         P_RARE,    300,  300,   gClock,         { E_(eepromTimeMinAddr), E_(eepromTimeHourAddr), E_(eepromDateDayAddr), E_(eepromDateMonthAddr), E_(eepromDateYearAddr), E_(eepromTimeDOWAddr), END_ } },
    { "brazier interval",    P_RARE,   1800, 1800,   gBrazierInterval, { E_(brazierIntervalAddr), END_ } },
    { "firmware / serial / language", P_STATIC, 43200, 43200, gStatic, { E_(languageAddr), END_ } },
};
static const int PLAN_N = sizeof(planGroups) / sizeof(planGroups[0]);
static const int PLAN_TIMER0 = 16;            // index of "timer PR1" (6 in a row)
static const int PLAN_ERRMEM = 22, PLAN_COUNTERS = 23;
static const uint32_t PLAN_BUS_CAP = 50;      // % of the time the bus may be busy with spare-time reads
static const uint32_t PLAN_PAUSE_MS = 5;      // after every group: others get the bus

static bool planActive() { return (stoveState >= 1 && stoveState <= 6) || stoveState >= 8; }
// Known remote menus that have nothing to do with the timers (registers.h, getRemoteMenuName)
static bool planMenuNotTimer(int m)
{
    switch (m) {
    case 0: case 1: case 3: case 5: case 7: case 8: case 9: case 11: case 12: case 13: case 14: case 19:
    case 39: case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47: case 49: case 50:
    case 51: case 52: case 53: case 54: case 64: case 65: return true;
    default: return false;
    }
}
static uint32_t planIntervalMs(const PlanGroup& g)
{
    uint32_t s = planActive() ? g.activeS : g.idleS;
    int m = cachedRemoteMenu;
    if (m > 0) {   // someone is in a menu of the remote control
        if (g.prio == P_SETTING && s > 5) s = 5;
        int idx = &g - planGroups;
        if (idx >= PLAN_TIMER0 && idx < PLAN_TIMER0 + 6 && !planMenuNotTimer(m) && s > 12) s = 12;   // a program every 2 s
    }
    if (bleIsClientConnected()) s *= 4;
    return s * 1000UL;
}
static void planDueRare(const char* name)
{
    for (auto& g : planGroups) if (!strcmp(g.name, name)) g.due = true;
}
static void planMenuChanged(int from, int to)
{
    LOGD("stove", "remote control menu %d -> %d", from, to);
    if (to == 0 && from > 0)   // back on the main screen: whatever was changed there, read it now
        for (auto& g : planGroups) if (g.prio == P_SETTING || (&g - planGroups >= PLAN_TIMER0 && &g - planGroups < PLAN_TIMER0 + 6)) g.due = true;
}
// After our own write: read the group of that register again right away
static void planWritten(uint8_t type, uint8_t addr)
{
    uint16_t key = (type == eeprom ? 0x100 : 0) | addr;
    for (auto& g : planGroups)
        for (int k = 0; k < 8 && g.regs[k] != END_; k++) if (g.regs[k] == key) { g.due = true; break; }
}
static void planConnect()   // (re)connect: read everything once
{
    for (auto& g : planGroups) { g.due = true; g.sinceConnect = false; }
    planFullDone = false;
}

// Bus time used by the planner, for the spare-time cap
static uint32_t planBusyMs = 0, planWindowMs = 0, planUtil = 0;

// Next group to read, or nullptr
static PlanGroup* planPick(uint32_t now)
{
    PlanGroup* best = nullptr; float bestScore = 0;
    for (auto& g : planGroups) {
        if (stoveState == UNKNOWN && &g != planGroups) continue;   // no answer: only the state register
        uint32_t iv = planIntervalMs(g);
        uint32_t age = g.lastMs ? now - g.lastMs : 0xFFFFFFFF;
        if (!(g.due || age >= iv)) continue;
        float over = g.lastMs ? (float)age / iv : 100.0f;                                  // never read: very overdue
        if (over > 100.0f) over = 100.0f;
        float score = (6 - g.prio) * 1000.0f + (g.due ? 500.0f : 0) + over;   // priority first, then overdue
        if (!best || score > bestScore) { best = &g; bestScore = score; }
    }
    // bus use in the running window (the cap would otherwise only act a window later)
    uint32_t el = now - planWindowMs;
    uint32_t util = el >= 1000 ? planBusyMs * 100 / el : planUtil;
    if (best || !planFullDone || util >= PLAN_BUS_CAP) return best;
    // spare time: the setting / rare / static group read longest ago, relative to its interval
    for (auto& g : planGroups) {
        if (g.prio < P_SETTING) continue;
        uint32_t iv = planIntervalMs(g), age = now - g.lastMs;
        float ratio = (float)age / iv;
        if (ratio < 0.25f) continue;
        if (!best || ratio > bestScore) { best = &g; bestScore = ratio; }
    }
    return best;
}

// Read one group (bus lock held only for it) and what follows from it
static void planRead(PlanGroup& g)
{
    uint32_t tWait = millis();
    stoveLockTake();
    uint32_t t0 = millis(), waited = t0 - tWait;
    if (waited > 3000) LOGW("stove", "sensor task waited %lu ms for the stove bus (held by a web / MQTT / Bluetooth command)", (unsigned long)waited);
    bool ok = g.read();
    if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
    uint32_t now = millis(), d = now - t0;
    g.reads++; if (!ok) g.fails++; else g.okMs = millis();
    g.due = false;
    // a failed read is tried again after 10 s at the latest (e.g. the stove clock before it is set)
    uint32_t iv = planIntervalMs(g);
    g.lastMs = ok || iv <= 10000 ? now : now - iv + 10000;
    g.sinceConnect = true;
    planBusyMs += d;
    diagCycle(g.prio >= P_SETTING, waited, d);
    if (&g == planGroups) {   // the state register
        if (stoveState == UNKNOWN) return;
        static int lastState = -1;
        if (lastState == 0 && stoveState == 1) planGroups[PLAN_COUNTERS].due = true;          // a start: number of starts
        if (stoveState >= 8 && lastState >= 0 && lastState < 8) planGroups[PLAN_ERRMEM].due = true;   // alarm
        lastState = stoveState;
        lastFastStatesAt = now;
    }
    if (!planFullDone) {
        bool all = true;
        for (auto& x : planGroups) if (!x.sinceConnect) { all = false; break; }
        if (!all) return;
        planFullDone = true;
        lastSlowStatesAt = now;
        logStoveChanges(false);
        logStoveChanges(true);   // first reading / settings
        return;
    }
    // age of the oldest regular value / setting ("slowSeconds" in /api/state)
    uint32_t oldest = now;
    for (auto& x : planGroups) if ((x.prio == P_REGULAR || x.prio == P_SETTING) && x.lastMs && x.lastMs < oldest) oldest = x.lastMs;
    lastSlowStatesAt = oldest;
    logStoveChanges(g.prio >= P_REGULAR);
}

// Seconds since timer program idx (0 - 5) was read into the cache, -1 = not yet (routesTimers.h)
long planTimerAgeS(int idx)
{
    const PlanGroup& g = planGroups[PLAN_TIMER0 + idx];
    return g.okMs ? (long)((millis() - g.okMs) / 1000) : -1;
}

// The remote control's menu from the planner when it is fresh (read every 2 s), -1 = read it
int planRemoteMenuFresh() { return planGroups[6].lastMs && millis() - planGroups[6].lastMs < 3000 ? cachedRemoteMenu : -1; }
int planStoveLanguage() { return cachedStoveLanguage; }

// Full refresh (start-up): let the sensor task read every group once and wait for it (at most
// 20 s; it stops early when the stove does not answer)
void getStates()
{
    planConnect();
    uint32_t t0 = millis();
    while (!planFullDone && millis() - t0 < 20000 && !stove.linkDown && !(stoveState == UNKNOWN && planGroups[0].reads)) {
        esp_task_wdt_reset();
        delay(50);
    }
}

// GET /api/plan -> the read plan: every group with its interval now, age and reads
void handleRoutePlan()
{
    uint32_t now = millis();
    String out; out.reserve(3200);
    char b[200];
    snprintf(b, sizeof(b), "{\"active\":%s,\"remoteMenu\":%d,\"busPercent\":%lu,\"fullRead\":%s,\"groups\":[",
             planActive() ? "true" : "false", cachedRemoteMenu, (unsigned long)planUtil, planFullDone ? "true" : "false");
    out += b;
    for (int i = 0; i < PLAN_N; i++) {
        const PlanGroup& g = planGroups[i];
        snprintf(b, sizeof(b), "%s{\"name\":\"%s\",\"prio\":%d,\"intervalS\":%lu,\"ageS\":%ld,\"reads\":%lu,\"fails\":%lu}",
                 i ? "," : "", g.name, g.prio, (unsigned long)(planIntervalMs(g) / 1000), g.lastMs ? (long)((now - g.lastMs) / 1000) : -1L,
                 (unsigned long)g.reads, (unsigned long)g.fails);
        out += b;
    }
    out += "]}";
    wm.server->send(200, "application/json", out);
}

static void sensorTask(void* pvParameters)
{
    const TickType_t idleTicks = pdMS_TO_TICKS(500);
    static uint32_t lastProbe = 0, downSince = 0;
    static bool wasDown = false;
    planWindowMs = millis();
    for (;;)
    {
        unsigned long now = millis();
        if (otaActive)                       // firmware update running: stay off the bus and the CPU
        {
            vTaskDelay(idleTicks);
            continue;
        }
        if (keyWatchTick())                  // key watch running: only the watched addresses, as fast as possible
        {
            vTaskDelay(1);
            continue;
        }
        if (bleStoveUartInQuietWindow(now, 1500) || (bleIsClientConnected() && bleConnectionAgeMs() < 5000))
        {
            vTaskDelay(idleTicks);
            continue;
        }
        // Serial link to the stove down (stove.linkDown, set by the Stove library after repeated
        // failures): every other bus command fails at once; probe the state register every 5 s.
        if (stove.linkDown) {
            if (!wasDown) {
                wasDown = true; downSince = millis();
                LOGE("stove", "no answer from the stove (serial connection?) - all stove commands fail at once, probing every 5 s");
            }
            stoveState = UNKNOWN;
            snprintf(StoveStateStr, sizeof(StoveStateStr), "%s", pc::stoveStateName(UNKNOWN));
            StoveIsOn = false;
            if (millis() - lastProbe >= 5000) {
                lastProbe = millis();
                stoveLockTake();
                stove.probe(ram, stoveStateAddr);
                if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
            }
            vTaskDelay(idleTicks);
            continue;
        }
        if (wasDown) {
            wasDown = false;
            LOGI("stove", "stove answers again after %lu s - reading everything once", (unsigned long)((millis() - downSince) / 1000));
            planConnect();
        }
        // bus use over the last ~5 s, for the spare-time cap
        if (now - planWindowMs >= 5000) { planUtil = planBusyMs * 100 / (now - planWindowMs); planBusyMs = 0; planWindowMs = now; }
        PlanGroup* g = planPick(now);
        if (!g) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        planRead(*g);
        vTaskDelay(pdMS_TO_TICKS(PLAN_PAUSE_MS));   // the web server and others get the bus
    }
}
