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
    if (cachedPelletSensor == 5 && cachedPelletCounter == 0) readKeep(cachedPelletShutdown, ram, 0x91);
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
static void stoveTimeTick();

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

// Fast-changing values — refreshed every 2 s
void getFastStates()
{
    // Serial link to the stove down (stove.linkDown, set by the Stove library after
    // repeated failures): every other bus command fails at once; here the state
    // register is probed every 5 s until the stove answers again.
    static uint32_t lastProbe = 0, downSince = 0;
    static bool wasDown = false;
    if (stove.linkDown) {
        if (!wasDown) {
            wasDown = true; downSince = millis();
            LOGE("stove", "no answer from the stove (serial connection?) - all stove commands fail at once, probing every 5 s");
        }
        stoveState = UNKNOWN;
        snprintf(StoveStateStr, sizeof(StoveStateStr), "%s", pc::stoveStateName(UNKNOWN));
        StoveIsOn = false;
        if (millis() - lastProbe < 5000) return;
        lastProbe = millis();
        stoveLockTake();
        int v = stove.probe(ram, stoveStateAddr);
        if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
        if (v < 0) return;
    }
    if (wasDown) { wasDown = false; LOGI("stove", "stove answers again after %lu s", (unsigned long)((millis() - downSince) / 1000)); }
    uint32_t tWait = millis();
    stoveLockTake();
    uint32_t t0 = millis(), waited = t0 - tWait;
    if (waited > 3000) LOGW("stove", "sensor task waited %lu ms for the stove bus (held by a web / MQTT / Bluetooth command)", (unsigned long)waited);

    getStoveState();
    // No answer from the stove (serial connection lost / stove without power): read
    // only the state register until it answers again, so the bus lock is free for
    // the web server instead of being held for every register x 3 attempts.
    if (stoveState == UNKNOWN) {   // no answer (the link goes down after repeated failures, see above)
        if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
        return;
    }
    getFumeTemp();
    getFlamePower();
    getRoomFanSpeed();
    int rpm = getFumeFanRPM();
    if (rpm >= 0) cachedFumeFanRPM = rpm;
    readKeep(cachedCurrentError, ram, currentErrorAddr);
    {
        // countdown to the alarm while the stove reports an error (RAM 0x30)
        static int prevErr = 0;
        static uint32_t errSince = 0;
        if (cachedCurrentError > 0) readKeep(cachedErrorCountdown, ram, 0x30);
        else cachedErrorCountdown = -1;
        if (cachedCurrentError > 0 && prevErr == 0) {
            errSince = millis();
            LOGW("stove", "stove reports %s (%s): it acts on it in %d s unless it clears (RAM 0x30)",
                 pc::errorCode(cachedCurrentError), pc::errorDesc(cachedCurrentError), cachedErrorCountdown);
        } else if (cachedCurrentError == 0 && prevErr > 0) {
            LOGI("stove", "%s cleared after %lu s", pc::errorCode(prevErr), (unsigned long)((millis() - errSince) / 1000));
        }
        prevErr = cachedCurrentError;
    }
    readKeep(cachedFlameActual, ram, flamePowerActualAddr);
    readKeep(cachedRoomFanControl, ram, roomFanControlAddr);
    readKeep(cachedRoomFanRunning, ram, roomFanRunningAddr);
    readKeep(cachedHotForFan, ram, hotForFanAddr);
    readKeep(cachedBrazierCountdown, ram, brazierCountdownAddr);
    cachedRemoteControlLastSeen = getRemoteControlLastSeen();
    lastFastStatesAt = millis();
    {
        uint32_t d = lastFastStatesAt - t0;
        diagCycle(false, waited, d);
        if (d > 5000) LOGW("stove", "reading the fast values took %lu ms (normally < 1 s): the stove answers slowly or not", (unsigned long)d);
    }
    logStoveChanges(false);

    if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
}

// Slow-changing values — refreshed every ~16 s
void getSlowStates()
{
    uint32_t tWait = millis();
    stoveLockTake();
    uint32_t t0 = millis(), waited = t0 - tWait;
    if (stove.linkDown || stoveState == UNKNOWN) { if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex); return; }   // stove not answering, see getFastStates

    getAmbTemp();
    getAmbTempStove();
    getAmbTempRemoteControl();
    {
        // INSTELL WIRELESS, bit 0 of RAM 0xB6: 1 = off, 0 = on.
        // On a read error keep the previous value.
        int sel = stove.read(ram, wirelessSensorAddr);
        if (sel >= 0) cachedWirelessSensor = (sel & 0x01) ? 0 : 1;
    }
    getTempSetpoint();
    getEnergySaving();
    yield();

    readKeep(cachedAugerSpeed, ram, augerSpeedAddr, 0.1f);
    readFirmwareVersion(cachedFirmwareVersion, sizeof(cachedFirmwareVersion));
    yield();

    cachedLowPallet      = arePalletsLow();
    cachedBeepEnabled    = stoveBeepIsEnabled();
    int starts = numberOfStarts();
    if (starts >= 0) cachedNumberOfStarts = starts;
    yield();

    readKeep(cachedHoursSinceLastService, eeprom, hoursSinceLastServiceAddr);
    readKeep(cachedBrazierInterval, eeprom, brazierIntervalAddr);
    {
        int lo = stove.read(eeprom, totalRuntimeAddrLow);
        int hi = stove.read(eeprom, totalRuntimeAddrHigh);
        if (lo >= 0 && hi >= 0) cachedTotalRuntime = lo | (hi << 8);
    }
    yield();

    readKeep(cachedCombustionQuality, eeprom, combustionQualityAddr);
    readKeep(cachedPelletQuality,     eeprom, pelletQualityAddr);
    yield();

    for (int i = 0; i < 5; i++) {
        int err = getErrorMemory(i);
        if (err >= 0) cachedErrors[i] = err;
        yield();
    }

    for (int i = 0; i < 6; i++) {
        refreshTimerCache(i);
        yield();
    }

    lastSlowStatesAt = millis();
    {
        uint32_t d = lastSlowStatesAt - t0;
        diagCycle(true, waited, d);
        if (d > 30000) LOGW("stove", "reading the slow values took %lu ms (normally a few s): the stove answers slowly or not", (unsigned long)d);
    }
    logStoveChanges(true);
    if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
}

// Full refresh — used at startup and for manual sync
void getStates()
{
    getFastStates();
    getSlowStates();
    getStoveDateTime();
}

static void sensorTask(void* pvParameters)
{
    const TickType_t delayTicks = pdMS_TO_TICKS(500);
    uint8_t slowCounter = 0;
    uint8_t bleFastCounter = 0;
    uint8_t bleSlowCounter = 0;
    for (;;)
    {
        unsigned long now = millis();
        if (otaActive)                       // firmware update running: stay off the bus and the CPU
        {
            vTaskDelay(delayTicks);
            continue;
        }
        if (keyWatchTick())                  // key watch running: only the watched addresses, as fast as possible
        {
            vTaskDelay(1);
            continue;
        }
        if (bleStoveUartInQuietWindow(now, 1500))
        {
            vTaskDelay(delayTicks);
            continue;
        }

        if (bleIsClientConnected())
        {
            if (bleConnectionAgeMs() < 5000)
            {
                vTaskDelay(delayTicks);
                continue;
            }

            if (++bleFastCounter >= 8) // every ~4 s while BLE is active
            {
                getFastStates();
                bleFastCounter = 0;
            }
            if (++bleSlowCounter >= 120) // every ~60 s while BLE is active
            {
                getSlowStates();
                bleSlowCounter = 0;
            }
            stoveTimeTick();
        }
        else
        {
            bleFastCounter = 0;
            bleSlowCounter = 0;
            getFastStates();
            if (++slowCounter >= 30) // every ~15 s
            {
                getSlowStates();
                slowCounter = 0;
            }
            stoveTimeTick();
        }
        vTaskDelay(delayTicks);
    }
}
