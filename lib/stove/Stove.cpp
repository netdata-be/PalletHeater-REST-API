#include "Stove.h"

#define SERIAL_MODE SERIAL_8N2 // 8 data bits, parity none, 2 stop bits
#define RX_PIN 16
#define TX_PIN 17


Stove::Stove(int uart_nr)
{
    StoveSerial = new HardwareSerial(uart_nr);
    StoveSerial->begin(1200, SERIAL_MODE, RX_PIN, TX_PIN, false, 256);
}

byte Stove::read(byte type, byte address)
{
    byte message[2] = {type, address};

    Serial.printf("Serial >>> { 0x%02X, 0x%02X }\n", message[0], message[1]);

    StoveSerial->flush();
    StoveSerial->write(message, sizeof(message));

    // Let's check the reply of the stove
    // We expect a 2 byte answer back but since we see our own sended data as echo back we need to read 4 bytes
    char stoveRxData[4];
    if (StoveSerial->readBytes(stoveRxData, 4) == 4)
    {
        Serial.printf("Serial <<< { 0x%02X, 0x%02X, 0x%02X, 0x%02X }\n", stoveRxData[0], stoveRxData[1], stoveRxData[2], stoveRxData[3]);

        byte checksum = stoveRxData[2];
        byte val = stoveRxData[3];
        byte address_response = checksum - val - type;
        Serial.printf("Serial === %i\n", int(val));
        if (address_response == address)
        {
            Serial.printf("Serial === %i\n", int(val));
            delay(10);
            return val;
        }
        else
        {
            log_e("Checksum does not match");
            return -1;
        }
    }
    return -1;
}

bool Stove::write(byte type, byte address, byte value)
{

    const byte writeByte = 0x80;
    uint8_t checksum_calc = (writeByte + type + address + value) & 0xFF;
    byte firstbyte = writeByte + type;
    byte message[4] = {firstbyte, address, value, checksum_calc};

    Serial.printf("Sending to Stove = { 0x%02X, 0x%02X, 0x%02X, 0x%02X}\n", message[0], message[1], message[2], message[3]);

    StoveSerial->write(message, sizeof(message));

    // Let's check the reply of the stove
    // We expect a 2 byte answer back but since we see our own sended data as echo back we need to read 6 bytes
    char stoveRxData[6];
    if (StoveSerial->readBytes(stoveRxData, 6) != 0)
    {
        Serial.printf("Received from Stove = { 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}\n", stoveRxData[0], stoveRxData[1], stoveRxData[2], stoveRxData[3], stoveRxData[4], stoveRxData[5]);
        // Bytes 0-3 are just echo from our TX line
        byte checksum = stoveRxData[4];
        byte value_response = stoveRxData[5];
        Serial.printf("Received back: checksum=0x%02X Value=0x%02X\n", checksum, value_response);

        uint8_t address_response = (checksum - writeByte - type - value_response) & 0xFF;

        if ((address_response == address) & (value_response == value))
        {
            Serial.printf("  [SUCESS]\n");
            return true;
        }
        else
        {
            Serial.printf("  [FAIL]\n");
            return false;
        }
    }
    return false;
}