// ============================================================================
// Fichier   : MeshtasticTelemetry.cpp
// ============================================================================
#include "MeshtasticTelemetry.h"
#include "MeshLink.h"   // TORADIO_FIELD_PACKET (constante de session, voir MeshLink.h)
#include "ProtobufWriter.h"

// Generateur d'ID de paquet (voir MeshtasticTelemetry.h : MESHPACKET_FIELD_ID,
// et MeshLink.cpp qui memorise cette valeur pour reconnaitre le
// FromRadio.queue_status correspondant). Un simple compteur incremental
// suffit ici (règle 15) : on n'a pas besoin d'unicite globale sur tout le
// mesh, seulement de distinguer NOTRE dernier paquet envoye des precedents,
// et les envois sont espaces de 5 min (voir DataLogger.cpp). Jamais 0
// (reserve pour "laisser le firmware attribuer lui-meme un ID", voir
// MESHPACKET_FIELD_FROM pour la meme convention appliquee au champ FROM).
static uint32_t nextPacketId = 1;

bool meshBuildEnvironmentTelemetryToRadio(uint8_t *outputBuffer, size_t outputCapacity, size_t &outputLength,
                                           uint32_t &packetId,
                                           uint32_t utcUnixTime,
                                           float temperatureC, float relativeHumidityPercent, float pressureHpa,
                                           uint16_t windDirectionDeg, float windSpeedKph, float windGustKph,
                                           float rainfall1hMm, float rainfall24hMm)
{
    // Construction "de l'interieur vers l'exterieur" : chaque message
    // imbrique est d'abord serialise dans son propre petit tampon, puis
    // injecte comme simple champ "bytes" dans le message qui le contient
    // (c'est exactement ce que fait le format protobuf pour un champ de
    // type message : un champ "bytes" avec longueur, rien de plus).

    uint8_t environmentMetricsBuffer[48];
    ProtobufWriter environmentMetricsWriter;
    environmentMetricsWriter.begin(environmentMetricsBuffer, sizeof(environmentMetricsBuffer));
    environmentMetricsWriter.writeFloatField(ENV_FIELD_TEMPERATURE, temperatureC);
    environmentMetricsWriter.writeFloatField(ENV_FIELD_RELATIVE_HUMIDITY, relativeHumidityPercent);
    environmentMetricsWriter.writeFloatField(ENV_FIELD_BAROMETRIC_PRESSURE, pressureHpa);
    environmentMetricsWriter.writeVarintField(ENV_FIELD_WIND_DIRECTION, windDirectionDeg);
    environmentMetricsWriter.writeFloatField(ENV_FIELD_WIND_SPEED, windSpeedKph * KPH_TO_MPS);
    environmentMetricsWriter.writeFloatField(ENV_FIELD_WIND_GUST, windGustKph * KPH_TO_MPS);
    environmentMetricsWriter.writeFloatField(ENV_FIELD_RAINFALL_1H, rainfall1hMm);
    environmentMetricsWriter.writeFloatField(ENV_FIELD_RAINFALL_24H, rainfall24hMm);

    uint8_t telemetryBuffer[64];
    ProtobufWriter telemetryWriter;
    telemetryWriter.begin(telemetryBuffer, sizeof(telemetryBuffer));
    telemetryWriter.writeFixed32Field(TELEMETRY_FIELD_TIME, utcUnixTime);
    telemetryWriter.writeBytesField(TELEMETRY_FIELD_ENVIRONMENT_METRICS, environmentMetricsWriter.data(), environmentMetricsWriter.length());

    uint8_t dataBuffer[80];
    ProtobufWriter dataWriter;
    dataWriter.begin(dataBuffer, sizeof(dataBuffer));
    dataWriter.writeVarintField(DATA_FIELD_PORTNUM, PORTNUM_TELEMETRY_APP);
    dataWriter.writeBytesField(DATA_FIELD_PAYLOAD, telemetryWriter.data(), telemetryWriter.length());

    packetId = nextPacketId;
    nextPacketId = nextPacketId + 1;
    if (nextPacketId == 0) { nextPacketId = 1; }   // ne jamais retomber sur 0 (voir plus haut)

    uint8_t meshPacketBuffer[100];
    ProtobufWriter meshPacketWriter;
    meshPacketWriter.begin(meshPacketBuffer, sizeof(meshPacketBuffer));
    // FROM (voir MeshtasticTelemetry.h) : sans ce champ, le firmware ne
    // peut pas attribuer/router le paquet et ne l'emet jamais sur le
    // reseau radio - bug confirme par l'utilisateur sur un vrai T114.
    // Ecrit explicitement a 0 (jamais omis, voir ProtobufWriter::
    // writeFixed32Field() : pas d'optimisation "valeur par defaut proto3"
    // dans cette implementation - le champ apparait bien sur le fil).
    meshPacketWriter.writeFixed32Field(MESHPACKET_FIELD_FROM, 0);
    meshPacketWriter.writeFixed32Field(MESHPACKET_FIELD_ID, packetId);
    meshPacketWriter.writeFixed32Field(MESHPACKET_FIELD_TO, MESHTASTIC_BROADCAST_ADDR);
    meshPacketWriter.writeVarintField(MESHPACKET_FIELD_CHANNEL, 0);
    meshPacketWriter.writeBytesField(MESHPACKET_FIELD_DECODED, dataWriter.data(), dataWriter.length());

    ProtobufWriter toRadioWriter;
    toRadioWriter.begin(outputBuffer, outputCapacity);
    toRadioWriter.writeBytesField(TORADIO_FIELD_PACKET, meshPacketWriter.data(), meshPacketWriter.length());

    outputLength = toRadioWriter.length();

    bool anyOverflow = environmentMetricsWriter.overflowed() || telemetryWriter.overflowed()
                        || dataWriter.overflowed() || meshPacketWriter.overflowed() || toRadioWriter.overflowed();
    return (anyOverflow == false);
}
