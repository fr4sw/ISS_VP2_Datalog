// ============================================================================
// Fichier   : ProtobufWriter.cpp
// ============================================================================
#include <string.h>
#include "ProtobufWriter.h"

// Wire types du format protobuf (nommes, pas de valeur magique - règle 16).
static const uint8_t PROTOBUF_WIRETYPE_VARINT = 0;
static const uint8_t PROTOBUF_WIRETYPE_FIXED32 = 5;
static const uint8_t PROTOBUF_WIRETYPE_LENGTH_DELIMITED = 2;

static const uint8_t VARINT_CONTINUATION_BIT = 0x80;
static const uint8_t VARINT_PAYLOAD_MASK = 0x7F;
static const uint8_t VARINT_PAYLOAD_BITS = 7;

void ProtobufWriter::begin(uint8_t *outputBuffer, size_t outputCapacity)
{
    buffer = outputBuffer;
    capacity = outputCapacity;
    cursor = 0;
    overflow = false;
}

void ProtobufWriter::writeByte(uint8_t byteValue)
{
    if (cursor >= capacity)
    {
        overflow = true;
        return;
    }
    buffer[cursor] = byteValue;
    cursor = cursor + 1;
}

// Encodage varint protobuf : groupes de 7 bits, poids faible en premier,
// bit de poids fort a 1 tant qu'il reste des groupes a lire.
// Référence : https://protobuf.dev/programming-guides/encoding/#varints
void ProtobufWriter::writeVarint(uint32_t value)
{
    uint32_t remainingValue = value;
    while (remainingValue > VARINT_PAYLOAD_MASK)
    {
        writeByte((uint8_t)((remainingValue & VARINT_PAYLOAD_MASK) | VARINT_CONTINUATION_BIT));
        remainingValue = remainingValue >> VARINT_PAYLOAD_BITS;
    }
    writeByte((uint8_t)remainingValue);
}

// Tag = (numero_de_champ << 3) | wire_type, lui-meme encode en varint.
// Référence : https://protobuf.dev/programming-guides/encoding/#structure
void ProtobufWriter::writeTag(uint32_t fieldNumber, uint8_t wireType)
{
    uint32_t tagValue = (fieldNumber << 3) | wireType;
    writeVarint(tagValue);
}

void ProtobufWriter::writeVarintField(uint32_t fieldNumber, uint32_t value)
{
    writeTag(fieldNumber, PROTOBUF_WIRETYPE_VARINT);
    writeVarint(value);
}

void ProtobufWriter::writeFloatField(uint32_t fieldNumber, float value)
{
    // Recopie explicite des octets du float (evite tout alias de pointeur
    // non defini - règle 15 : rester explicite plutot que "reinterpret_cast").
    uint32_t floatBits = 0;
    memcpy(&floatBits, &value, sizeof(floatBits));

    writeTag(fieldNumber, PROTOBUF_WIRETYPE_FIXED32);
    writeByte((uint8_t)(floatBits & 0xFF));
    writeByte((uint8_t)((floatBits >> 8) & 0xFF));
    writeByte((uint8_t)((floatBits >> 16) & 0xFF));
    writeByte((uint8_t)((floatBits >> 24) & 0xFF));
}

void ProtobufWriter::writeFixed32Field(uint32_t fieldNumber, uint32_t value)
{
    writeTag(fieldNumber, PROTOBUF_WIRETYPE_FIXED32);
    writeByte((uint8_t)(value & 0xFF));
    writeByte((uint8_t)((value >> 8) & 0xFF));
    writeByte((uint8_t)((value >> 16) & 0xFF));
    writeByte((uint8_t)((value >> 24) & 0xFF));
}

void ProtobufWriter::writeBytesField(uint32_t fieldNumber, const uint8_t *data, size_t dataLength)
{
    writeTag(fieldNumber, PROTOBUF_WIRETYPE_LENGTH_DELIMITED);
    writeVarint((uint32_t)dataLength);
    for (size_t index = 0; index < dataLength; index++)
    {
        writeByte(data[index]);
    }
}

size_t ProtobufWriter::length() const
{
    return cursor;
}

bool ProtobufWriter::overflowed() const
{
    return overflow;
}

const uint8_t *ProtobufWriter::data() const
{
    return buffer;
}
