// ============================================================================
// Fichier   : Rtc.h
// Rôle      : Acces au circuit RTC DS3231 (I2C). N'est utilise que par
//             TimeManager (règle 102) : le reste du programme ne connait
//             jamais la source de l'heure, seulement TimeManager.now().
// Fonctions : rtcBegin()        - initialise le bus I2C et le circuit RTC.
//                                 Renvoie false si le circuit n'est pas
//                                 detecte sur le bus. Si detecte, reecrit
//                                 l'horodatage aux valeurs MANUAL_TIME_*
//                                 de Config.h dans deux cas :
//                                   - le RTC signale une perte d'alimentation
//                                     (horodatage non fiable, bit OSF actif) ;
//                                   - RTC_FORCE_MANUAL_TIME vaut 1 dans
//                                     Config.h (forcage volontaire).
//             rtcCopyDateTime() - lit l'horodatage courant du RTC.
// Dépendances : Bibliotheque "RTClib" (Adafruit, Arduino Library Manager),
//             qui requiert elle-meme "Adafruit BusIO".
// Référence : Maxim Integrated / Analog Devices DS3231, datasheet DS3231.pdf,
//             registre "Status" bit OSF (Oscillator Stop Flag)
//             github.com/adafruit/RTClib
// ============================================================================
#pragma once
#include <Arduino.h>

bool rtcBegin();
void rtcCopyDateTime(char dateString[9], char timeString[7]);
