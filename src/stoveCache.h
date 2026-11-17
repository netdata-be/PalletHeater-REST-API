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
#if SENSOR_DEBUG
            Serial.print(".");
#endif
            delay(10);
        } while (((millis() - start) <= (1000 * sec)) && (ntp_tm.tm_year < (2016 - 1900)));
        if (ntp_tm.tm_year <= (2016 - 1900))
            return false; // the NTP call was not successful

        currentDst = ntp_tm.tm_isdst;
#if SENSOR_DEBUG
        Serial.print("now ");
        Serial.println(now);
        char time_output[30];
        strftime(time_output, 30, "%a  %d-%m-%y %T", localtime(&now));
        Serial.println(time_output);
        Serial.println();
#endif
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
unsigned long lastFastStatesAt = 0;
unsigned long lastSlowStatesAt = 0;

void getAmbTemp() // Get room temperature
{
    if (!readKeep(ambTemp, ram, ambTempAddr, 0.5f)) log_e("Failed to get room temperature");
#if SENSOR_DEBUG
    Serial.print("T. amb. ");
    Serial.println(ambTemp);
#endif
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
    if (!readKeep(ambTempStove, ram, ambTempStoveAddr, 0.5f)) log_e("Failed to get stove temperature");
#if SENSOR_DEBUG
    Serial.print("T. amb. stove : ");
    Serial.println(ambTempStove);
#endif
}

void getAmbTempRemoteControl() // Get room temperature from stove sensor
{
    if (!readKeep(ambTempRemoteControl, ram, ambTempRemoteAddr, 0.5f)) log_e("Failed to get remote control temperature");
#if SENSOR_DEBUG
    Serial.print("T. amb. remote control : ");
    Serial.println(ambTempRemoteControl);
#endif
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
    return pc::pelletsLow(cachedPelletSensor, cachedPelletCounter);
}

void getTempSetpoint()
{
    readKeep(setPointTemp, eeprom, TempSetpointAddr);
#if SENSOR_DEBUG
    Serial.print("T. setpoint. ");
    Serial.println(setPointTemp);
#endif
}

void getFumeTemp() // Get flue gas temperature
{
    readKeep(fumesTemp, ram, fumesTempAddr);
#if SENSOR_DEBUG
    Serial.printf("T. fumes %d\n", fumesTemp);
#endif
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
#if SENSOR_DEBUG
    Serial.printf("Fan speed: %d\n", roomFanSpeed);
#endif
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

// Fast-changing values — refreshed every 2 s
void getFastStates()
{
    if (stoveMutex) xSemaphoreTakeRecursive(stoveMutex, portMAX_DELAY);

    getStoveState();
    getFumeTemp();
    getFlamePower();
    getRoomFanSpeed();
    int rpm = getFumeFanRPM();
    if (rpm >= 0) cachedFumeFanRPM = rpm;
    readKeep(cachedCurrentError, ram, currentErrorAddr);
    readKeep(cachedFlameActual, ram, flamePowerActualAddr);
    readKeep(cachedRoomFanControl, ram, roomFanControlAddr);
    readKeep(cachedRoomFanRunning, ram, roomFanRunningAddr);
    readKeep(cachedHotForFan, ram, hotForFanAddr);
    readKeep(cachedBrazierCountdown, ram, brazierCountdownAddr);
    cachedRemoteControlLastSeen = getRemoteControlLastSeen();
    lastFastStatesAt = millis();

    if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
}

// Slow-changing values — refreshed every ~16 s
void getSlowStates()
{
    if (stoveMutex) xSemaphoreTakeRecursive(stoveMutex, portMAX_DELAY);

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
