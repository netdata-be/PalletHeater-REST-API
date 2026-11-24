#include "Stove.h"

#define SERIAL_MODE SERIAL_8N2 // 8 data bits, parity none, 2 stop bits

// Shared debug buffer — defined in constants.h, accessed via extern
extern String stoveDebugBuffer;

static void appendDebugLog(const String& line)
{
    // error = failed for good (all attempts); a failed attempt that is retried is not an error
    if (Stove::logger) Stove::logger(line.indexOf("FAILED") >= 0, line.c_str());
    else Serial.println(line);
    stoveDebugBuffer += line + "\n";
    // Keep buffer capped to avoid heap exhaustion
    if (stoveDebugBuffer.length() > 3000) {
        int cut = stoveDebugBuffer.indexOf('\n', stoveDebugBuffer.length() - 2000);
        stoveDebugBuffer = (cut >= 0) ? stoveDebugBuffer.substring(cut + 1) : "";
    }
}

StoveMonitorFn Stove::monitor = nullptr;
StoveLogFn     Stove::logger  = nullptr;
StoveWriteFn   Stove::writeHook = nullptr;
void         (*Stove::beforeTx)() = nullptr;

Stove::Stove(int uart_nr, int8_t rxPin, int8_t txPin, bool invert)
{
    StoveSerial = new HardwareSerial(uart_nr);
    StoveSerial->begin(1200, SERIAL_MODE, rxPin, txPin, invert, 256);
}

void Stove::setDebug(bool enabled)  { _debugEnabled = enabled; }
bool Stove::isDebugEnabled() const  { return _debugEnabled; }

void Stove::debugLog(const char* msg) { appendDebugLog(String(msg)); }

// ─────────────────────────────────────────────────────────────────
// LOW LEVEL TRANSACTION
// ─────────────────────────────────────────────────────────────────
//
// The Micronova bus is single wire: everything we transmit is echoed back on
// RX, followed by the stove's 2 byte reply. At 1200 baud 8N2 one byte takes
// 11 bits = ~9.2 ms on the wire, so a read frame (2 echo + 2 reply) needs
// ~37 ms and a write frame (4 echo + 2 reply) ~55 ms, plus the stove's
// reaction time. The timeout is therefore derived from the frame length
// instead of a fixed value.

static const uint32_t BYTE_TIME_US      = 11UL * 1000000UL / 1200UL; // ~9167 µs
static const uint32_t STOVE_LATENCY_MS  = 60;   // margin for the stove to start answering
static const uint8_t  MAX_ATTEMPTS      = 3;
static const uint32_t RETRY_DELAY_MS    = 40;   // let the bus go idle before retrying

void Stove::drainRx()
{
    StoveSerial->flush(true);                // wait until our TX is on the wire
    while (StoveSerial->available()) StoveSerial->read();  // drop stale / late bytes
}

// Send `tx`, then wait for txLen echo bytes + replyLen reply bytes.
// Returns the number of bytes received (== txLen + replyLen on success).
int Stove::transact(const byte* tx, size_t txLen, byte* rx, size_t rxLen)
{
    drainRx();
    uint32_t timeoutMs = (rxLen * BYTE_TIME_US) / 1000UL + STOVE_LATENCY_MS;
    StoveSerial->setTimeout(timeoutMs);
    StoveSerial->write(tx, txLen);
    int received = StoveSerial->readBytes(rx, rxLen);
    esp_task_wdt_reset();
    return received;
}

// ─────────────────────────────────────────────────────────────────
// READ
// ─────────────────────────────────────────────────────────────────

// Link state: a transaction that failed after all attempts counts; LINK_DOWN_AFTER
// in a row and the link is down. Any answer brings it up again.
void Stove::linkResult(bool ok)
{
    if (ok) { failedInRow = 0; linkDown = false; return; }
    if (failedInRow < 255) failedInRow++;
    if (failedInRow >= LINK_DOWN_AFTER) linkDown = true;
}

// Why did an attempt fail? The bus is one wire: the controller always hears its
// own bytes back (echo), also when the stove does not answer. No (correct) echo
// means the bus itself is not connected (cable / interface), not a stove timeout.
static void failReason(char* out, size_t sz, const byte* tx, size_t txLen, const byte* rx, int received, size_t full)
{
    int n = received < 0 ? 0 : received;
    size_t echoN = (size_t)n < txLen ? (size_t)n : txLen;
    bool echoOk = (size_t)n >= txLen && memcmp(rx, tx, txLen) == 0;
    if (n == 0) { snprintf(out, sz, "nothing received, no echo of TX: bus not connected (cable / interface?)"); return; }
    if (!echoOk && memcmp(rx, tx, echoN) != 0) {
        char got[24] = ""; for (int i = 0; i < n && i < 6; i++) snprintf(got + strlen(got), sizeof(got) - strlen(got), "%s%02X", i ? " " : "", rx[i]);
        snprintf(out, sz, "no echo of TX (received %s): bus not connected or line stuck (cable / interface?)", got);
        return;
    }
    if ((size_t)n < txLen) { snprintf(out, sz, "echo incomplete (%d of %u bytes): bus not connected (cable / interface?)", n, (unsigned)txLen); return; }
    if ((size_t)n < full) { snprintf(out, sz, "echo OK, the stove did not answer (%d of %u bytes)", n, (unsigned)full); return; }
    snprintf(out, sz, "echo OK, answer with a wrong checksum (noise on the line?)");
}

int Stove::read(byte type, byte address)
{
    if (linkDown) { skipped++; return -1; }
    if (beforeTx) beforeTx();
    return readTx(type, address);
}

int Stove::probe(byte type, byte address)
{
    if (beforeTx) beforeTx();
    return readTx(type, address);
}

int Stove::readTx(byte type, byte address)
{
    const byte message[2] = {type, address};
    char buf[160];
    char lastWhy[96] = "";
    transactions++;

    for (uint8_t attempt = 1; attempt <= MAX_ATTEMPTS; attempt++)
    {
        if (attempt > 1) { retries++; delay(RETRY_DELAY_MS); }
        esp_task_wdt_reset();

        if (_debugEnabled) {
            snprintf(buf, sizeof(buf), "READ  TX: { 0x%02X, 0x%02X }  attempt %u", type, address, attempt);
            appendDebugLog(buf);
        }

        byte rx[4] = {0};
        int received = transact(message, sizeof(message), rx, sizeof(rx));
        if (received != (int)sizeof(rx)) {
            char why[96];
            failReason(why, sizeof(why), message, sizeof(message), rx, received, sizeof(rx));
            snprintf(buf, sizeof(buf), "READ  ERR: %s 0x%02X attempt %u/%u: %s", type == 0x20 ? "EEPROM" : "RAM", address, attempt, MAX_ATTEMPTS, why);
            appendDebugLog(buf);
            strncpy(lastWhy, why, sizeof(lastWhy) - 1);
            if (monitor) monitor(false, type, address, 0, false, attempt, MAX_ATTEMPTS, message, sizeof(message), rx, received > 0 ? (uint8_t)received : 0);
            continue;
        }
        // Echo is informational only; the checksum below is the real validation.
        if (_debugEnabled && (rx[0] != message[0] || rx[1] != message[1])) {
            snprintf(buf, sizeof(buf), "READ  WARN: echo differs { 0x%02X, 0x%02X }", rx[0], rx[1]);
            appendDebugLog(buf);
        }

        byte checksum        = rx[2];
        byte val             = rx[3];
        byte address_derived = checksum - val - type;

        if (_debugEnabled) {
            snprintf(buf, sizeof(buf), "READ  RX: resp={ 0x%02X, 0x%02X } val=0x%02X addr_derived=0x%02X",
                     checksum, val, val, address_derived);
            appendDebugLog(buf);
        }

        if (address_derived == address) {
            if (_debugEnabled) {
                snprintf(buf, sizeof(buf), "READ  OK: val=0x%02X (%d)", val, val);
                appendDebugLog(buf);
            }
            if (monitor) monitor(false, type, address, val, true, attempt, MAX_ATTEMPTS, message, sizeof(message), rx, sizeof(rx));
            linkResult(true);
            delay(10);
            return val;
        }

        {
            char why[96];
            failReason(why, sizeof(why), message, sizeof(message), rx, received, sizeof(rx));
            snprintf(buf, sizeof(buf), "READ  ERR: %s 0x%02X attempt %u/%u: %s", type == 0x20 ? "EEPROM" : "RAM", address, attempt, MAX_ATTEMPTS, why);
            appendDebugLog(buf);
            strncpy(lastWhy, why, sizeof(lastWhy) - 1);
        }
        if (monitor) monitor(false, type, address, val, false, attempt, MAX_ATTEMPTS, message, sizeof(message), rx, sizeof(rx));
    }

    readErrors++;
    linkResult(false);
    snprintf(buf, sizeof(buf), "READ FAILED: %s 0x%02X after %u attempts - %s",
             type == 0x20 ? "EEPROM" : "RAM", address, MAX_ATTEMPTS, lastWhy);
    appendDebugLog(buf);
    return -1;
}

// ─────────────────────────────────────────────────────────────────
// WRITE
// ─────────────────────────────────────────────────────────────────
// Writes are idempotent (same value to the same address), so retrying is safe.

bool Stove::write(byte type, byte address, byte value)
{
    if (linkDown) { skipped++; return false; }
    if (beforeTx) beforeTx();
    const byte writeByte     = 0x80;
    uint8_t checksum_calc    = (writeByte + type + address + value) & 0xFF;
    const byte message[4]    = {(byte)(writeByte + type), address, value, checksum_calc};
    char buf[160];
    char lastWhy[96] = "";
    transactions++;

    for (uint8_t attempt = 1; attempt <= MAX_ATTEMPTS; attempt++)
    {
        if (attempt > 1) { retries++; delay(RETRY_DELAY_MS); }
        esp_task_wdt_reset();

        if (_debugEnabled) {
            snprintf(buf, sizeof(buf), "WRITE TX: { 0x%02X, 0x%02X, 0x%02X, 0x%02X }  attempt %u",
                     message[0], message[1], message[2], message[3], attempt);
            appendDebugLog(buf);
        }

        byte rx[6] = {0};
        int received = transact(message, sizeof(message), rx, sizeof(rx));
        if (received != (int)sizeof(rx)) {
            char why[96];
            failReason(why, sizeof(why), message, sizeof(message), rx, received, sizeof(rx));
            snprintf(buf, sizeof(buf), "WRITE ERR: %s 0x%02X = %u attempt %u/%u: %s", type == 0x20 ? "EEPROM" : "RAM", address, value, attempt, MAX_ATTEMPTS, why);
            appendDebugLog(buf);
            strncpy(lastWhy, why, sizeof(lastWhy) - 1);
            if (monitor) monitor(true, type, address, value, false, attempt, MAX_ATTEMPTS, message, sizeof(message), rx, received > 0 ? (uint8_t)received : 0);
            continue;
        }
        // Echo is informational only; the address/value in the reply is the real validation.
        if (_debugEnabled && memcmp(rx, message, sizeof(message)) != 0)
            appendDebugLog("WRITE WARN: echo differs from transmitted frame");

        byte checksum_resp   = rx[4];
        byte value_resp      = rx[5];
        uint8_t address_resp = (checksum_resp - writeByte - type - value_resp) & 0xFF;

        if (_debugEnabled) {
            snprintf(buf, sizeof(buf), "WRITE RX: resp={ 0x%02X, 0x%02X } val_resp=0x%02X addr_resp=0x%02X",
                     checksum_resp, value_resp, value_resp, address_resp);
            appendDebugLog(buf);
        }

        if (address_resp == address && value_resp == value) {
            if (_debugEnabled) appendDebugLog("WRITE OK: [SUCCESS]");
            if (monitor) monitor(true, type, address, value, true, attempt, MAX_ATTEMPTS, message, sizeof(message), rx, sizeof(rx));
            if (writeHook) writeHook(type, address, value, true);
            linkResult(true);
            return true;
        }

        snprintf(buf, sizeof(buf), "WRITE ERR: %s 0x%02X = %u attempt %u/%u: the stove confirmed address 0x%02X value %u",
                 type == 0x20 ? "EEPROM" : "RAM", address, value, attempt, MAX_ATTEMPTS, address_resp, value_resp);
        appendDebugLog(buf);
        snprintf(lastWhy, sizeof(lastWhy), "the stove confirmed another address / value");
        if (monitor) monitor(true, type, address, value, false, attempt, MAX_ATTEMPTS, message, sizeof(message), rx, sizeof(rx));
    }

    writeErrors++;
    linkResult(false);
    snprintf(buf, sizeof(buf), "WRITE FAILED: %s 0x%02X = %u not confirmed after %u attempts - %s",
             type == 0x20 ? "EEPROM" : "RAM", address, value, MAX_ATTEMPTS, lastWhy);
    appendDebugLog(buf);
    if (writeHook) writeHook(type, address, value, false);
    return false;
}
