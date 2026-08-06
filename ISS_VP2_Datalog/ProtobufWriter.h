// ============================================================================
// Fichier   : ProtobufWriter.h
// Rôle      : Encodeur protobuf minimal, generique (independant de tout
//             message Meshtastic en particulier). Ecrit directement dans un
//             tampon fourni par l'appelant (pas d'allocation dynamique).
//             Ne sait PAS decoder : ce projet n'a besoin que d'emettre des
//             messages ToRadio, pas d'interpreter finement les FromRadio
//             (voir MeshLink.h). Volontairement limite aux types de champs
//             utilises par ce projet ; a etendre si de nouveaux besoins
//             apparaissent (règle 15 : pas d'abstraction non utilisee).
// Fonctions : begin()            - associe un tampon de sortie.
//             writeVarintField() - champ entier (wire type 0 : varint).
//             writeFloatField()  - champ flottant 32 bits (wire type 5).
//             writeFixed32Field()- champ entier 32 bits taille fixe (wire type 5).
//             writeBytesField()  - champ "bytes" ou message imbrique deja
//                                  serialise (wire type 2 : longueur + donnees).
//             length()           - nombre d'octets ecrits jusqu'ici.
//             overflowed()       - true si le tampon fourni etait trop petit
//                                  (règle 23 : toute erreur doit pouvoir etre
//                                  detectee - ici, sans ecrire hors tampon).
// Référence : Format binaire "Protocol Buffers" (proto3), encodage des tags
//             et des varints : https://protobuf.dev/programming-guides/encoding/
// ============================================================================
#pragma once
#include <Arduino.h>

class ProtobufWriter
{
public:
    void begin(uint8_t *outputBuffer, size_t outputCapacity);

    void writeVarintField(uint32_t fieldNumber, uint32_t value);
    void writeFloatField(uint32_t fieldNumber, float value);
    void writeFixed32Field(uint32_t fieldNumber, uint32_t value);
    void writeBytesField(uint32_t fieldNumber, const uint8_t *data, size_t dataLength);

    size_t length() const;
    bool overflowed() const;
    const uint8_t *data() const;

private:
    void writeByte(uint8_t byteValue);
    void writeVarint(uint32_t value);
    void writeTag(uint32_t fieldNumber, uint8_t wireType);

    uint8_t *buffer;
    size_t   capacity;
    size_t   cursor;
    bool     overflow;
};
