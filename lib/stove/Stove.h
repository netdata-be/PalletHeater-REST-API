/*
  stoveSerial.h - Library for interacting with the serial line of micronova stoves.
  Created by Wouter D'Haeseleer, April 9, 2021.
*/

#ifndef Stove_h
#define Stove_h

#include "Arduino.h"
#include <HardwareSerial.h>

class Stove
{
  public:
    Stove(int uart_nr);
    bool write(byte type, byte address, byte value);
    byte read(byte type, byte address);
  private:
    HardwareSerial* StoveSerial;
};
#endif