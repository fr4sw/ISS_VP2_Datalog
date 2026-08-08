// ============================================================================
// Fichier      : Config.h
// Rôle         : Activation des modules logiciels du projet.
//                Aucune information matérielle (GPIO, bus) ne figure ici.
// Référence    : CodingRules_Gen.md §6
// ============================================================================
#pragma once
#include <Arduino.h>

// --- Source de capteurs ISS : CHOIX EXCLUSIF ---
// Sur nRF52840, PIN_RS485_RX et PIN_RADIO_CS (CS du SX1278) partagent la
// meme broche physique selon le schema retenu : il est donc interdit
// d'activer ISS_WIRELESS et ISS_RS485 simultanement sur cette cible.
// Un seul des deux defines suivants doit valoir 1.
#define ISS_WIRELESS   0
#define ISS_RS485      1

#if (ISS_WIRELESS + ISS_RS485) != 1
    #error "Config.h : ISS_WIRELESS et ISS_RS485 sont exclusifs (une seule source active)"
#endif

// --- Source d'horodatage : CHOIX EXCLUSIF ---
// Ordre de preference si plusieurs sources sont physiquement presentes :
//   1) TIME_MODE_GPS_RTC   (GPS prioritaire, RTC en secours et resynchro)
//   2) TIME_MODE_RTC_ONLY
//   3) TIME_MODE_GPS_ONLY (uniquement 1 fois par heure, avec manual après la première mise à l'heure)
//   4) TIME_MODE_MANUAL    (horodatage fige, initialise dans le code)
// Un seul mode doit etre selectionne ci-dessous.
#define TIME_MODE_GPS_RTC   1
#define TIME_MODE_RTC_ONLY  2
#define TIME_MODE_GPS_ONLY  3
#define TIME_MODE_MANUAL    4

#define TIME_MODE   TIME_MODE_GPS_RTC

// --- Interfaces annexes ---
#define USE_SD_CARD    1
#define USE_WIFI_IHM   0
#define USE_OTA        0
#define USE_BME680     1         // capteur temp, Hum, pression interne (et gaz eventuellement)
#define USE_MESHTASTIC 1         // liaison serie vers un appareil Meshtastic (T114), voir MeshLink.h
#define USE_BLE        1         // annonce BLE minimale (donnees courantes en lecture seule pour une
                                  // appli generique type nRF Connect/LightBlue, pas d'appli dediee),
                                  // voir BleLink.h

// --- Constantes protocole ISS (Davis Vantage Pro2) ---
#define ISS_FRAME_MAX_LENGTH        8   // longueur max, cas FSK avec CRC
#define UART_RS485_BAUD          4800   // Debit RS485
#define ISS_RS485_FRAME_LENGTH      8   // filaire, pas de CRC
#define ISS_WIRELESS_FRAME_LENGTH   8   // FSK, 6 octets utiles + 2 octets CRC
#define TEMPERATURE_RESOLUTION   0.1f    // °C, résolution capteur Davis

// --- Date/heure de reference manuelle (utilisee si aucune autre source
// n'est disponible). A adapter avant chaque campagne de mesure si le
// GPS/RTC sont absents.
// ATTENTION : ne jamais ecrire ces nombres avec un zero devant (ex : 08) -
// un litteral C commencant par 0 est interprete en OCTAL. "09" ne
// compilerait pas (chiffre invalide en octal, erreur detectee), mais
// "010" compilerait silencieusement avec la valeur 8 au lieu de 10 : le
// danger n'est pas visible a la compilation dans ce cas precis.
#define MANUAL_TIME_YEAR    2026
#define MANUAL_TIME_MONTH      8
#define MANUAL_TIME_DAY        2
#define MANUAL_TIME_HOUR        1
#define MANUAL_TIME_MINUTE      5
#define MANUAL_TIME_SECOND      0

// Indicateur de forcage : a 1, le RTC DS3231 est reecrit avec les valeurs
// MANUAL_TIME_* ci-dessus a CHAQUE demarrage, meme s'il contient deja un
// horodatage valide (mise a l'heure volontaire, ex : premiere mise en
// service ou recalage sur le terrain). A repasser a 0 apres la mise a
// l'heure, sans quoi le RTC perd sa fonction de maintien de l'heure a la
// coupure d'alimentation du reste du montage.
#define RTC_FORCE_MANUAL_TIME   0

// --- Fuseau horaire : PARAMETRE (module Params), valeur par defaut ---
// Horodatage RTC/GPS toujours conserve en UTC en interne ; le decalage
// n'est applique qu'a l'affichage/enregistrement (TimeManager::now()).
// En heures entieres (usage courant) : La Reunion = UTC+4, pas d'heure
// d'ete. Les quelques fuseaux a la demi-heure ne sont pas geres pour
// l'instant (règle 26 : pas d'optimisation/generalisation avant besoin
// reel ; a etendre en minutes le jour ou une campagne le necessite).
#define TIMEZONE_OFFSET_HOURS_DEFAULT   4

// --- GPS (TIME_MODE_GPS_RTC / TIME_MODE_GPS_ONLY) ---
// GPS_TIMEOUT genereux : un premier point a froid (cold start) peut
// demander de 5 a 15 minutes, pas quelques secondes.
#define GPS_TIMEOUT                    900000UL   // ms (15 min), avant abandon d'une synchro GPS
// Valeur PAR DEFAUT uniquement : la valeur reellement utilisee est le
// PARAMETRE GPSMINSAT (module Params), modifiable sans reprogrammer.
// Seuil unique, utilise a la fois pour accepter un point date/heure ET
// pour accepter/enregistrer une position (latitude/longitude) : la
// precision de position depend fortement du nombre de satellites,
// contrairement a l'heure (correcte des 4 satellites). Indicatif (DOP
// variable selon l'environnement, ne pas prendre comme une garantie) :
//     4 satellites  ~50 m de precision position (heure : OK)
//     8 satellites  ~5 m de precision position
// A augmenter (ex : 8) si la position doit etre exploitable, en sachant
// que cela rallonge la duree d'acquisition GPS.
#define GPS_MINIMUM_SATELLITES         4
// isValid() de TinyGPSPlus indique seulement qu'une valeur a deja ete
// decodee au moins une fois depuis le demarrage, pas qu'elle est fraiche :
// gpsParser est statique et n'est jamais reinitialise entre deux sessions
// GPS (voir gpsBegin()). GPS_FIX_MAX_AGE_MS borne l'age (age(), en ms)
// tolere pour un champ date/heure/satellites avant de le considerer
// perime (ex : reste d'une session precedente).
#define GPS_FIX_MAX_AGE_MS             2000UL
// Frequence de resynchronisation, une fois qu'un premier point valide a
// ete obtenu : le RTC (present en mode GPS_RTC) derive tres peu, une
// resynchro quotidienne suffit ; sans RTC (GPS_ONLY, horloge logicielle
// uniquement) on recale plus souvent pour limiter la derive de millis().
#define GPS_RTC_RESYNC_INTERVAL_MS     86400000UL   // 24 h
#define GPS_ONLY_RESYNC_INTERVAL_MS     14400000UL   // 4 h

// --- Meshtastic (MeshLink.h/.cpp) ---
// MESH_BAUD_DEFAULT : a VERIFIER sur le T114 reel (configuration du module
// "Serial"/console de l'appareil) - 38400 est la seule valeur documentee
// trouvee avec certitude (module "Serial", pas necessairement le meme
// reglage que la console/API protobuf). Modifiable sans reprogrammer via
// le parametre MESHBAUD (Params) une fois verifie.
#define MESH_BAUD_DEFAULT           38400UL
// Delai (bloquant, voir MeshLink.cpp) laisse au T114 pour cesser d'emettre
// du texte de debug apres la poignee de main, avant de lui envoyer la
// telemetrie.
#define MESH_HANDSHAKE_SETTLE_MS    250UL
// Duree max pendant laquelle l'alimentation Mesh est maintenue APRES
// l'envoi, en attendant une reponse du module T114 (voir MeshLink.cpp :
// meshLinkUpdate()). Ecrire les octets sur l'UART ne signifie pas que le
// module a fini de les emettre sur le reseau radio - couper l'alimentation
// immediatement apres l'ecriture ne lui en laisse pas le temps. Non
// bloquant (verifie a chaque loop(), pas d'attente active) : n'empeche PAS
// la reception RS485 pendant ce delai.
#define MESH_ACK_TIMEOUT_MS         180000UL   // ms (3 min)

// --- Creneau de "transmission" (règle Davis/WeeWx/Weatherlink) ---
// Contrairement a l'ecriture SD (LOG_WRITE_INTERVAL_MS_DEFAULT, plus
// frequente, purement locale), ce creneau est cale sur l'horloge murale
// (00, 05, 10, ... minutes), pas sur un simple compte a rebours depuis le
// demarrage : cela permet de PREDIRE l'instant de la prochaine
// transmission, comme le fait WeeWx/Weatherlink. C'est aussi la base du
// calcul du taux de reception (tauxReceptionPct, voir DataLogger.cpp).
// MeshLink.h/.cpp existe (premier jet) mais n'est pas encore branche sur
// ce creneau : voir DataLogger.h remarque 2 pour le point d'integration
// prevu (colonne creneauTransmission).
#define TRANSMISSION_SLOT_MINUTES   5

// Delai entre 2 trames ISS au-dela duquel on considere qu'au moins une
// trame a probablement ete ratee entre les deux (voir DataLogger.cpp :
// compteur missedFrameGapCount, DEBUG uniquement). Marge confortable par
// rapport a l'intervalle theorique Davis (~2,5 a 2,56s selon la station,
// voir issSecondsPerPacket()) : un ecart franc, pas un simple jitter.
#define FRAME_GAP_WARNING_MS            3000UL

// --- BLE (USE_BLE) - voir BleLink.h ---
#define BLE_DEVICE_NAME          "ISS-VP2-Datalog"   // nom affiche par les applis de scan BLE
#define BLE_UPDATE_INTERVAL_MS   5000UL   // frequence de rafraichissement des caracteristiques
#define BLE_TX_POWER_DBM         4        // puissance d'emission (dBm), valeurs typiques nRF52 :
                                           // -20/-16/-12/-8/-4/0/4/8

// Court delai apres la fermeture du creneau de 5 min avant l'envoi de la
// telemetrie Mesh : laisse le temps a une derniere trame ISS en cours de
// traitement de se terminer avant de figer/envoyer la synthese du creneau
// (evite un effet de bord ou l'envoi partirait avec des donnees pas tout
// a fait figees). Non bloquant (voir DataLogger::update()).
#define MESH_SEND_SETTLE_MS            200UL

// Volume d'eau par basculement de l'auget (bucket Davis metrique). A
// CONFIRMER sur ce materiel (le collecteur imperial, 0,01 pouce/0,254 mm,
// existe aussi et n'est pas distinguable par logiciel).
#define RAIN_MM_PER_TIP                 0.2f
// Duree sans nouveau clic de pluie avant de considerer un episode termine.
#define RAIN_EPISODE_TIMEOUT_MS         1800000UL   // ms (30 min)

// --- timeout écriture carte SD (pour ne pas écrire trop souvent) ---
// Valeur PAR DEFAUT uniquement : la valeur reellement utilisee est un
// PARAMETRE (module Params), modifiable sans reprogrammer, persistee sur
// la carte SD (fichier PARAMS_FILE_NAME). 0 = ecriture a chaque trame recue.
// Independant du creneau de transmission ci-dessus (deux cumuls distincts).
#define LOG_WRITE_INTERVAL_MS_DEFAULT   30000UL

// --- Fichiers de configuration/journalisation sur carte SD ---
// Noms courts 8.3 (FAT), portables entre coeurs Arduino.
#define PARAMS_FILE_NAME     "PARAMS.TXT"
#define EVENTLOG_FILE_NAME   "EVENTS.LOG"
#define LOCATION_FILE_NAME   "LOCATION.LOG"   // historique des positions GPS (voir LocationLog.h),
                                               // separe du CSV temps reel : une position change tres
                                               // rarement (station fixe), inutile de la repeter sur
                                               // chaque ligne ecrite toutes les 30s

// --- Capteur interieur BME680 ---
#define BME680_READ_INTERVAL_MS   30000UL   // ms, periode entre deux mesures 30s pour debug, ensuite 1 à 5 minutes

#define DEBUG              1   // 1 = affiche les acquisitions de données, 0 = production

#define DEBUG_RAW_FRAMES   1   // 1 = affiche chaque trame brute recue en
                                // hexadecimal avant decodage, 0 = production

#define DEBUG_GPS          1   // 1 = affiche uniquement l'heure UTC et le
                                // nombre de satellites decodes par le GPS
                                // (le flux NMEA complet est trop volumineux
                                // pour DEBUG_RAW_FRAMES), 0 = production

#define DEBUG_MESH         1   // 1 = affiche sur le moniteur serie les
                                // trames Meshtastic echangees (envoyees ET
                                // recues) en hexadecimal, voir MeshLink.cpp,
                                // 0 = production

// Marge de resynchronisation : duree de 100 bits au debit RS485 utilise,
// convertie en microsecondes. A ce debit, une trame complete arrive
// toutes les 2 a 2,5 secondes (protocole Davis), donc un delai de
// quelques dizaines de bits sans reception pendant l'assemblage d'une
// trame signale de maniere fiable une perte de synchronisation.
#define ISS_RS485_RESYNC_TIMEOUT_US   ((100UL * 1000000UL) / UART_RS485_BAUD)  
