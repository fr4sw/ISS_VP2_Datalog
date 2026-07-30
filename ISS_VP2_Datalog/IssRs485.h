// ============================================================================
// Fichier   : IssRs485.h
// Rôle      : Reception des trames ISS filaire. NE DECODE PAS la trame,
//             se limite a l'assemblage des octets recus (règle 20).
// Fonctions : begin()    - initialise le port UART dedie.
//             update()   - assemble les octets recus.
//             getFrame() - renvoie true et remplit rawFrame si une trame
//                          complete est disponible.
// Référence : Protocole Davis Vantage Pro2 filaire, trame 6 octets sans CRC
//             (retour d'experience communautaire, wxforum.net/index.php?topic=32706.0)
// Particularité : Reception RS485 via Serial1, reconfigure sur NOS broches
//             via setPins() avant begin() (methode disponible sur le
//             core arduino-nRF5 / Seeeduino).
// Référence : github.com/sandeepmistry/arduino-nRF5, Uart::setPins()
// ============================================================================
#pragma once
#include <Arduino.h>
#include "BoardConfig.h"
#include "Config.h"
#include "IssCommon.h"

class IssRs485
{
public:
    void begin();
    void update();
    bool getFrame(IssRawFrame &rawFrame);
    unsigned long lastByteMicros;

private:
    uint8_t frameBuffer[ISS_RS485_FRAME_LENGTH];
    uint8_t frameLength;
    bool    frameReady;
};

extern IssRs485 issRs485;
