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
// Called when an EEPROM write is left out because the register already holds the value.
typedef void (*StoveSkipFn)(uint8_t type, uint8_t address, uint8_t value);

class Stove
{
  public:
    static StoveMonitorFn monitor;   // nullptr = off
    static StoveWriteFn   writeHook; // nullptr = off
    static StoveSkipFn    skipHook;  // nullptr = off
    static StoveLogFn     logger;    // nullptr = Serial
    // Called before every transaction (read, probe, write) when set: lets the background reader
    // hand the bus over to a waiting command first (src/stoveCache.h). May give and take the lock.
    static void         (*beforeTx)();
    // Pins and polarity come from the board (src/board.h); invert = the RX and TX
    // levels inverted by the interface. The defaults are the ESP32 DevKit wiring.
    Stove(int uart_nr, int8_t rxPin = 16, int8_t txPin = 17, bool invert = false);
    // When the link is down (LINK_DOWN_AFTER transactions in a row failed after
    // all attempts) read() returns -1 and write() false at once, without touching
    // the bus. Only probe() still talks to the stove; one answer brings the link up.
    // EEPROM (type 0x20): the register is read first and not written when it already holds the
    // value (returns true): an EEPROM cell wears with every write, and clients such as Loxone
    // send their settings again and again. A failed read writes as before. RAM: always written
    // (commands such as the stove state).
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
    volatile uint32_t writesUnchanged = 0; // EEPROM writes left out: the value was already there
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