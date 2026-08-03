// ============================================================================
// Fichier   : SerialConsole.cpp
// ============================================================================
#include <Arduino.h>
#include "SerialConsole.h"
#include "Params.h"

static const uint8_t CONSOLE_LINE_MAX_LENGTH = 40;

static char    consoleLineBuffer[CONSOLE_LINE_MAX_LENGTH];
static uint8_t consoleLineLength;

static void printHelp()
{
    Serial.println(F("[Console] Commandes disponibles :"));
    Serial.println(F("  HELP                  - affiche cette aide"));
    Serial.println(F("  GET                   - affiche les parametres courants"));
    Serial.println(F("  SET TZ <heures>       - fuseau horaire (ex : SET TZ 4 pour UTC+4)"));
    Serial.println(F("  SET LOGINTERVAL <ms>  - periode d'ecriture CSV (0 = a chaque trame)"));
    Serial.println(F("  SET GPSBAUD <bauds>   - debit UART du GPS (ex : SET GPSBAUD 9600)"));
    Serial.println(F("  SAVE                  - enregistre les parametres sur la carte SD"));
}

static void printCurrentValues()
{
    Serial.print(F("[Console] TZ="));
    Serial.print(params.getTimezoneOffsetHours());
    Serial.print(F("  LOGINTERVAL="));
    Serial.print(params.getLogWriteIntervalMs());
    Serial.print(F("  GPSBAUD="));
    Serial.println(params.getGpsBaudRate());
}

// Traite une commande "SET <CLE> <VALEUR>" deja separee de son mot-clef
// "SET" (règle 20 : une seule tache par fonction).
static void handleSetCommand(char arguments[])
{
    char *key = strtok(arguments, " ");
    char *valueText = strtok(nullptr, " ");
    if ((key == nullptr) || (valueText == nullptr))
    {
        Serial.println(F("[Console] Erreur : syntaxe attendue SET <CLE> <VALEUR>"));
        return;
    }

    long value = atol(valueText);

    if (strcmp(key, "TZ") == 0)
    {
        params.setTimezoneOffsetHours((int8_t)value);
        Serial.println(F("[Console] TZ mis a jour (SAVE pour rendre le reglage permanent)"));
    }
    else if (strcmp(key, "LOGINTERVAL") == 0)
    {
        params.setLogWriteIntervalMs((uint32_t)value);
        Serial.println(F("[Console] LOGINTERVAL mis a jour (SAVE pour rendre le reglage permanent)"));
    }
    else if (strcmp(key, "GPSBAUD") == 0)
    {
        params.setGpsBaudRate((uint32_t)value);
        Serial.println(F("[Console] GPSBAUD mis a jour (SAVE pour rendre le reglage permanent)"));
    }
    else
    {
        Serial.print(F("[Console] Erreur : parametre inconnu : "));
        Serial.println(key);
    }
}

// Decoupe et execute une ligne de commande complete. Independante de la
// source physique (voir remarque dans SerialConsole.h).
static void executeCommandLine(char line[])
{
    char *command = strtok(line, " ");
    if (command == nullptr)
    {
        return;
    }

    if (strcmp(command, "HELP") == 0)
    {
        printHelp();
    }
    else if (strcmp(command, "GET") == 0)
    {
        printCurrentValues();
    }
    else if (strcmp(command, "SET") == 0)
    {
        char *remainder = command + strlen(command) + 1;
        bool hasRemainder = (remainder <= (consoleLineBuffer + consoleLineLength));
        if (hasRemainder == true)
        {
            handleSetCommand(remainder);
        }
        else
        {
            Serial.println(F("[Console] Erreur : syntaxe attendue SET <CLE> <VALEUR>"));
        }
    }
    else if (strcmp(command, "SAVE") == 0)
    {
        params.save();
    }
    else
    {
        Serial.println(F("[Console] Commande inconnue. Tapez HELP pour la liste des commandes."));
    }
}

void serialConsoleBegin()
{
    consoleLineLength = 0;
    consoleLineBuffer[0] = '\0';
}

void serialConsoleUpdate()
{
    while (Serial.available() > 0)
    {
        char receivedChar = (char)Serial.read();

        if (receivedChar == '\n')
        {
            consoleLineBuffer[consoleLineLength] = '\0';
            if (consoleLineLength > 0)
            {
                executeCommandLine(consoleLineBuffer);
            }
            consoleLineLength = 0;
            continue;
        }

        if (receivedChar == '\r')
        {
            continue;
        }

        if (consoleLineLength < (CONSOLE_LINE_MAX_LENGTH - 1))
        {
            consoleLineBuffer[consoleLineLength] = receivedChar;
            consoleLineLength = consoleLineLength + 1;
        }
    }
}
