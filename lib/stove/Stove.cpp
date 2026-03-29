#include "Stove.h"

#define SERIAL_MODE SERIAL_8N2 // 8 data bits, parity none, 2 stop bits
#define RX_PIN 16
#define TX_PIN 17

#define STOVE_DEBUG 0  // Set to 1 to enable verbose serial frame logging


Stove::Stove(int uart_nr)
{
    StoveSerial = new HardwareSerial(uart_nr);
    StoveSerial->setTimeout(50);
    StoveSerial->begin(1200, SERIAL_MODE, RX_PIN, TX_PIN, false, 256);
}

byte Stove::read(byte type, byte address)
{
    byte message[2] = {type, address};

    esp_task_wdt_reset();
#if STOVE_DEBUG
    Serial.printf("Serial >>> { 0x%02X, 0x%02X }\n", message[0], message[1]);
#endif

    StoveSerial->flush();
    StoveSerial->write(message, sizeof(message));

    // We expect a 2 byte answer back but since we see our own sent data as echo we need to read 4 bytes
    char stoveRxData[4];

    if (StoveSerial->readBytes(stoveRxData, 4) == 4)
    {
        esp_task_wdt_reset();

        byte checksum = stoveRxData[2];
        byte val = stoveRxData[3];
        byte address_response = checksum - val - type;
        if (address_response == address)
        {
            delay(10);
            return val;
        }
        else
        {
            log_e("Checksum does not match");
            return -1;
        }
    }
    esp_task_wdt_reset();
    log_e("Did not receive 4 bytes - returning -1");
    return -1;
}

bool Stove::write(byte type, byte address, byte value)
{

    const byte writeByte = 0x80;
    uint8_t checksum_calc = (writeByte + type + address + value) & 0xFF;
    byte firstbyte = writeByte + type;
    byte message[4] = {firstbyte, address, value, checksum_calc};

    esp_task_wdt_reset();
    StoveSerial->write(message, sizeof(message));

    // Bytes 0-3 are echo from our TX line; bytes 4-5 are the stove's response
    char stoveRxData[6];
    if (StoveSerial->readBytes(stoveRxData, 6) != 0)
    {
        esp_task_wdt_reset();
        byte checksum = stoveRxData[4];
        byte value_response = stoveRxData[5];
#if STOVE_DEBUG
        Serial.printf("Received back: checksum=0x%02X Value=0x%02X\n", checksum, value_response);
#endif

        uint8_t address_response = (checksum - writeByte - type - value_response) & 0xFF;

        if ((address_response == address) && (value_response == value))
        {
#if STOVE_DEBUG
            Serial.printf("  [SUCCESS]\n");
#endif
            return true;
        }
        else
        {
#if STOVE_DEBUG
            Serial.printf("  [FAIL]\n");
#endif
            return false;
        }
    }
    log_e("Did not receive 6 bytes - returning false");
    esp_task_wdt_reset();
    return false;
}