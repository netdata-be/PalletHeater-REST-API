#pragma once
#include <NimBLEDevice.h>
#include <esp_ota_ops.h>
#include <WiFi.h>
#include <Preferences.h>
#include <constants.h>

// Forward declarations
bool setFlamePower(int power);
bool setRoomFanSpeed(int speed);
void setCombustionQuality(int val);
void getTempSetpoint();
bool setEnergySavingStart(int start);
bool setEnergySavingStop(int stop);

// ─── Stove service UUIDs ──────────────────────────────────────────────────────
// Room temp is int16 little-endian, value * 10 (e.g. 21.5 °C → 215).
// Fume fan RPM is uint16 little-endian. Everything else is uint8.

#define STOVE_SVC_UUID         "A1B20001-1234-5678-ABCD-EF0123456789"
#define CHAR_STOVE_STATE_UUID  "A1B20002-1234-5678-ABCD-EF0123456789"  // R  N
#define CHAR_POWER_UUID        "A1B20003-1234-5678-ABCD-EF0123456789"  // RWN  0=off 1=on
#define CHAR_SETPOINT_UUID     "A1B20004-1234-5678-ABCD-EF0123456789"  // RWN  7-30 °C
#define CHAR_FAN_UUID          "A1B20005-1234-5678-ABCD-EF0123456789"  // RWN  0-3
#define CHAR_FLAME_UUID        "A1B20006-1234-5678-ABCD-EF0123456789"  // RWN  1-4
#define CHAR_ROOM_TEMP_UUID    "A1B20007-1234-5678-ABCD-EF0123456789"  // R  N  int16 * 10
#define CHAR_FUME_TEMP_UUID    "A1B20008-1234-5678-ABCD-EF0123456789"  // R  N  °C
#define CHAR_BEEP_UUID         "A1B20009-1234-5678-ABCD-EF0123456789"  // RWN  0/1
#define CHAR_LOW_PALLET_UUID   "A1B2000A-1234-5678-ABCD-EF0123456789"  // R  N  0/1
#define CHAR_FUME_FAN_RPM_UUID "A1B2000B-1234-5678-ABCD-EF0123456789"  // R  N  uint16
#define CHAR_COMBUSTION_UUID   "A1B2000C-1234-5678-ABCD-EF0123456789"  // RWN  raw: 2=-3 … 8=+3 76=+G
#define CHAR_PELLET_UUID       "A1B2000D-1234-5678-ABCD-EF0123456789"  // RWN  raw: 0=-5 … 10=+5
#define CHAR_IP_UUID           "A1B2000E-1234-5678-ABCD-EF0123456789"  // R  N  UTF-8 IP address
#define CHAR_INFO_UUID         "A1B2000F-1234-5678-ABCD-EF0123456789"  // R  N  cached info
#define CHAR_ENERGY_SAVING_START_UUID "A1B20014-1234-5678-ABCD-EF0123456789"  // RWN  start offset (-3..0)
#define CHAR_ENERGY_SAVING_STOP_UUID  "A1B20015-1234-5678-ABCD-EF0123456789"  // RWN  stop offset (0..3)
// byte 0: WiFi RSSI int8 (0 = not connected); bytes 1+: IP null-terminated, then SSID (no null)
#define CHAR_WIFI_UUID         "A1B20013-1234-5678-ABCD-EF0123456789"  // R  N  WiFi RSSI + IP + SSID
#define CHAR_RESTART_UUID      "A1B20016-1234-5678-ABCD-EF0123456789"  // W   write any byte to restart
#define CHAR_HOSTNAME_UUID     "A1B20017-1234-5678-ABCD-EF0123456789"  // RWN UTF-8 hostname (saved to NVS; takes effect after restart)
#define CHAR_NETCONFIG_UUID    "A1B20018-1234-5678-ABCD-EF0123456789"  // RWN CSV: "dhcp" or "static,ip,gw,sn,dns" (takes effect after restart)

// ─── Timer service UUIDs ─────────────────────────────────────────────────────
// Each characteristic is 7 bytes: [enabled, on_raw, off_raw, setpoint, flame, fan, dow_mask]
// on_raw/off_raw: val*10 = minutes since 00:00; 144 = disabled
// dow_mask bits: Mon=1 Tue=2 Wed=4 Thu=8 Fri=16 Sat=32 Sun=64

#define CHAR_TIMER1_UUID "A1B20020-1234-5678-ABCD-EF0123456789"  // RWN
#define CHAR_TIMER2_UUID "A1B20021-1234-5678-ABCD-EF0123456789"  // RWN
#define CHAR_TIMER3_UUID "A1B20022-1234-5678-ABCD-EF0123456789"  // RWN
#define CHAR_TIMER4_UUID "A1B20023-1234-5678-ABCD-EF0123456789"  // RWN
#define CHAR_TIMER5_UUID "A1B20024-1234-5678-ABCD-EF0123456789"  // RWN
#define CHAR_TIMER6_UUID "A1B20025-1234-5678-ABCD-EF0123456789"  // RWN

// ─── OTA service UUIDs ────────────────────────────────────────────────────────
// Protocol:
//   1. Client writes CTRL: 0x01 + uint32_t LE firmware size + API key (ASCII, /api/auth)
//                                                           → NOTIFY 0x01 (ready) | 0xFF (error, also a wrong key)
//   2. Client writes DATA chunks (WRITE_NR)                 → NOTIFY 0x50 <pct%> every 4 KB
//   3. Client writes CTRL: 0x02                             → NOTIFY 0x02 (success, reboots) | 0xFF
//   Abort: client writes CTRL 0x03 at any time             → NOTIFY 0x03

#define OTA_SVC_UUID  "A1B20010-1234-5678-ABCD-EF0123456789"
#define OTA_CTRL_UUID "A1B20011-1234-5678-ABCD-EF0123456789"  // RWN
#define OTA_DATA_UUID "A1B20012-1234-5678-ABCD-EF0123456789"  // W (no response)

// ─── Characteristic handles ───────────────────────────────────────────────────

static NimBLECharacteristic* charTimer[6]    = {nullptr};
static NimBLECharacteristic* charStoveState  = nullptr;
static NimBLECharacteristic* charPower       = nullptr;
static NimBLECharacteristic* charSetpoint    = nullptr;
static NimBLECharacteristic* charFan         = nullptr;
static NimBLECharacteristic* charFlame       = nullptr;
static NimBLECharacteristic* charRoomTemp    = nullptr;
static NimBLECharacteristic* charFumeTemp    = nullptr;
static NimBLECharacteristic* charBeep        = nullptr;
static NimBLECharacteristic* charLowPallet   = nullptr;
static NimBLECharacteristic* charFumeFanRpm  = nullptr;
static NimBLECharacteristic* charCombustion  = nullptr;
static NimBLECharacteristic* charPellet      = nullptr;
static NimBLECharacteristic* charIPAddr      = nullptr;
static NimBLECharacteristic* charInfo        = nullptr;
static NimBLECharacteristic* charEnergySavingStart = nullptr;
static NimBLECharacteristic* charEnergySavingStop  = nullptr;
static NimBLECharacteristic* charWifi        = nullptr;
static NimBLECharacteristic* charRestart     = nullptr;
static NimBLECharacteristic* charHostname    = nullptr;
static NimBLECharacteristic* charNetConfig   = nullptr;
static NimBLECharacteristic* otaCtrl         = nullptr;
static NimBLECharacteristic* otaData         = nullptr;

static bool          bleRestartPending     = false;
static bool          bleClientConnected    = false;
static bool          bleForceNotifyPending = false;
static uint16_t      bleConnHandle         = 0xFFFF;
static unsigned long bleConnectedAt        = 0;
static unsigned long bleLastUpdate         = 0;
static bool          bleAdvPending         = false;
static unsigned long bleAdvPendingAt       = 0;
static unsigned long bleLastStoveOpAt      = 0;

enum BLEWriteKind : uint8_t {
    BW_POWER,
    BW_SETPOINT,
    BW_FAN,
    BW_FLAME,
    BW_BEEP,
    BW_COMBUSTION,
    BW_PELLET,
    BW_ENERGY_SAVING_START,
    BW_ENERGY_SAVING_STOP
};

struct BLEWriteJob {
    BLEWriteKind kind = BW_POWER;
    uint8_t      value = 0;
};

static constexpr uint8_t BLE_WRITE_QUEUE_SIZE = 12;
static BLEWriteJob bleWriteQueue[BLE_WRITE_QUEUE_SIZE];
static volatile uint8_t bleWriteHead = 0;
static volatile uint8_t bleWriteTail = 0;
static volatile uint8_t bleWriteCount = 0;
static portMUX_TYPE bleWriteQueueMux = portMUX_INITIALIZER_UNLOCKED;

struct BLEPendingTimerWrite {
    int           idx  = -1;
    uint8_t       data[7] = {};
    volatile bool ready = false;
};
static BLEPendingTimerWrite blePendingTimerWrite;

static bool bleOwnsRadio() {
    return bleClientConnected || bleAdvPending;
}

static bool bleIsClientConnected() {
    return bleClientConnected;
}

static unsigned long bleConnectionAgeMs() {
    return bleClientConnected ? millis() - bleConnectedAt : 0;
}

static bool bleStoveUartInQuietWindow(unsigned long now, unsigned long quietMs) {
    return bleLastStoveOpAt != 0 && now - bleLastStoveOpAt < quietMs;
}

static bool bleWriteQueuePush(BLEWriteKind kind, uint8_t value) {
    portENTER_CRITICAL(&bleWriteQueueMux);
    if (bleWriteCount >= BLE_WRITE_QUEUE_SIZE) {
        portEXIT_CRITICAL(&bleWriteQueueMux);
        return false;
    }
    bleWriteQueue[bleWriteTail].kind = kind;
    bleWriteQueue[bleWriteTail].value = value;
    bleWriteTail = (bleWriteTail + 1) % BLE_WRITE_QUEUE_SIZE;
    bleWriteCount++;
    portEXIT_CRITICAL(&bleWriteQueueMux);
    return true;
}

static bool bleWriteQueuePop(BLEWriteJob* job) {
    portENTER_CRITICAL(&bleWriteQueueMux);
    if (bleWriteCount == 0) {
        portEXIT_CRITICAL(&bleWriteQueueMux);
        return false;
    }
    *job = bleWriteQueue[bleWriteHead];
    bleWriteHead = (bleWriteHead + 1) % BLE_WRITE_QUEUE_SIZE;
    bleWriteCount--;
    portEXIT_CRITICAL(&bleWriteQueueMux);
    return true;
}

static bool bleWriteQueueHasJobs() {
    portENTER_CRITICAL(&bleWriteQueueMux);
    bool hasJobs = bleWriteCount > 0;
    portEXIT_CRITICAL(&bleWriteQueueMux);
    return hasJobs;
}

// ─── OTA state ────────────────────────────────────────────────────────────────

static esp_ota_handle_t       otaHandle       = 0;
static const esp_partition_t* otaPartition    = nullptr;
static bool                   otaInProgress   = false;
static size_t                 otaBytesWritten = 0;
static size_t                 otaTotalSize    = 0;

// ─── Value helpers ────────────────────────────────────────────────────────────

static void bleSetU8 (NimBLECharacteristic* c, uint8_t v)  { c->setValue(&v, 1); }
static void bleSetI16(NimBLECharacteristic* c, float v) {
    int16_t iv = (int16_t)(v * 10.0f);
    c->setValue((uint8_t*)&iv, 2);
}
static void bleSetU16(NimBLECharacteristic* c, uint16_t v) { c->setValue((uint8_t*)&v, 2); }

static void bleSetInfo() {
    uint8_t info[28] = {};
    size_t fwLen = strnlen(cachedFirmwareVersion, 11);
    memcpy(info, cachedFirmwareVersion, fwLen);
    uint16_t runtime = (uint16_t)max(0, min(65535, cachedTotalRuntime));
    uint16_t starts = (uint16_t)max(0, min(65535, cachedNumberOfStarts));
    memcpy(&info[11], &runtime, 2);
    memcpy(&info[13], &starts, 2);
    for (int i = 0; i < 5; i++) info[15 + i] = (uint8_t)max(0, min(255, cachedErrors[i]));
    uint16_t serviceHours = (uint16_t)max(0, min(65535, cachedHoursSinceLastService));
    memcpy(&info[20], &serviceHours, 2);
    int16_t remoteTempRaw = INT16_MIN;
    if (ambTempRemoteControl >= 0.0f) {
        remoteTempRaw = (int16_t)round(ambTempRemoteControl * 2.0f);
    }
    memcpy(&info[22], &remoteTempRaw, 2);
    uint32_t stoveTimestamp = (uint32_t)max((time_t)0, stoveTime);
    memcpy(&info[24], &stoveTimestamp, 4);
    charInfo->setValue(info, sizeof(info));
}

static void bleSetWifi() {
    // Format: [RSSI int8][IP UTF-8 null-terminated][SSID UTF-8 no null]
    uint8_t buf[52] = {};
    bool connected = WiFi.status() == WL_CONNECTED;
    buf[0] = (uint8_t)(int8_t)(connected ? WiFi.RSSI() : 0);
    String ip   = connected ? WiFi.localIP().toString() : String("");
    String ssid = connected ? WiFi.SSID()               : String("");
    size_t ipLen   = min((size_t)15, ip.length());
    size_t ssidLen = min((size_t)32, ssid.length());
    memcpy(buf + 1, ip.c_str(), ipLen);
    buf[1 + ipLen] = 0; // null separator between IP and SSID
    memcpy(buf + 2 + ipLen, ssid.c_str(), ssidLen);
    charWifi->setValue(buf, 2 + ipLen + ssidLen);
}

static void bleUpdateAll() {
    // BLE reads and notifications publish cached stove values only. The sensor
    // task is responsible for refreshing these values from the stove UART.
    bleSetU8 (charStoveState, (uint8_t)stoveState);
    bleSetU8 (charPower,      (uint8_t)StoveIsOn);
    bleSetU8 (charSetpoint,   (uint8_t)setPointTemp);
    bleSetU8 (charFan,        (uint8_t)roomFanSpeed);
    bleSetU8 (charFlame,      (uint8_t)flamePower);
    bleSetI16(charRoomTemp,   ambTemp);
    bleSetU8 (charFumeTemp,   (uint8_t)fumesTemp);
    bleSetU8 (charBeep,       (uint8_t)cachedBeepEnabled);
    bleSetU8 (charLowPallet,  (uint8_t)cachedLowPallet);
    bleSetU16(charFumeFanRpm, (uint16_t)cachedFumeFanRPM);
    bleSetU8 (charCombustion, (uint8_t)cachedCombustionQuality);
    bleSetU8 (charPellet,     (uint8_t)cachedPelletQuality);
    bleSetU8 (charEnergySavingStart, (uint8_t)energySavingStart);
    bleSetU8 (charEnergySavingStop,  (uint8_t)energySavingStop);
    String ip = WiFi.localIP().toString();
    charIPAddr->setValue(ip.c_str());
    bleSetInfo();
    bleSetWifi();
    for (int i = 0; i < 6; i++) {
        uint8_t td[7] = {
            (uint8_t)cachedTimerEnabled[i],
            (uint8_t)cachedTimerOnVal[i],
            (uint8_t)cachedTimerOffVal[i],
            (uint8_t)cachedTimerSetpoint[i],
            (uint8_t)cachedTimerFlame[i],
            (uint8_t)cachedTimerFan[i],
            (uint8_t)cachedTimerDowMask[i]
        };
        charTimer[i]->setValue(td, 7);
    }
}

static void bleForceNotifyAll() {
    charStoveState->notify();
    charPower->notify();
    charSetpoint->notify();
    charFan->notify();
    charFlame->notify();
    charRoomTemp->notify();
    charFumeTemp->notify();
    charBeep->notify();
    charLowPallet->notify();
    charFumeFanRpm->notify();
    charCombustion->notify();
    charPellet->notify();
    charIPAddr->notify();
    charInfo->notify();
    charWifi->notify();
    charEnergySavingStart->notify();
    charEnergySavingStop->notify();
    for (int i = 0; i < 6; i++) charTimer[i]->notify();
}

static void bleNotifyChanges() {
    static int     prevState      = -99;
    static int     prevPower      = -1;
    static int     prevSetpoint   = -1;
    static int     prevFan        = -1;
    static int     prevFlame      = -1;
    static int16_t prevRoomTemp   = INT16_MIN;
    static int     prevFumeTemp   = -1;
    static int     prevBeep       = -1;
    static int     prevLowPallet  = -1;
    static int     prevFumeFanRpm = -1;
    static int     prevCombustion = -1;
    static int     prevPellet     = -1;
    static int     prevStarts     = -1;
    static int     prevRuntime    = -1;
    static int     prevInfoLowPallet = -1;
    static int     prevErrors[5]  = {-1, -1, -1, -1, -1};    static int prevServiceHours = -1;
    static int16_t prevRemoteTemp = INT16_MIN;
    static uint32_t prevStoveTime = 0;    static String  prevFirmware   = "";
    static String  prevIP         = "";
    static int8_t  prevWifiRSSI   = 0;
    static String  prevWifiSSID   = "";
    static String  prevWifiIP     = "";

#define BLE_NOTIFY_IF(ch, prev, cur) \
    if ((int)(cur) != (prev)) { ch->notify(); (prev) = (int)(cur); }

    static int prevEnergySavingStart = 127;
    static int prevEnergySavingStop  = -1;

    BLE_NOTIFY_IF(charStoveState, prevState,      stoveState)
    BLE_NOTIFY_IF(charPower,      prevPower,       (int)StoveIsOn)
    BLE_NOTIFY_IF(charSetpoint,   prevSetpoint,    (int)setPointTemp)
    BLE_NOTIFY_IF(charFan,        prevFan,         roomFanSpeed)
    BLE_NOTIFY_IF(charFlame,      prevFlame,       flamePower)
    BLE_NOTIFY_IF(charFumeTemp,   prevFumeTemp,    (int)fumesTemp)
    BLE_NOTIFY_IF(charBeep,       prevBeep,        (int)cachedBeepEnabled)
    BLE_NOTIFY_IF(charLowPallet,  prevLowPallet,   cachedLowPallet)
    BLE_NOTIFY_IF(charFumeFanRpm, prevFumeFanRpm,  cachedFumeFanRPM)
    BLE_NOTIFY_IF(charCombustion, prevCombustion,  cachedCombustionQuality)
    BLE_NOTIFY_IF(charPellet,     prevPellet,      cachedPelletQuality)
    BLE_NOTIFY_IF(charEnergySavingStart, prevEnergySavingStart, energySavingStart)
    BLE_NOTIFY_IF(charEnergySavingStop,  prevEnergySavingStop,  energySavingStop)

#undef BLE_NOTIFY_IF

    int16_t curRoomTemp = (int16_t)(ambTemp * 10.0f);
    if (curRoomTemp != prevRoomTemp) {
        charRoomTemp->notify();
        prevRoomTemp = curRoomTemp;
    }

    String curIP = WiFi.localIP().toString();
    if (curIP != prevIP) {
        charIPAddr->setValue(curIP.c_str());
        charIPAddr->notify();
        prevIP = curIP;
    }

    bool wifiNowConnected = WiFi.status() == WL_CONNECTED;
    int8_t curWifiRSSI = (int8_t)(wifiNowConnected ? WiFi.RSSI() : 0);
    String curWifiSSID = wifiNowConnected ? WiFi.SSID()             : String("");
    String curWifiIP   = wifiNowConnected ? WiFi.localIP().toString(): String("");
    if (curWifiRSSI != prevWifiRSSI || curWifiSSID != prevWifiSSID || curWifiIP != prevWifiIP) {
        bleSetWifi();
        charWifi->notify();
        prevWifiRSSI = curWifiRSSI;
        prevWifiSSID = curWifiSSID;
        prevWifiIP   = curWifiIP;
    }

    bool infoChanged = cachedNumberOfStarts != prevStarts ||
                       cachedTotalRuntime != prevRuntime ||
                       cachedLowPallet != prevInfoLowPallet ||
                       cachedHoursSinceLastService != prevServiceHours ||
                       strcmp(cachedFirmwareVersion, prevFirmware.c_str()) != 0;
    for (int i = 0; i < 5; i++) {
        if (cachedErrors[i] != prevErrors[i]) infoChanged = true;
    }
    int16_t curRemoteTemp = INT16_MIN;
    if (ambTempRemoteControl >= 0.0f) {
        curRemoteTemp = (int16_t)round(ambTempRemoteControl * 2.0f);
    }
    if (curRemoteTemp != prevRemoteTemp) infoChanged = true;
    if ((uint32_t)stoveTime != prevStoveTime) infoChanged = true;
    if (infoChanged) {
        bleSetInfo();
        charInfo->notify();
        prevStarts = cachedNumberOfStarts;
        prevRuntime = cachedTotalRuntime;
        prevInfoLowPallet = cachedLowPallet;
        prevServiceHours = cachedHoursSinceLastService;
        prevRemoteTemp = curRemoteTemp;
        prevStoveTime = (uint32_t)stoveTime;
        prevFirmware = cachedFirmwareVersion;
        for (int i = 0; i < 5; i++) prevErrors[i] = cachedErrors[i];
    }

    static uint8_t prevTimerData[6][7] = {};
    for (int i = 0; i < 6; i++) {
        uint8_t td[7] = {
            (uint8_t)cachedTimerEnabled[i],
            (uint8_t)cachedTimerOnVal[i],
            (uint8_t)cachedTimerOffVal[i],
            (uint8_t)cachedTimerSetpoint[i],
            (uint8_t)cachedTimerFlame[i],
            (uint8_t)cachedTimerFan[i],
            (uint8_t)cachedTimerDowMask[i]
        };
        if (memcmp(td, prevTimerData[i], 7) != 0) {
            charTimer[i]->notify();
            memcpy(prevTimerData[i], td, 7);
        }
    }
}

// ─── Stove write callback ─────────────────────────────────────────────────────

// Shared helper — call from every onSubscribe override.
// Logs the subscription and immediately pushes the current value so the app
// does not need to issue a separate readValue for initial population.
static void handleSubscribe(NimBLECharacteristic* pChar, uint16_t subValue) {
    LOGD("ble", "+%lums subscription %s = %u",
        millis() - bleConnectedAt, pChar->getUUID().toString().c_str(), subValue);
    if (subValue > 0) pChar->notify(); // push current value immediately
}

// Logging-only callback for read/notify-only characteristics (no write handler needed).
class BLELogCB : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* pChar, ble_gap_conn_desc* desc, uint16_t subValue) override {
        handleSubscribe(pChar, subValue);
    }
};
static BLELogCB bleLogCB;

class StoveWriteCB : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* pChar, ble_gap_conn_desc* desc, uint16_t subValue) override {
        handleSubscribe(pChar, subValue);
    }
    void onWrite(NimBLECharacteristic* pChar) override {
        if (pChar->getDataLength() == 0) return;
        uint8_t v = pChar->getValue()[0];
        BLEWriteKind kind;
        if      (pChar == charPower)             kind = BW_POWER;
        else if (pChar == charSetpoint)          kind = BW_SETPOINT;
        else if (pChar == charFan)               kind = BW_FAN;
        else if (pChar == charFlame)             kind = BW_FLAME;
        else if (pChar == charBeep)              kind = BW_BEEP;
        else if (pChar == charCombustion)        kind = BW_COMBUSTION;
        else if (pChar == charPellet)            kind = BW_PELLET;
        else if (pChar == charEnergySavingStart) kind = BW_ENERGY_SAVING_START;
        else if (pChar == charEnergySavingStop)  kind = BW_ENERGY_SAVING_STOP;
        else return;

        LOGI("ble", "write received: characteristic %s, value %u",
                      pChar->getUUID().toString().c_str(), v);
        if (!bleWriteQueuePush(kind, v)) {
            LOGE("ble", "write queue full: command from the app dropped");
        }
    }
};

static StoveWriteCB bleWriteCB;

// ─── Timer write callback ─────────────────────────────────────────────────────

class TimerWriteCB : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* pChar, ble_gap_conn_desc* desc, uint16_t subValue) override {
        handleSubscribe(pChar, subValue);
    }
    void onWrite(NimBLECharacteristic* pChar) override {
        if (pChar->getDataLength() < 7) return;
        int idx = -1;
        for (int i = 0; i < 6; i++) {
            if (charTimer[i] == pChar) { idx = i; break; }
        }
        if (idx < 0) return;
        const uint8_t* d = (const uint8_t*)pChar->getValue().data();
        // Reject out-of-range settings: setpoint 7-30, flame 1-4, fan 0-3
        if (d[3] < 7 || d[3] > 30 || d[4] < 1 || d[4] > 4 || d[5] > 3) {
            LOGW("ble", "timer write idx=%d rejected: setpoint=%u flame=%u fan=%u out of range",
                          idx, d[3], d[4], d[5]);
            return;
        }
        LOGI("ble", "timer write received (index %d)", idx);
        // Update in-memory cache immediately so BLE reads reflect the new value
        // before the EEPROM write completes.
        cachedTimerEnabled[idx]  = d[0] ? 1 : 0;
        cachedTimerOnVal[idx]    = d[1];
        cachedTimerOffVal[idx]   = d[2];
        cachedTimerSetpoint[idx] = d[3];
        cachedTimerFlame[idx]    = d[4];
        cachedTimerFan[idx]      = d[5];
        cachedTimerDowMask[idx]  = d[6];
        // Defer EEPROM writes to bleLoop() under stoveMutex.
        blePendingTimerWrite.idx = idx;
        memcpy(blePendingTimerWrite.data, d, 7);
        blePendingTimerWrite.ready = true;
        bleForceNotifyPending = true;
    }
};

static TimerWriteCB timerWriteCB;

// ─── OTA callbacks ────────────────────────────────────────────────────────────

static void otaNotify(uint8_t status) {
    if (!otaCtrl) return;
    otaCtrl->setValue(&status, 1);
    otaCtrl->notify();
}

static bool bleOtaKeyOk(const uint8_t* key, size_t len);   // auth.h: the API key

class OtaCtrlCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        size_t len = pChar->getDataLength();
        if (len == 0) return;
        const uint8_t* data = (const uint8_t*)pChar->getValue().data();
        uint8_t cmd = data[0];

        if (cmd == 0x01) {
            uint32_t size = 0;
            if (len >= 5) memcpy(&size, &data[1], 4);
            // the API key follows the size: without it anyone in Bluetooth range could flash
            if (len <= 5 || !bleOtaKeyOk(&data[5], len - 5)) {
                LOGW("ble", "BLE OTA refused: API key missing or wrong");
                otaNotify(0xFF); return;
            }

            otaPartition = esp_ota_get_next_update_partition(nullptr);
            if (!otaPartition) { otaNotify(0xFF); return; }

            if (esp_ota_begin(otaPartition, size ? size : OTA_SIZE_UNKNOWN, &otaHandle) != ESP_OK) {
                otaNotify(0xFF); return;
            }
            otaInProgress   = true;
            otaBytesWritten = 0;
            otaTotalSize    = size;
            LOGI("ble", "BLE OTA update started");
            otaNotify(0x01);
        }
        else if (cmd == 0x02) {
            if (!otaInProgress) { otaNotify(0xFF); return; }
            otaInProgress = false;

            if (esp_ota_end(otaHandle) != ESP_OK)              { otaNotify(0xFF); return; }
            if (esp_ota_set_boot_partition(otaPartition) != ESP_OK) { otaNotify(0xFF); return; }

            LOGI("ble", "BLE OTA update complete");
            markRestart("firmware update (BLE OTA)");
            otaNotify(0x02);
            delay(1000);
            esp_restart();
        }
        else if (cmd == 0x03) {
            if (otaInProgress) { esp_ota_abort(otaHandle); otaInProgress = false; }
            otaNotify(0x03);
        }
    }
};

class OtaDataCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        if (!otaInProgress) return;
        size_t len = pChar->getDataLength();
        if (len == 0) return;

        if (esp_ota_write(otaHandle, pChar->getValue().data(), len) != ESP_OK) {
            esp_ota_abort(otaHandle);
            otaInProgress = false;
            otaNotify(0xFF);
            return;
        }

        otaBytesWritten += len;

        static size_t lastNotified = 0;
        if (otaBytesWritten - lastNotified >= 4096) {
            lastNotified = otaBytesWritten;
            uint8_t pct = otaTotalSize ? static_cast<uint8_t>(otaBytesWritten * 100 / otaTotalSize) : 0;
            uint8_t msg[2] = { 0x50, pct };
            otaCtrl->setValue(msg, 2);
            otaCtrl->notify();
        }
    }
};

static OtaCtrlCB otaCtrlCB;
static OtaDataCB otaDataCB;

static int bleHciReason(int reason) {
    // NimBLE host wraps controller/HCI errors as 0x200 + hci_reason.
    return (reason >= 0x200 && reason <= 0x2FF) ? reason - 0x200 : reason;
}

static const char* bleDisconnectReasonName(int reason) {
    reason = bleHciReason(reason);
    switch (reason) {
        case 0x08: return "connection timeout";
        case 0x13: return "remote user terminated";
        case 0x16: return "local host terminated";
        case 0x3E: return "connection failed to establish";
        default:   return "unknown";
    }
}

class RestartWriteCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        LOGI("ble", "restart requested via BLE");
        bleRestartPending = true;
    }
};
static RestartWriteCB restartWriteCB;

class HostnameWriteCB : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* pChar, ble_gap_conn_desc* desc, uint16_t subValue) override {
        handleSubscribe(pChar, subValue);
    }
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string val = pChar->getValue();
        if (val.empty()) return;
        // Keep printable ASCII only, max 32 chars
        String name = "";
        for (size_t i = 0; i < val.size() && name.length() < 32; i++) {
            char c = val[i];
            if (c >= 32 && c < 127) name += c;
        }
        if (name.isEmpty()) return;
        Preferences prefs;
        prefs.begin("net", false);
        prefs.putString("hostname", name);
        prefs.end();
        // Update characteristic so the app can confirm the saved value
        pChar->setValue(name.c_str());
        LOGI("ble", "hostname set to '%s' (takes effect after restart)", name.c_str());
    }
};
static HostnameWriteCB hostnameWriteCB;

class NetConfigWriteCB : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* pChar, ble_gap_conn_desc* desc, uint16_t subValue) override {
        handleSubscribe(pChar, subValue);
    }
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string val = pChar->getValue();
        if (val.empty()) return;
        String cfg(val.c_str());
        cfg.trim();
        bool valid = false;
        if (cfg == "dhcp") {
            valid = true;
        } else if (cfg.startsWith("static,")) {
            // Expect: static,ip,gw,subnet,dns
            int parts = 1;
            for (int i = 0; i < (int)cfg.length(); i++) if (cfg[i] == ',') parts++;
            if (parts == 5) {
                // Validate each IP field by attempting an IPAddress parse
                String rest = cfg.substring(7); // after "static,"
                IPAddress ip, gw, sn, dns;
                int c1 = rest.indexOf(',');
                int c2 = rest.indexOf(',', c1 + 1);
                int c3 = rest.indexOf(',', c2 + 1);
                if (c1 > 0 && c2 > c1 && c3 > c2) {
                    valid = ip.fromString(rest.substring(0, c1)) &&
                            gw.fromString(rest.substring(c1 + 1, c2)) &&
                            sn.fromString(rest.substring(c2 + 1, c3)) &&
                            dns.fromString(rest.substring(c3 + 1));
                }
            }
        }
        if (!valid) {
            LOGW("ble", "network config rejected: '%s'", cfg.c_str());
            return;
        }
        Preferences prefs;
        prefs.begin("net", false);
        prefs.putString("netcfg", cfg);
        prefs.end();
        pChar->setValue(cfg.c_str());
        LOGI("ble", "network config saved: '%s' (takes effect after a restart)", cfg.c_str());
    }
};
static NetConfigWriteCB netConfigWriteCB;

static int bleGapLogger(ble_gap_event* event, void* arg) {
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        LOGI("ble", "link closed: reason %d (0x%X), HCI 0x%02X (%s)",
            event->disconnect.reason,
            event->disconnect.reason,
            bleHciReason(event->disconnect.reason),
            bleDisconnectReasonName(event->disconnect.reason));
    }
    return 0;
}

// ─── Server callback ──────────────────────────────────────────────────────────

class BLEServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
        bleClientConnected    = true;
        bleForceNotifyPending = true;
        bleConnHandle         = desc->conn_handle;
        bleConnectedAt        = millis();
        bleLastUpdate         = millis();
        LOGD("ble", "client connected - waiting for subscriptions before sending all values");
        LOGI("ble", "client connected (handle %u, interval %u x 1.25 = %.0f ms, supervision timeout %u x 10 = %u ms)",
            desc->conn_handle,
            desc->conn_itvl, desc->conn_itvl * 1.25f,
            desc->supervision_timeout, desc->supervision_timeout * 10);
    }
    void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
        bleClientConnected = false;
        NimBLEDevice::getAdvertising()->stop();
        bleAdvPending   = true;
        bleAdvPendingAt = millis();
        LOGI("ble", "client disconnected (handle %u, interval %u x 1.25 = %.0f ms, supervision timeout %u x 10 = %u ms, connected %lu ms)",
            desc->conn_handle,
            desc->conn_itvl, desc->conn_itvl * 1.25f,
            desc->supervision_timeout, desc->supervision_timeout * 10,
            millis() - bleConnectedAt);
    }
};

static BLEServerCB bleServerCB;

// ─── Setup ────────────────────────────────────────────────────────────────────

static NimBLECharacteristic* makeChar(NimBLEService* svc, const char* uuid,
                                       uint32_t props, const char* desc) {
    auto* c = svc->createCharacteristic(uuid, props);
    c->createDescriptor("2901")->setValue(desc);
    return c;
}

void setupBLE() {
    NimBLEDevice::init("OpenPelletBridge");
    NimBLEDevice::setMTU(185);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setCustomGapHandler(bleGapLogger);

    NimBLEServer* pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(&bleServerCB);
    // Disable auto-advertise on disconnect — we manage it manually with a 2 s
    // backoff so iOS can't reconnect before NimBLE finishes cleaning up.
    pServer->advertiseOnDisconnect(false);

    // ── Stove control service ─────────────────────────────────────────────────
    NimBLEService* stoveSvc = pServer->createService(STOVE_SVC_UUID);

    const uint32_t RN  = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY;
    const uint32_t RWN = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY;

    charStoveState = makeChar(stoveSvc, CHAR_STOVE_STATE_UUID,  RN,  "Stove State (0=off 1=starting 2=loading 3=ignition 4=working 5=cleaning 6=final 7=standby 8=alarm)");
    charPower      = makeChar(stoveSvc, CHAR_POWER_UUID,        RWN, "Power (0=off 1=on)");
    charSetpoint   = makeChar(stoveSvc, CHAR_SETPOINT_UUID,     RWN, "Setpoint C (7-30)");
    charFan        = makeChar(stoveSvc, CHAR_FAN_UUID,          RWN, "Fan Speed (0-3)");
    charFlame      = makeChar(stoveSvc, CHAR_FLAME_UUID,        RWN, "Flame Power (1-4)");
    charRoomTemp   = makeChar(stoveSvc, CHAR_ROOM_TEMP_UUID,    RN,  "Room Temp (int16 LE x10 degC)");
    charFumeTemp   = makeChar(stoveSvc, CHAR_FUME_TEMP_UUID,    RN,  "Fume Temp (degC)");
    charBeep       = makeChar(stoveSvc, CHAR_BEEP_UUID,         RWN, "Beep (0=off 1=on)");
    charLowPallet  = makeChar(stoveSvc, CHAR_LOW_PALLET_UUID,   RN,  "Low Pellet Alarm (0=ok 1=alarm)");
    charFumeFanRpm = makeChar(stoveSvc, CHAR_FUME_FAN_RPM_UUID, RN,  "Fume Fan RPM (uint16 LE)");
    charCombustion = makeChar(stoveSvc, CHAR_COMBUSTION_UUID,   RWN, "Combustion Quality (2=-3 3=-2 4=-1 5=0 6=+1 7=+2 8=+3 76=+G)");
    charPellet     = makeChar(stoveSvc, CHAR_PELLET_UUID,       RWN, "Pellet Quality (0=-5 1=-4 2=-3 3=-2 4=-1 5=0 6=+1 7=+2 8=+3 9=+4 10=+5)");
    charEnergySavingStart = makeChar(stoveSvc, CHAR_ENERGY_SAVING_START_UUID, RWN, "Energy Saving Start Offset (-3..0)");
    charEnergySavingStop  = makeChar(stoveSvc, CHAR_ENERGY_SAVING_STOP_UUID,  RWN, "Energy Saving Stop Offset (0..3)");
    charIPAddr     = makeChar(stoveSvc, CHAR_IP_UUID,           RN,  "WiFi IP Address (UTF-8)");
    charInfo       = makeChar(stoveSvc, CHAR_INFO_UUID,         RN,  "Cached stove info JSON");
    charWifi       = makeChar(stoveSvc, CHAR_WIFI_UUID,         RN,  "WiFi info: byte0=RSSI int8, bytes1+=SSID UTF-8");
    charRestart    = makeChar(stoveSvc, CHAR_RESTART_UUID, NIMBLE_PROPERTY::WRITE_NR, "Write any byte to restart");
    charRestart->setCallbacks(&restartWriteCB);

    {
        Preferences prefs;
        prefs.begin("net", true);
        String hn = prefs.getString("hostname", "openpelletbridge");
        prefs.end();
        const uint32_t RWN = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY;
        charHostname = makeChar(stoveSvc, CHAR_HOSTNAME_UUID, RWN, "Hostname (UTF-8, takes effect after restart)");
        charHostname->setValue(hn.c_str());
        charHostname->setCallbacks(&hostnameWriteCB);
    }

    {
        Preferences prefs;
        prefs.begin("net", true);
        String nc = prefs.getString("netcfg", "dhcp");
        prefs.end();
        const uint32_t RWN = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY;
        charNetConfig = makeChar(stoveSvc, CHAR_NETCONFIG_UUID, RWN, "Net config: dhcp or static,ip,gw,sn,dns (restart to apply)");
        charNetConfig->setValue(nc.c_str());
        charNetConfig->setCallbacks(&netConfigWriteCB);
    }

    // Write + subscribe logging for writable characteristics.
    NimBLECharacteristic* writable[] = {
        charPower, charSetpoint, charFan, charFlame, charBeep, charCombustion, charPellet,
        charEnergySavingStart, charEnergySavingStop
    };
    for (auto* c : writable) c->setCallbacks(&bleWriteCB);

    // Subscribe logging for read-only characteristics.
    NimBLECharacteristic* readOnly[] = {
        charStoveState, charRoomTemp, charFumeTemp, charLowPallet,
        charFumeFanRpm, charIPAddr, charInfo, charWifi
    };
    for (auto* c : readOnly) c->setCallbacks(&bleLogCB);

    static const char* TIMER_UUIDS[6] = {
        CHAR_TIMER1_UUID, CHAR_TIMER2_UUID, CHAR_TIMER3_UUID,
        CHAR_TIMER4_UUID, CHAR_TIMER5_UUID, CHAR_TIMER6_UUID
    };
    for (int i = 0; i < 6; i++) {
        char desc[5];
        snprintf(desc, sizeof(desc), "PR%d", i + 1);
        charTimer[i] = makeChar(stoveSvc, TIMER_UUIDS[i], RWN, desc);
        charTimer[i]->setCallbacks(&timerWriteCB);
    }

    bleUpdateAll();
    stoveSvc->start();

    // ── OTA service ───────────────────────────────────────────────────────────
    NimBLEService* otaSvc = pServer->createService(OTA_SVC_UUID);

    otaCtrl = otaSvc->createCharacteristic(
        OTA_CTRL_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    otaCtrl->createDescriptor("2901")->setValue("OTA Control");
    otaCtrl->setCallbacks(&otaCtrlCB);

    otaData = otaSvc->createCharacteristic(OTA_DATA_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    otaData->createDescriptor("2901")->setValue("OTA Data");
    otaData->setCallbacks(&otaDataCB);

    otaSvc->start();

    // ── Advertising ───────────────────────────────────────────────────────────
    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(STOVE_SVC_UUID);
    pAdv->setScanResponse(true);
    // 40–80 ms advertising interval for quick iOS reconnect/discovery.
    pAdv->setMinInterval(64);    // 64 * 0.625 ms = 40 ms
    pAdv->setMaxInterval(128);   // 128 * 0.625 ms = 80 ms
    // Avoid forcing a preferred connection interval range on some central
    // devices that may fail during the initial link establishment.
    // pAdv->setMinPreferred(80);
    // pAdv->setMaxPreferred(160);
    NimBLEDevice::startAdvertising();

    // Enable NimBLE-Arduino info logs so disconnect reason codes are visible.
    esp_log_level_set("NimBLEServer", ESP_LOG_INFO);
    LOGI("ble", "Bluetooth started, advertising as OpenPelletBridge");
}

// Called from loop() — notifies changed characteristic values every 2 s.
void bleLoop() {
    unsigned long now = millis();

    if (bleRestartPending) {
        bleRestartPending = false;
        delay(200);
        esp_restart();
    }

    // Start advertising shortly after disconnect. iOS reconnects by scanning
    // for this advertisement, so longer backoff reduces failed direct-connect
    // retries from a stale central attempt.
    if (bleAdvPending && now - bleAdvPendingAt >= 1000) {
        bleAdvPending = false;
        LOGI("ble", "advertising restarted");
        NimBLEDevice::startAdvertising();
    }

    // Process BLE-originated stove writes asynchronously. The NimBLE callback
    // only enqueues work; this loop applies one UART transaction at a time with
    // spacing so back-to-back app writes cannot overlap stove frames.
    BLEWriteJob job;
    if (bleWriteQueueHasJobs() && !bleStoveUartInQuietWindow(now, 150) && stoveMutex &&
        xSemaphoreTakeRecursive(stoveMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
        CmdSource cs("Bluetooth app");
        if (!bleWriteQueuePop(&job)) {
            xSemaphoreGiveRecursive(stoveMutex);
        } else {
            uint8_t v = job.value;
            bool ok = true;
            if (job.kind == BW_POWER) {
                const int cachedState = stoveState;
                if      (v == 1 && pelletsEmptyOff()) { ok = false; LOGW("stove", "switch on refused (Bluetooth app): pellets used up, refill the hopper first"); }
                else if (v == 1 && cachedState == OFF)
                    ok = stove.write(ram, stoveStateAddr, 1);
                else if (v == 0 && (cachedState == IGNITION || cachedState == WORKING || cachedState == ALARM))
                    ok = stove.write(ram, stoveStateAddr, 6);
                else if (v == 0 && (cachedState == STARTING || cachedState == PALLET_LOADING))
                    ok = stove.write(ram, stoveStateAddr, 0);
            } else if (job.kind == BW_SETPOINT) {
                ok = (v >= 7 && v <= 30 && stove.write(eeprom, TempSetpointAddr, v));
                if (ok) setPointTemp = (float)v;
            } else if (job.kind == BW_FAN) {
                ok = (v <= 3 && setRoomFanSpeed(v));
                if (ok) roomFanSpeed = (int)v;
            } else if (job.kind == BW_FLAME) {
                ok = (v >= 1 && v <= 4 && setFlamePower(v));
                if (ok) flamePower = (int)v;
            } else if (job.kind == BW_BEEP) {
                ok = stove.write(eeprom, beepEnabledAddr, v ? 1 : 0);
                if (ok) cachedBeepEnabled = (bool)v;
            } else if (job.kind == BW_COMBUSTION) {
                setCombustionQuality(v);
                cachedCombustionQuality = v;
            } else if (job.kind == BW_PELLET) {
                ok = stove.write(eeprom, pelletQualityAddr, v);
                if (ok) cachedPelletQuality = v;
            } else if (job.kind == BW_ENERGY_SAVING_START) {
                ok = setEnergySavingStart((int8_t)v);
            } else if (job.kind == BW_ENERGY_SAVING_STOP) {
                ok = setEnergySavingStop((int)v);
            }
            bleLastStoveOpAt = millis();
            LOGI("ble", "write %s (value %u)", ok ? "applied" : "failed", v);
            bleForceNotifyPending = true;
            xSemaphoreGiveRecursive(stoveMutex);
        }
    }

    // Apply pending timer EEPROM write under mutex.
    if (blePendingTimerWrite.ready && !bleStoveUartInQuietWindow(now, 150) && stoveMutex &&
        xSemaphoreTakeRecursive(stoveMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
        CmdSource cs("Bluetooth app");
        blePendingTimerWrite.ready = false;
        int idx = blePendingTimerWrite.idx;
        const uint8_t* d = blePendingTimerWrite.data;
        byte base = timerBases[idx];
        stove.write(eeprom, base + TIMER_ONOFF_OFFSET,    d[0] ? 1 : 0);
        delay(20);
        stove.write(eeprom, base + TIMER_ONTIME_OFFSET,   d[1]);
        delay(20);
        stove.write(eeprom, base + TIMER_OFFTIME_OFFSET,  d[2]);
        delay(20);
        stove.write(eeprom, base + TIMER_SETPOINT_OFFSET, d[3]);
        delay(20);
        stove.write(eeprom, base + TIMER_FLAME_OFFSET,    d[4]);
        delay(20);
        stove.write(eeprom, base + TIMER_FAN_OFFSET,      d[5]);
        delay(20);
        stove.write(eeprom, base + TIMER_DOW_OFFSET,      d[6]);
        bleLastStoveOpAt = millis();
        LOGI("ble", "timer write applied (index %d)", idx);
        bleForceNotifyPending = true;
        xSemaphoreGiveRecursive(stoveMutex);
    }

    if (!bleClientConnected) return;

    if (now - bleLastUpdate < 2000) return;
    bleLastUpdate = now;
    unsigned long age = millis() - bleConnectedAt;
    bleUpdateAll();
    if (bleForceNotifyPending) {
        // iOS subscribes ~21 characteristics with 0.12 s stagger starting at t+1 s,
        // so the subscription window closes at ~t+3.5 s. Only fire the bulk notify
        // after that window to guarantee all characteristics are subscribed.
        // iOS also reads each characteristic on subscribe, so values aren't lost.
        if (age >= 4000) {
            bleForceNotifyPending = false;
            LOGD("ble", "+%lums sending all values to the client", age);
            bleForceNotifyAll();
        } else {
            LOGD("ble", "+%lums waiting before sending all values (connected %lu ms < 4000)", age, age);
            bleNotifyChanges();
        }
    } else {
        bleNotifyChanges();
    }
}
