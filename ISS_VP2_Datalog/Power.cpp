// ============================================================================
// Fichier   : Power.cpp
// Note      : PIN_GPS_ENABLE et PIN_MESH_ENABLE sont actifs a l'etat BAS
//             sur ce montage (0 = alimente, 1 = coupe). Voir BoardConfig.h,
//             POWER_ENABLE_ACTIVE_LEVEL / POWER_ENABLE_INACTIVE_LEVEL.
// ============================================================================
#include <Arduino.h>
#include "Power.h"
#include "BoardConfig.h"
#include "EventLog.h"

Power power;

void Power::begin()
{
#if defined(PIN_GPS_ENABLE)
    pinMode(PIN_GPS_ENABLE, OUTPUT);
    digitalWrite(PIN_GPS_ENABLE, POWER_ENABLE_INACTIVE_LEVEL);
#endif
#if defined(PIN_MESH_ENABLE)
    pinMode(PIN_MESH_ENABLE, OUTPUT);
    digitalWrite(PIN_MESH_ENABLE, POWER_ENABLE_INACTIVE_LEVEL);
#endif
}

void Power::enableGps()
{
#if defined(PIN_GPS_ENABLE)
    digitalWrite(PIN_GPS_ENABLE, POWER_ENABLE_ACTIVE_LEVEL);
    Serial.println(F("[Power] GPS active (niveau bas)"));
    logEvent(F("GPS active"));
#else
    Serial.println(F("[Power] Avertissement : PIN_GPS_ENABLE non definie pour cette carte"));
#endif
}

void Power::disableGps()
{
#if defined(PIN_GPS_ENABLE)
    digitalWrite(PIN_GPS_ENABLE, POWER_ENABLE_INACTIVE_LEVEL);
    Serial.println(F("[Power] GPS coupe (niveau haut)"));
    logEvent(F("GPS coupe"));
#endif
}

void Power::enableMesh()
{
#if defined(PIN_MESH_ENABLE)
    digitalWrite(PIN_MESH_ENABLE, POWER_ENABLE_ACTIVE_LEVEL);
    Serial.println(F("[Power] Mesh active (niveau bas)"));
#else
    Serial.println(F("[Power] Avertissement : PIN_MESH_ENABLE non definie pour cette carte"));
#endif
}

void Power::disableMesh()
{
#if defined(PIN_MESH_ENABLE)
    digitalWrite(PIN_MESH_ENABLE, POWER_ENABLE_INACTIVE_LEVEL);
    Serial.println(F("[Power] Mesh coupe (niveau haut)"));
#endif
}
