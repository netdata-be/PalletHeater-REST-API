/*
  stoveSerial.h - Library for interacting with the serial line of micronova stoves.
  Created by Wouter D'Haeseleer, April 9, 2021.
*/

#ifndef Stove_h
#define Stove_h

#ifdef ESP32
#include "esp_log.h"
#include <esp_task_wdt.h>
#endif
#include "Arduino.h"
#include <HardwareSerial.h>


// Called after every bus transaction (also failed ones) when set: the live bus
// monitor of the web app. Must be fast and must not touch the bus.
//   isWrite, type (0x00 RAM / 0x20 EEPROM), address, value (read result or the
//   value written), ok, attempt (1..of) and the raw bytes of that attempt. Called
//   for every attempt, so a retried transaction gives one call per try:
//   tx = sent (read: type, address; write: 0x80 + type, address, value, checksum)
//   rx = received (the bus is one wire: first the echo of tx, then the stove's
//        2 byte answer: checksum, value). rxLen < full length = timeout.
typedef void (*StoveMonitorFn)(bool isWrite, uint8_t type, uint8_t address, uint8_t value, bool ok, uint8_t attempt, uint8_t of,
                               const uint8_t* tx, uint8_t txLen, const uint8_t* rx, uint8_t rxLen);

// Receives the library's log lines (errors, and the frames when debug is on);
// without it they go to Serial. error = a failed attempt.
typedef void (*StoveLogFn)(bool error, const char* line);

// Called after every write (ok or failed for good), also when the bus monitor is
// off: logs who changed what (liveDebug.h).
typedef void (*StoveWriteFn)(uint8_t type, uint8_t address, uint8_t value, bool ok);

class Stove
{
  public:
    static StoveMonitorFn monitor;   // nullptr = off
    static StoveWriteFn   writeHook; // nullptr = off
    static StoveLogFn     logger;    // nullptr = Serial
    Stove(int uart_nr);
    // When the link is down (LINK_DOWN_AFTER transactions in a row failed after
    // all attempts) read() returns -1 and write() false at once, without touching
    // the bus. Only probe() still talks to the stove; one answer brings the link up.
    bool write(byte type, byte address, byte value);
    // Returns the register value (0-255), or -1 on timeout / checksum error.
    int read(byte type, byte address);
    int probe(byte type, byte address);   // a read that is tried even when the link is down
    volatile bool linkDown = false;       // the stove does not answer
    volatile uint32_t skipped = 0;        // reads / writes refused because the link was down
    static const uint8_t LINK_DOWN_AFTER = 2;
    void setDebug(bool enabled);
    bool isDebugEnabled() const;

    // Diagnostics (exposed on /metrics)
    volatile uint32_t readErrors  = 0;  // reads that failed after all retries
    volatile uint32_t writeErrors = 0;  // writes that failed after all retries
    volatile uint32_t retries     = 0;  // individual retry attempts
    volatile uint32_t transactions = 0; // reads + writes (not counting retries)
  private:
    HardwareSerial* StoveSerial;
    void drainRx();
    int  readTx(byte type, byte address);
    void linkResult(bool ok);
    uint8_t failedInRow = 0;
    int  transact(const byte* tx, size_t txLen, byte* rx, size_t rxLen);
    bool _debugEnabled = false;
    void debugLog(const char* msg);
};
#endif