// ============================================================================
// Fichier   : Params.h
// Rôle      : Parametres modifiables SANS reprogrammer (règle : contrairement
//             a Config.h, qui fixe les valeurs par defaut a la compilation).
//             Persistes dans un fichier texte sur la carte SD
//             (Config.h : PARAMS_FILE_NAME), lisible/modifiable directement
//             depuis un PC (carte retiree ou montee en mass-storage), et
//             modifiable a chaud via SerialConsole (moniteur serie USB).
//             Si la carte SD est absente ou le fichier introuvable, les
//             valeurs par defaut de Config.h sont utilisees (fonctionnement
//             degrade mais jamais bloquant).
// Fonctions : begin()                     - charge les parametres (SD, sinon
//                                           defauts Config.h). A appeler
//                                           apres HalPins::beginSdCard().
//             save()                      - reecrit le fichier de parametres
//                                           avec les valeurs courantes.
//             getTimezoneOffsetHours()    - decalage local par rapport a
//                                           l'UTC, en heures entieres (ex :
//                                           +4 pour La Reunion). Les fuseaux
//                                           a la demi-heure ne sont pas
//                                           geres (cas rare, voir Config.h).
//             setTimezoneOffsetHours()
//             getLogWriteIntervalMs()     - periode d'ecriture CSV ; 0 =
//                                           ecriture a chaque trame recue.
//             setLogWriteIntervalMs()
//             getGpsBaudRate()            - debit UART du GPS (bauds).
//             setGpsBaudRate()
//             getMeshBaudRate()           - debit UART du lien Meshtastic
//                                           (bauds), meme port physique que
//                                           le GPS (voir SharedUart.h).
//             setMeshBaudRate()
//             getGpsMinSatellites()       - nombre minimum de satellites
//                                           exige pour accepter un point
//                                           GPS (date/heure ET position),
//                                           voir Config.h : GPS_MINIMUM_SATELLITES
//                                           pour l'indication de precision.
//             setGpsMinSatellites()
//             getDataloggerUtc()          - true = horodatage CSV/EVENTS.LOG
//                                           en UTC, false (defaut) = heure
//                                           locale (TZ, voir ci-dessus).
//             setDataloggerUtc()
//             getMeshUtc()                - true = horodatage transmis sur
//                                           le mesh en UTC, false (defaut) =
//                                           heure locale. NE CONCERNE PAS le
//                                           champ Telemetry.time actuel
//                                           (fixed32 UTC impose par le
//                                           protocole Meshtastic, voir
//                                           MeshtasticTelemetry.h) : reserve
//                                           a un futur canal texte/lisible.
//             setMeshUtc()
// Format fichier PARAMS_FILE_NAME : une ligne par parametre, "CLE=VALEUR"
//             (nombres entiers uniquement, pas d'espaces), lignes commencant
//             par '#' ignorees (commentaires). Exemple :
//                 # Fuseau horaire de La Reunion (UTC+4)
//                 TZ=4
//                 LOGINTERVAL=30000
//                 GPSBAUD=9600
//                 MESHBAUD=38400
//                 GPSMINSAT=4
//                 DATALOGGERUTC=0
//                 MESHUTC=0
// ============================================================================
#pragma once
#include <Arduino.h>

class Params
{
public:
    void begin();
    void save();

    int8_t   getTimezoneOffsetHours() const;
    void     setTimezoneOffsetHours(int8_t hours);

    uint32_t getLogWriteIntervalMs() const;
    void     setLogWriteIntervalMs(uint32_t intervalMs);

    uint32_t getGpsBaudRate() const;
    void     setGpsBaudRate(uint32_t baudRate);

    uint32_t getMeshBaudRate() const;
    void     setMeshBaudRate(uint32_t baudRate);

    uint8_t  getGpsMinSatellites() const;
    void     setGpsMinSatellites(uint8_t minSatellites);

    bool     getDataloggerUtc() const;
    void     setDataloggerUtc(bool useUtc);

    bool     getMeshUtc() const;
    void     setMeshUtc(bool useUtc);

    // Intervalle moyen mesure entre deux trames ISS consecutives (ms),
    // recalcule empiriquement a chaque creneau de 5 min (voir
    // DataLogger::checkReceptionSlotBoundary()) : remplace la formule
    // theorique Davis (issSecondsPerPacket(), IssCommon.h) comme base du
    // calcul du taux de reception, celle-ci s'etant averee ne pas
    // correspondre au comportement reel observe sur le materiel (~2,56s/
    // trame mesures, contre 2,5s attendus pour stationId=0). Mis a jour en
    // RAM a chaque creneau (pas d'ecriture SD automatique - règle 26, voir
    // aussi LOG_WRITE_INTERVAL_MS) : n'est persiste sur PARAMS_FILE_NAME
    // qu'au prochain SAVE explicite, pour eviter de "reapprendre" de zero
    // a chaque redemarrage si l'utilisateur choisit de le figer. 0 = pas
    // encore mesure (repli sur issSecondsPerPacket() au tout premier
    // creneau apres un demarrage a froid).
    uint32_t getIssAverageFrameIntervalMs() const;
    void     setIssAverageFrameIntervalMs(uint32_t intervalMs);

    // true (defaut) = annonce BLE active au demarrage (voir BleLink.h),
    // false = BLE jamais initialise. Pris en compte uniquement au
    // demarrage (ISS_VP2_Datalog.ino) : changer ce parametre a chaud via
    // la console necessite un redemarrage pour prendre effet, la
    // bibliotheque Bluefruit n'etant pas concue ici pour un arret/
    // redemarrage complet a la volee (règle 26 : pas de complexite
    // suplementaire pour un besoin non exprime - a etendre si un vrai
    // besoin de bascule a chaud apparait).
    bool     getBleEnabled() const;
    void     setBleEnabled(bool enabled);

    // Delai (ms) laisse au T114 pour demarrer sa pile logicielle apres
    // mise sous tension, avant de lui envoyer la poignee de main - voir
    // Config.h : MESH_POWERON_SETTLE_MS_DEFAULT pour la valeur par defaut
    // et le detail du compromis.
    uint32_t getMeshPowerOnSettleMs() const;
    void     setMeshPowerOnSettleMs(uint32_t settleMs);

    // true = n'envoie PAS ToRadio.want_config_id avant la telemetrie
    // (experimental - voir MeshLink.cpp : permet de verifier si le T114
    // accepte un ToRadio.packet direct sans poignee de main prealable, ce
    // qui eviterait le dump de configuration complet declenche par
    // want_config_id). false (defaut) = comportement habituel.
    bool     getMeshSkipHandshake() const;
    void     setMeshSkipHandshake(bool skip);

private:
    void load();

    int8_t   timezoneOffsetHours;
    uint32_t logWriteIntervalMs;
    uint32_t gpsBaudRate;
    uint32_t meshBaudRate;
    uint8_t  gpsMinSatellites;
    bool     dataloggerUtc;
    bool     meshUtc;
    uint32_t issAverageFrameIntervalMs;
    bool     bleEnabled;
    uint32_t meshPowerOnSettleMs;
    bool     meshSkipHandshake;
};

extern Params params;
