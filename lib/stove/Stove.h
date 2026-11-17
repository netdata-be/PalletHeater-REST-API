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


class Stove
{
  public:
    Stove(int uart_nr);
    bool write(byte type, byte address, byte value);
    // Returns the register value (0-255), or -1 on timeout / checksum error.
    int read(byte type, byte address);
    void setDebug(bool enabled);
    bool isDebugEnabled() const;

    // Diagnostics (exposed on /metrics)
    volatile uint32_t readErrors  = 0;  // reads that failed after all retries
    volatile uint32_t writeErrors = 0;  // writes that failed after all retries
    volatile uint32_t retries     = 0;  // individual retry attempts
  private:
    HardwareSerial* StoveSerial;
    void drainRx();
    int  transact(const byte* tx, size_t txLen, byte* rx, size_t rxLen);
    bool _debugEnabled = false;
    void debugLog(const char* msg);
};
#endif