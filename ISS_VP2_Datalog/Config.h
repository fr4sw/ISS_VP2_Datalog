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

#define TIME_MODE   TIME_MODE_RTC_ONLY

// Date/heure de reference utilisee si aucune autre source n'est disponible.
// A adapter avant chaque campagne de mesure si le GPS/RTC sont absents.
#define MANUAL_TIME_YEAR    2026
#define MANUAL_TIME_MONTH      8
#define MANUAL_TIME_DAY       02
#define MANUAL_TIME_HOUR      01
#define MANUAL_TIME_MINUTE    05
#define MANUAL_TIME_SECOND     0

// Indicateur de forcage : a 1, le RTC DS3231 est reecrit avec les valeurs
// MANUAL_TIME_* ci-dessus a CHAQUE demarrage, meme s'il contient deja un
// horodatage valide (mise a l'heure volontaire, ex : premiere mise en
// service ou recalage sur le terrain). A repasser a 0 apres la mise a
// l'heure, sans quoi le RTC perd sa fonction de maintien de l'heure a la
// coupure d'alimentation du reste du montage.
#define RTC_FORCE_MANUAL_TIME   0

// --- Interfaces annexes ---
#define USE_SD_CARD    1
#define USE_WIFI_IHM   0
#define USE_OTA        0
#define USE_BME680     1         // capteur temp, Hum, pression interne (et gaz eventuellement)

// --- Constantes fonctionnelles (pas de GPIO, valeurs physiques nommées) ---
#define GPS_TIMEOUT              10000   // ms, avant abandon d'une synchro GPS
#define GPS_MINIMUM_SATELLITES   4
#define TEMPERATURE_RESOLUTION   0.1f    // °C, résolution capteur Davis

// --- Constantes protocole ISS (Davis Vantage Pro2) ---
#define ISS_FRAME_MAX_LENGTH        8   // longueur max, cas FSK avec CRC
#define UART_RS485_BAUD          4800   // Debit RS485
#define ISS_RS485_FRAME_LENGTH      8   // filaire, pas de CRC
#define ISS_WIRELESS_FRAME_LENGTH   8   // FSK, 6 octets utiles + 2 octets CRC

// --- timeout écriture carte SD (pour ne pas écrire trop souvent)
#define LOG_WRITE_INTERVAL_MS   30000UL

// --- Capteur interieur BME680 ---
#define BME680_READ_INTERVAL_MS   30000UL   // ms, periode entre deux mesures 30s pour debug, ensuite 1 à 5 minutes

#define DEBUG              1   // 1 = affiche les acquisitions de données, 0 = production

#define DEBUG_RAW_FRAMES   1   // 1 = affiche chaque trame brute recue en
                                // hexadecimal avant decodage, 0 = production

// Marge de resynchronisation : duree de 100 bits au debit RS485 utilise,
// convertie en microsecondes. A ce debit, une trame complete arrive
// toutes les 2 a 2,5 secondes (protocole Davis), donc un delai de
// quelques dizaines de bits sans reception pendant l'assemblage d'une
// trame signale de maniere fiable une perte de synchronisation.
#define ISS_RS485_RESYNC_TIMEOUT_US   ((100UL * 1000000UL) / UART_RS485_BAUD)  
