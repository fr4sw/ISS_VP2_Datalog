// ============================================================================
// Fichier : ISS_VP2_Datalog.ino
// Rôle    : Enchainement des traitements uniquement (règle 5).
//           Toute logique metier reside dans les modules dedies.
// ============================================================================
#include "Config.h"
#include "BoardConfig.h"
#include "TimeManager.h"
#include "Power.h"
#include "Params.h"
#include "SerialConsole.h"
#include "EventLog.h"
#include "LocationLog.h"
#include "DataLogger.h"
#if USE_BLE
    #include "BleLink.h"
#endif

#if ISS_WIRELESS
    #include "IssWireless.h"
#endif
#if ISS_RS485
    #include "IssRs485.h"
#endif
#if USE_WIFI_IHM
    #include "WifiPortal.h"
#endif
#if USE_BME680
    #include "BmeIndoor.h"
#endif

void setup()
{
    Serial.begin(115200);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

// Remarque  : Attente de l'enumeration USB CDC avant les premiers
//             messages de debug. Sans cette attente, les messages
//             envoyes avant l'ouverture du moniteur serie par l'hote
//             sont perdus (comportement normal de l'USB CDC virtuel,
//             different d'un UART physique).
// ============================================================================
    unsigned long usbWaitStart = millis();
    while ((!Serial) && (millis() - usbWaitStart < 3000))
    {
        delay(10);
    }

    Serial.println(F("[Main] Demarrage ISS_VP2_Datalog"));

    power.begin();
    serialConsoleBegin();

    // Params et EventLog ont besoin de la carte SD : leur begin() appelle
    // HalPins::beginSdCard() (idempotent), qui l'initialise si necessaire.
    params.begin();

    // timeManager.begin() DOIT precede tout appel a logEvent() (y compris
    // le tout premier ci-dessous) : logEvent() appelle en interne
    // timeManager.now() pour horodater sa ligne, qui echoue si le RTC/GPS
    // n'a pas encore ete initialise (bug releve : le tout premier
    // logEvent() de la session affichait a tort "[TimeManager] Erreur :
    // RTC indisponible", alors que le RTC etait en realite disponible -
    // simplement pas encore initialise a ce point du demarrage).
    timeManager.begin();

    logEventBegin();
    locationLogBegin();
    logEvent(F("Demarrage ISS_VP2_Datalog"));

    dataLogger.begin();

#if ISS_WIRELESS
    issWireless.begin();
#endif
#if ISS_RS485
    issRs485.begin();
#endif
#if USE_WIFI_IHM
    wifiPortal.begin();
#endif
#if USE_BME680
    bmeIndoor.begin();
#endif
#if USE_BLE
    if (params.getBleEnabled() == true)
    {
        bleLinkBegin();
    }
#endif
}

#if DEBUG_RAW_FRAMES
// Affiche une trame brute en hexadecimal, prefixee comme demande. Factorise
// ici car appelee depuis deux endroits de loop() (trame decodee avec succes
// -> prefixe avec son numero dans le creneau ; trame non decodable -> pas
// de numero, voir loop()).
static void printRawFrame(const char prefix[], const IssRawFrame &rawFrame)
{
    Serial.print(prefix);
    for (uint8_t index = 0; index < rawFrame.length; index++)
    {
        if (rawFrame.bytes[index] < 0x10)
        {
            Serial.print(F("0"));
        }
        Serial.print(rawFrame.bytes[index], HEX);
        Serial.print(F(" "));
    }
    Serial.println();
    // Voir DataLogger.cpp (meme raisonnement) : sans ce flush(), le volume
    // de lignes DEBUG par trame peut deborder le tampon CDC-USB et perdre
    // silencieusement le debut de certaines lignes (bug releve : "[Main]
    // Trame brute #30 : ..." devenu juste "#30 : ...").
    if (Serial) { Serial.flush(); }   // jamais de flush() sans hote connecte (voir DataLogger.cpp, premiere occurrence)
}
#endif

void loop()
{
    serialConsoleUpdate();
    timeManager.update();
#if USE_BLE
    if (params.getBleEnabled() == true)
    {
        bleLinkUpdate();
    }
#endif

#if USE_BME680
    bmeIndoor.update();
    IndoorData indoorData;
    bool indoorDataValid = bmeIndoor.getData(indoorData);
    if (indoorDataValid == true)
    {
        dataLogger.updateIndoorData(indoorData);
    }
#endif

    IssRawFrame rawFrame;
    bool frameReceived = false;

#if ISS_RS485
    issRs485.update();
    frameReceived = issRs485.getFrame(rawFrame);
#endif
#if ISS_WIRELESS
    issWireless.update();
    frameReceived = issWireless.getFrame(rawFrame);
#endif

    if (frameReceived == true)
    {
        IssData decodedData;
        bool decodeSuccess = decodeFrame(rawFrame, decodedData);
        if (decodeSuccess == true)
        {
            dataLogger.logRecord(decodedData);

#if DEBUG_RAW_FRAMES
            // Imprime APRES logRecord() (pas avant, comme precedemment) :
            // c'est logRecord() qui determine le numero de cette trame
            // dans le creneau de 5 min en cours (voir
            // DataLogger::checkReceptionSlotBoundary()), donc ce numero
            // n'est connu qu'une fois logRecord() revenu. Fusionne ce qui
            // etait avant deux lignes distinctes ("[DataLogger] Trame #N
            // du creneau en cours" + "[Main] Trame brute : ...") en une
            // seule, pour correler immediatement le numero et le contenu
            // de la trame sans avoir a rapprocher deux lignes a l'oeil.
            // Tampon de 32 octets : "[Main] Trame brute #" (20) + jusqu'a
            // 5 chiffres (uint16_t, max 65535) + " : " (3) + terminateur.
            // Avec un tampon trop juste (bug releve par l'utilisateur : le
            // "24" precedent tronquait AVANT le separateur " : ", collant
            // le numero directement aux octets hexa sans espace - illisible
            // a l'oeil, semblait montrer "aucun numero").
            char prefix[32];
            snprintf(prefix, sizeof(prefix), "[Main] Trame brute #%u : ", dataLogger.getLastFrameNumberInSlot());
            printRawFrame(prefix, rawFrame);
#endif
        }
        else
        {
            Serial.println(F("[Main] Erreur : trame ISS non decodable"));
#if DEBUG_RAW_FRAMES
            printRawFrame("[Main] Trame brute (non decodable) : ", rawFrame);
#endif
        }
    }

    dataLogger.update();
}
