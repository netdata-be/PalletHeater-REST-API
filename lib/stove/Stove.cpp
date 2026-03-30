#include "Stove.h"

#define SERIAL_MODE SERIAL_8N2 // 8 data bits, parity none, 2 stop bits
#define RX_PIN 16
#define TX_PIN 17

// Shared debug buffer — defined in constants.h, accessed via extern
extern String stoveDebugBuffer;

static void appendDebugLog(const String& line)
{
    Serial.println(line);
    stoveDebugBuffer += line + "\n";
    // Keep buffer capped to avoid heap exhaustion
    if (stoveDebugBuffer.length() > 3000) {
        int cut = stoveDebugBuffer.indexOf('\n', stoveDebugBuffer.length() - 2000);
        stoveDebugBuffer = (cut >= 0) ? stoveDebugBuffer.substring(cut + 1) : "";
    }
}

Stove::Stove(int uart_nr)
{
    StoveSerial = new HardwareSerial(uart_nr);
    StoveSerial->setTimeout(50);
    StoveSerial->begin(1200, SERIAL_MODE, RX_PIN, TX_PIN, false, 256);
}

void Stove::setDebug(bool enabled)  { _debugEnabled = enabled; }
bool Stove::isDebugEnabled() const  { return _debugEnabled; }

void Stove::debugLog(const char* msg) { appendDebugLog(String(msg)); }

// ─────────────────────────────────────────────────────────────────
// READ
// ─────────────────────────────────────────────────────────────────

byte Stove::read(byte type, byte address)
{
    byte message[2] = {type, address};

    esp_task_wdt_reset();

    if (_debugEnabled) {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "READ  TX: { 0x%02X, 0x%02X }  (type=0x%02X addr=0x%02X)",
                 message[0], message[1], type, address);
        appendDebugLog(buf);
    }

    StoveSerial->flush();
    StoveSerial->write(message, sizeof(message));

    char stoveRxData[4];
    int received = StoveSerial->readBytes(stoveRxData, 4);

    if (received != 4) {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "READ  ERR: timeout — received %d/4 bytes", received);
        appendDebugLog(buf);
        esp_task_wdt_reset();
        return -1;
    }

    esp_task_wdt_reset();

    byte checksum        = stoveRxData[2];
    byte val             = stoveRxData[3];
    byte address_derived = checksum - val - type;

    if (_debugEnabled) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "READ  RX: echo={ 0x%02X, 0x%02X } resp={ 0x%02X, 0x%02X }  "
                 "checksum=0x%02X val=0x%02X addr_derived=0x%02X",
                 (uint8_t)stoveRxData[0], (uint8_t)stoveRxData[1],
                 (uint8_t)stoveRxData[2], (uint8_t)stoveRxData[3],
                 checksum, val, address_derived);
        appendDebugLog(buf);
    }

    if (address_derived == address) {
        if (_debugEnabled) {
            char buf[48];
            snprintf(buf, sizeof(buf), "READ  OK: val=0x%02X (%d)", val, val);
            appendDebugLog(buf);
        }
        delay(10);
        return val;
    }

    char buf[80];
    snprintf(buf, sizeof(buf),
             "READ  ERR: checksum mismatch — expected addr=0x%02X got 0x%02X",
             address, address_derived);
    appendDebugLog(buf);
    return -1;
}

// ─────────────────────────────────────────────────────────────────
// WRITE
// ─────────────────────────────────────────────────────────────────

bool Stove::write(byte type, byte address, byte value)
{
    const byte writeByte     = 0x80;
    uint8_t checksum_calc    = (writeByte + type + address + value) & 0xFF;
    byte firstbyte           = writeByte + type;
    byte message[4]          = {firstbyte, address, value, checksum_calc};

    if (_debugEnabled) {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "WRITE TX: { 0x%02X, 0x%02X, 0x%02X, 0x%02X }  "
                 "(type=0x%02X addr=0x%02X val=0x%02X chk=0x%02X)",
                 message[0], message[1], message[2], message[3],
                 type, address, value, checksum_calc);
        appendDebugLog(buf);
    }

    esp_task_wdt_reset();
    StoveSerial->write(message, sizeof(message));

    char stoveRxData[6];
    int received = StoveSerial->readBytes(stoveRxData, 6);

    if (received == 0) {
        appendDebugLog("WRITE ERR: timeout — received 0 bytes");
        esp_task_wdt_reset();
        return false;
    }

    esp_task_wdt_reset();

    byte checksum_resp   = stoveRxData[4];
    byte value_resp      = stoveRxData[5];
    uint8_t address_resp = (checksum_resp - writeByte - type - value_resp) & 0xFF;

    if (_debugEnabled) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "WRITE RX: echo={ 0x%02X,0x%02X,0x%02X,0x%02X } resp={ 0x%02X, 0x%02X }  "
                 "checksum=0x%02X val_resp=0x%02X addr_resp=0x%02X",
                 (uint8_t)stoveRxData[0], (uint8_t)stoveRxData[1],
                 (uint8_t)stoveRxData[2], (uint8_t)stoveRxData[3],
                 (uint8_t)stoveRxData[4], (uint8_t)stoveRxData[5],
                 checksum_resp, value_resp, address_resp);
        appendDebugLog(buf);
    }

    if ((address_resp == address) && (value_resp == value)) {
        if (_debugEnabled) appendDebugLog("WRITE OK: [SUCCESS]");
        return true;
    }

    char buf[96];
    snprintf(buf, sizeof(buf),
             "WRITE ERR: mismatch — addr expected=0x%02X got=0x%02X  "
             "val expected=0x%02X got=0x%02X",
             address, address_resp, value, value_resp);
    appendDebugLog(buf);
    return false;
}
