// ============================================================================
// Fichier   : IssRs485.cpp
// ============================================================================
#include <Arduino.h>
#include "IssRs485.h"
#include "BoardConfig.h"
#include "HalPins.h"

IssRs485 issRs485;

void IssRs485::begin()
{
    configureRs485Pins();
#if defined(ARDUINO_ARCH_NRF52)
    Serial1.begin(UART_RS485_BAUD);
#elif defined(ARDUINO_ARCH_ESP32)
    Serial1.begin(UART_RS485_BAUD, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
#endif
    frameLength = 0;
    frameReady = false;
}

void IssRs485::update()
{
    // Resynchronisation : si une trame est en cours d'assemblage
    // (frameLength > 0) et qu'aucun octet n'est arrive depuis plus de
    // ISS_RS485_RESYNC_TIMEOUT_US, on l'abandonne. Le prochain octet
    // recu redemarre l'assemblage depuis la position 0.
    if (frameLength > 0)
    {
        unsigned long elapsedMicros = micros() - lastByteMicros;
        if (elapsedMicros > ISS_RS485_RESYNC_TIMEOUT_US)
        {
            Serial.print(F("[IssRs485] Resynchronisation : trame partielle abandonnee, octets recus = "));
            Serial.println(frameLength);
            frameLength = 0;
        }
    }

  
    while (Serial1.available() > 0)
    {
        uint8_t receivedByte = Serial1.read();
        lastByteMicros = micros();
        
        frameBuffer[frameLength] = receivedByte;
        frameLength = frameLength + 1;

        if (frameLength == ISS_RS485_FRAME_LENGTH)
        {
            frameReady = true;
            frameLength = 0;
        }
    }
}

bool IssRs485::getFrame(IssRawFrame &rawFrame)
{
    if (frameReady == false)
    {
        return false;
    }
    for (uint8_t index = 0; index < ISS_RS485_FRAME_LENGTH; index++ )
    {
        rawFrame.bytes[index] = frameBuffer[index];
    }
    rawFrame.length = ISS_RS485_FRAME_LENGTH;
    rawFrame.hasCrc = false;
    rawFrame.hasCrc = true;   // teste : false -> true, pour activer checkCRC()
    
    frameReady = false;
    
    return true;
}
