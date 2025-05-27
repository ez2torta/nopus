#ifndef OPUS_PROCESS_H
#define OPUS_PROCESS_H

#include <stdlib.h>

#include <string.h>

#include <opus/opus.h>

#include "files.h"

#include "list.h"

#include "type.h"

#include "common.h"

#define CHUNK_HEADER_ID (0x80000001)
//#define CHUNK_CONTEXT_ID (0x80000003)
#define CHUNK_DATA_ID (0x80000004)
#define CHUNK_CAPCOM_DATA_ID (0x80000004)

#define OPUS_VERSION (0)

// The identifier for regular old Ogg Opus files.
#define OGG_OPUS_ID IDENTIFIER_TO_U32('O','g','g','S')

typedef struct __attribute__((packed)) {
    u32 chunkId; // Compare to CHUNK_HEADER_ID.
    u32 chunkSize; // Exclusive of chunkId and chunkSize. Usually 0x18.

    u8 version; // Compare to OPUS_VERSION.

    u8 channelCount; // Values that are not 1 or 2 are illegal.

    u16 frameSize; // Frame size if constant bitrate, 0 if variable bitrate.

    u32 sampleRate; // Allowed values: 48000, 24000, 16000, 12000, and 8000.

    u32 dataOffset; // Offset to the data chunk (OpusDataChunk).
    u32 _unk14; // Offset to a section with the ID 0x80000002, can't find it anywhere ..
    u32 contextOffset; // Offset to a section with the ID 0x80000003, supposedly holds looping info.

    u16 preSkipSamples; // The amount of samples that should be skipped at the beginning of playback.

    u16 _pad16;
} OpusFileHeader;

typedef struct __attribute__((packed)) {
    u32 chunkId; // Compare to CHUNK_DATA_ID.
    u32 chunkSize; // Exclusive of chunkId and chunkSize.

    u8 data[0]; // Dynamically sized Opus packets (OpusPacketHeader).
} OpusDataChunk;

typedef struct __attribute__((packed)) {
    // Note: both of these fields are in big-endian, might just be the DSPs endianness..
    u32 packetSize; // Length of the Opus packet.
    u32 finalRange; // Encoder final range.
    u8 packet[0];
} OpusPacketHeader;

// Estructura del encabezado de formato Capcom OPUS mejorada
typedef struct __attribute__((packed)) {
    u32 numSamples;    // Número total de muestras
    u32 channelCount;  // Número de canales (1 o 2)
    u64 loopInfo;      // 8 bytes de información de loop (todos FF si no hay loop)
    u32 unknown1;      // Valor específico del original (0xF8000000)
    u32 frameSize;     // Tamaño de frame con datos extra (offset 0x10)
    u32 extraChunks;   // Cantidad de chunks extra (offset 0x14)
    u32 padding1;      // Null/padding (offset 0x18)
    u32 dataOffset;    // Offset a los datos OPUS (típicamente 0x30)
    u8 configData[16]; // Configuración específica de Capcom (bytes exactos del original)
} OpusCapcomHeader;

void OpusPreprocess(u8* opusData) {
    OpusFileHeader* fileHeader = (OpusFileHeader*)opusData;

    if (fileHeader->chunkId == OGG_OPUS_ID)
        panic("A Ogg Opus file was passed; please pass in a Nintendo Opus file.");
    if (fileHeader->chunkId != CHUNK_HEADER_ID)
        panic("OPUS file header ID is nonmatching");

    if (fileHeader->contextOffset != 0)
        warn("OPUS context is present but will be ignored");
    
    if (
        fileHeader->sampleRate != 48000 &&
        fileHeader->sampleRate != 24000 &&
        fileHeader->sampleRate != 16000 &&
        fileHeader->sampleRate != 12000 &&
        fileHeader->sampleRate != 8000
    )
        panic("Invalid OPUS sample rate (%uhz)", fileHeader->sampleRate);

    if (
        fileHeader->channelCount != 1 &&
        fileHeader->channelCount != 2
    )
        panic("Invalid OPUS channel count (%u)", fileHeader->channelCount);

    OpusDataChunk* dataChunk = (OpusDataChunk*)(opusData + fileHeader->dataOffset);
    if (dataChunk->chunkId != CHUNK_DATA_ID)
        panic("OPUS data chunk ID is nonmatching");
}

u32 OpusGetChannelCount(u8* opusData) {
    return ((OpusFileHeader*)opusData)->channelCount;
}
u32 OpusGetSampleRate(u8* opusData) {
    return ((OpusFileHeader*)opusData)->sampleRate;
}

ListData OpusDecode(u8* opusData) {
    OpusFileHeader* fileHeader = (OpusFileHeader*)opusData;
    OpusDataChunk* dataChunk = (OpusDataChunk*)(opusData + fileHeader->dataOffset);

    int error;
    OpusDecoder* decoder = opus_decoder_create(fileHeader->sampleRate, fileHeader->channelCount, &error);
    if (error != OPUS_OK)
        panic("OpusDecode: opus_decoder_create fail: %s", opus_strerror(error));

    const unsigned coFrameSize = fileHeader->frameSize != 0 ?
        fileHeader->frameSize :
        (fileHeader->sampleRate / 100 * fileHeader->sampleRate);
    
    s16* tempSamples = (s16*)malloc(coFrameSize * sizeof(s16));
    if (!tempSamples)
        panic("OpusDecode: failed to alloc temp buffer");

    ListData samples;
    ListInit(&samples, sizeof(s16), coFrameSize);
    
    unsigned offset = 0;

    int samplesLeftToSkip = fileHeader->preSkipSamples;

    while (offset < dataChunk->chunkSize) {
        OpusPacketHeader* packetHeader = (OpusPacketHeader*)(dataChunk->data + offset);
        u32 packetSize = __builtin_bswap32(packetHeader->packetSize);

        offset += sizeof(OpusPacketHeader) + packetSize;

        int samplesDecoded = opus_decode(decoder, packetHeader->packet, packetSize, tempSamples, coFrameSize, 0);
        if (samplesDecoded < 0)
            panic("OpusProcess: opus_decode fail: %s", opus_strerror(samplesDecoded));

        if (samplesLeftToSkip > 0) {
            // Full skip.
            if (samplesDecoded <= samplesLeftToSkip) {
                samplesLeftToSkip -= samplesDecoded;
                continue;
            }
            // Partial skip.
            else {
                int remainingSamples = samplesDecoded - samplesLeftToSkip;
                ListAddRange(&samples, tempSamples + samplesLeftToSkip, remainingSamples * fileHeader->channelCount);
                samplesLeftToSkip = 0;
            }
        }
        // No skip.
        else
            ListAddRange(&samples, tempSamples, samplesDecoded * fileHeader->channelCount);
    }

    opus_decoder_destroy(decoder);
    
    free(tempSamples);

    return samples;
}

#define OPUS_PACKETSIZE_MAX (1275)

// Cambiar de 96000 a 99000 para coincidir con el bitrate del archivo original
#define OPUS_DEFAULT_BITRATE (99000)

typedef struct OpusBuildPacket {
    struct OpusBuildPacket* next;

    u32 packetLen;
    u32 finalRange;
    u8 packetData[0];
} OpusBuildPacket;

MemoryFile OpusBuild(s16* samples, u32 sampleCount, u32 sampleRate, u32 channelCount) {
    if (
        sampleRate != 48000 && sampleRate != 24000 &&
        sampleRate != 16000 && sampleRate != 12000 &&
        sampleRate != 8000
    ) {
        panic(
            "OpusBuild: Invalid sample rate (%uhz)\n"
            "Allowed sample rates are: 48000, 24000, 16000, 12000, and 8000",
            sampleRate
        );
    }

    if (
        channelCount != 1 && channelCount != 2
    ) {
        panic(
            "OpusBuild: Invalid channel count (%u)\n"
            "Only one or two channels are allowed.",
            channelCount
        );
    }

    MemoryFile mfResult;

    int opusError;

    OpusEncoder* encoder = opus_encoder_create(sampleRate, channelCount, OPUS_APPLICATION_AUDIO, &opusError);
    if (opusError < 0)
        panic("OpusBuild: opus_encoder_create failed: %s", opus_strerror(opusError));

    opusError = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(OPUS_DEFAULT_BITRATE));
    if (opusError < 0)
        panic("OpusBuild: failed to set Opus bitrate to %u", OPUS_DEFAULT_BITRATE);

    opusError = opus_encoder_ctl(encoder, OPUS_SET_VBR(1));
    if (opusError < 0)
        panic("OpusBuild: failed to enable Opus VBR");

    opusError = opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(0));
    if (opusError < 0)
        panic("OpusBuild: failed to disable Opus VBR constraint");

    u32 frameDurationMs = 20;
    u32 frameSize = (sampleRate / 1000) * frameDurationMs;
    u32 samplesPerFrame = frameSize * channelCount;

    int preSkipSamples;
    opusError = opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&preSkipSamples));
    if (opusError < 0)
        panic("OpusBuild: failed to get pre-skip sample count");

    // Root packet has no data. packetLen holds the packet count.
    OpusBuildPacket* rootPacket = (OpusBuildPacket*)malloc(sizeof(OpusBuildPacket));
    if (rootPacket == NULL)
        panic("OpusBuild: failed to allocate root packet");

    rootPacket->next = NULL;
    rootPacket->packetLen = 0;
    rootPacket->finalRange = 0;

    OpusBuildPacket* currentPacket = rootPacket;

    u8 buffer[OPUS_PACKETSIZE_MAX];

    for (u32 i = 0; i + samplesPerFrame <= sampleCount; i += samplesPerFrame) {
        int nbBytes = opus_encode(
            encoder, samples + i, frameSize, buffer, sizeof(buffer)
        );
        if (nbBytes < 0)
            panic("OpusBuild: opus_encode failed: %s", opus_strerror(nbBytes));

        OpusBuildPacket* packet = (OpusBuildPacket*)malloc(sizeof(OpusBuildPacket) + nbBytes);
        if (packet == NULL)
            panic("OpusBuild: failed to allocate packet");

        packet->next = NULL;

        packet->packetLen = nbBytes;
        memcpy(packet->packetData, buffer, nbBytes);

        opusError = opus_encoder_ctl(encoder, OPUS_GET_FINAL_RANGE(&packet->finalRange));
        if (opusError < 0)
            panic("OpusBuild: failed to get encoder final range");

        currentPacket->next = packet;
        currentPacket = packet;

        rootPacket->packetLen++;
    }

    opus_encoder_destroy(encoder);

    // Now to actually write the file.

    mfResult.size = sizeof(OpusFileHeader) + sizeof(OpusDataChunk);

    currentPacket = rootPacket->next;
    while (currentPacket != NULL) {
        mfResult.size += sizeof(OpusPacketHeader) + currentPacket->packetLen;
        currentPacket = currentPacket->next;
    }

    mfResult.data_void = malloc(mfResult.size);
    if (mfResult.data_void == NULL)
        panic("OpusBuild: failed to allocate file buffer");

    OpusFileHeader* fileHeader = (OpusFileHeader*)mfResult.data_void;

    fileHeader->chunkId = CHUNK_HEADER_ID;
    fileHeader->chunkSize = sizeof(OpusFileHeader) - 8;

    fileHeader->version = OPUS_VERSION;

    fileHeader->channelCount = channelCount;

    fileHeader->frameSize = 0; // VBR.

    fileHeader->sampleRate = sampleRate;

    fileHeader->dataOffset = sizeof(OpusFileHeader);
    fileHeader->_unk14 = 0x00000000;
    fileHeader->contextOffset = 0x00000000;

    fileHeader->preSkipSamples = preSkipSamples;

    fileHeader->_pad16 = 0x0000;

    OpusDataChunk* dataChunk = (OpusDataChunk*)(fileHeader + 1);

    dataChunk->chunkId = CHUNK_DATA_ID;
    dataChunk->chunkSize = mfResult.size - sizeof(OpusFileHeader) - sizeof(OpusDataChunk);

    OpusPacketHeader* currentPacketHeader = (OpusPacketHeader*)dataChunk->data;

    currentPacket = rootPacket->next;
    while (currentPacket != NULL) {
        currentPacketHeader->packetSize = __builtin_bswap32(currentPacket->packetLen);
        currentPacketHeader->finalRange = __builtin_bswap32(currentPacket->finalRange);

        memcpy(
            currentPacketHeader->packet,
            currentPacket->packetData, currentPacket->packetLen
        );

        currentPacketHeader = (OpusPacketHeader*)(
            (u8*)(currentPacketHeader + 1) + currentPacket->packetLen
        );

        currentPacket = currentPacket->next;
    }

    // Free packets.
    currentPacket = rootPacket;
    while (currentPacket) {
        OpusBuildPacket* nextPacket = currentPacket->next;
        free(currentPacket);
        currentPacket = nextPacket;
    }

    return mfResult;
}

// Ahora acepta configData, criticalBytes y lista de tamaños de paquetes originales
MemoryFile OpusBuildCapcom(s16* samples, u32 sampleCount, u32 sampleRate,
    u32 channelCount, u32 loopStart, u32 loopEnd,
    u8* configData, u8* criticalBytes, u32* orig_packet_sizes, size_t orig_packet_count)
{
    // Para OPUS Capcom necesitamos un bitrate alto para buena calidad
    const u32 bitRate = 99000;
    
    // El frameSize debe ser 60ms (2880 muestras a 48kHz) 
    // Esto se basa en el código de vgmstream donde OPUS_SWITCH usa frames de 60ms
    const u32 frameSize = 2880;  // 60ms @ 48kHz
    const u32 samplesPerFrame = frameSize;
    
    if (
        sampleRate != 48000 && sampleRate != 24000 &&
        sampleRate != 16000 && sampleRate != 12000 &&
        sampleRate != 8000
    ) {
        panic(
            "OpusBuildCapcom: Invalid sample rate (%uhz)\n"
            "Allowed sample rates are: 48000, 24000, 16000, 12000, and 8000",
            sampleRate
        );
    }

    if (
        channelCount != 1 && channelCount != 2
    ) {
        panic(
            "OpusBuildCapcom: Invalid channel count (%u)\n"
            "Only one or two channels are allowed.",
            channelCount
        );
    }

    // Calcular el número de muestras por canal
    u32 samplesPerChannel = sampleCount / channelCount;

    // Validar los puntos de loop
    if (loopEnd > samplesPerChannel) {
        warn("OpusBuildCapcom: loop_end exceeds sample count, clamping to sample count");
        loopEnd = samplesPerChannel;
    }
    
    if (loopStart >= loopEnd) {
        warn("OpusBuildCapcom: loop_start >= loop_end, disabling loops");
        loopStart = 0;
        loopEnd = 0;
    }
    
    // Variables para el encoder
    OpusEncoder* encoder = NULL;
    int opusError = 0;
    
    // Inicializar el encoder de OPUS con configuración de alta calidad
    // Basado en el análisis del código de vgmstream
    encoder = opus_encoder_create(sampleRate, channelCount, OPUS_APPLICATION_AUDIO, &opusError);
    if (opusError < 0)
        panic("OpusBuildCapcom: opus_encoder_create failed: %s", opus_strerror(opusError));
    
    // Configurar el encoder con los parámetros óptimos
    opusError = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitRate));
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set Opus bitrate");
    
    opusError = opus_encoder_ctl(encoder, OPUS_SET_VBR(1)); // VBR habilitado
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set Opus VBR");
    
    opusError = opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(0)); // Sin restricción de VBR
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set Opus VBR constraint");
    
    opusError = opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(10)); // Máxima complejidad
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set Opus complexity");
    
    opusError = opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC)); // Optimizado para música
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set Opus signal type");
    
    opusError = opus_encoder_ctl(encoder, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND)); // Máximo ancho de banda
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set Opus bandwidth");
    
    // Valor de preSkip constante para 48kHz como en archivos Capcom
    int preSkipSamples = 312; // Valor observado para 48kHz
    
    // Generación de paquetes Opus - Usar ListData en lugar de List
    ListData packetList;
    ListInit(&packetList, sizeof(u8), 8192); // Buffer más grande para paquetes grandes
    
    // Número de paquetes
    u32 packetCount = 0;
    
    // Codificar los paquetes igualando los tamaños originales
    for (size_t p = 0, i = 0; p < orig_packet_count && i + samplesPerFrame <= sampleCount; ++p, i += samplesPerFrame) {
        u8 buffer[8192];
        int nbBytes = 0;
        int targetSize = orig_packet_sizes[p];
        int try_bitrate = bitRate;
        int tries = 0;
        const int BITRATE_MIN = 8000;
        const int BITRATE_MAX = 512000;
        // Ajustar bitrate para igualar tamaño de paquete (solo para el primer paquete, luego usar mismo bitrate)
        if (p == 0) {
            int best_diff = 999999;
            int best_bytes = 0;
            int best_bitrate = try_bitrate;
            for (tries = 0; tries < 30; ++tries) {
                opus_encoder_ctl(encoder, OPUS_SET_BITRATE(try_bitrate));
                nbBytes = opus_encode(encoder, samples + i, frameSize, buffer, sizeof(buffer));
                if (nbBytes < 0)
                    panic("OpusBuildCapcom: opus_encode failed: %s", opus_strerror(nbBytes));
                int diff = abs(nbBytes - targetSize);
                if (diff < best_diff) {
                    best_diff = diff;
                    best_bytes = nbBytes;
                    best_bitrate = try_bitrate;
                }
                if (nbBytes == targetSize) break;
                if (nbBytes > targetSize) try_bitrate -= 2000;
                else if (nbBytes < targetSize) try_bitrate += 2000;
                if (try_bitrate < BITRATE_MIN || try_bitrate > BITRATE_MAX) break;
            }
            nbBytes = best_bytes;
            opus_encoder_ctl(encoder, OPUS_SET_BITRATE(best_bitrate));
        } else {
            opus_encoder_ctl(encoder, OPUS_SET_BITRATE(try_bitrate));
            nbBytes = opus_encode(encoder, samples + i, frameSize, buffer, sizeof(buffer));
            if (nbBytes < 0)
                panic("OpusBuildCapcom: opus_encode failed: %s", opus_strerror(nbBytes));
        }
        // Si no se logra igualar, se deja el más cercano (nunca mayor que el target)
        if (nbBytes > targetSize) nbBytes = targetSize;

        // Almacenar el tamaño del paquete (big endian)
        u32 packetSizeBE = __builtin_bswap32(nbBytes);
        ListAddRange(&packetList, (u8*)&packetSizeBE, 4);

        // Para el primer paquete, usar el valor específico 0xF0000000
        u32 finalRange;
        if (p == 0) {
            finalRange = 0xF0000000;
        } else {
            opusError = opus_encoder_ctl(encoder, OPUS_GET_FINAL_RANGE(&finalRange));
            if (opusError < 0)
                panic("OpusBuildCapcom: failed to get encoder final range");
            finalRange = __builtin_bswap32(finalRange);
        }

        // Almacenar finalRange (big endian)
        ListAddRange(&packetList, (u8*)&finalRange, 4);

        // Almacenar los datos del paquete
        ListAddRange(&packetList, buffer, nbBytes);

        packetCount++;
    }
    
    opus_encoder_destroy(encoder);
    
    // Ahora construir el archivo OPUS de Capcom completo
    
    // Tamaños específicos del formato Capcom
    u32 capcomHeaderSize = 0x30; // Tamaño del encabezado Capcom
    u32 nintendoHeaderSize = 0x18; // Tamaño del encabezado Nintendo OPUS
    u32 dataChunkHeaderSize = 0x0C; // 4 bytes ceros + 4 bytes tamaño + 4 bytes ID
    
    // Calcular el tamaño total del archivo
    u32 totalSize = capcomHeaderSize + nintendoHeaderSize + dataChunkHeaderSize + packetList.elementCount;
    
    // Crear el archivo de resultado
    MemoryFile result;
    result.size = totalSize;
    result.data_void = malloc(totalSize);
    if (result.data_void == NULL)
        panic("OpusBuildCapcom: failed to allocate file buffer");
    
    // Inicializar a cero toda la memoria
    memset(result.data_void, 0, totalSize);
    
    // Puntero para acceder a bytes
    u8* fileData = (u8*)result.data_void;
    
    // 1. ENCABEZADO CAPCOM (0x00-0x2F)
    
    // 0x00: Número de muestras por canal
    memcpy(fileData + 0x00, &samplesPerChannel, 4);
    
    // 0x04: Número de canales
    memcpy(fileData + 0x04, &channelCount, 4);
    
    // 0x08-0x0F: Información de loop
    if (loopStart == 0 && loopEnd == 0) {
        // Sin loop: FF FF FF FF FF FF FF FF
        memset(fileData + 0x08, 0xFF, 8);
    } else {
        // Con loop: puntos específicos
        memcpy(fileData + 0x08, &loopStart, 4);
        memcpy(fileData + 0x0C, &loopEnd, 4);
    }
    
    // 0x10: Valor especial F8 00 00 00 (constante en todos los archivos Capcom)
    u32 f8Value = 0x000000F8;
    memcpy(fileData + 0x10, &f8Value, 4);
    
    // 0x14-0x1B: Valores específicos
    u32 zeroValue = 0;
    memcpy(fileData + 0x14, &zeroValue, 4);
    memcpy(fileData + 0x18, &zeroValue, 4);
    
    // 0x1C: Offset a datos (30 00 00 00)
    u32 offset30 = 0x30;
    memcpy(fileData + 0x1C, &offset30, 4);
    
    // 0x20-0x2F: Configuración específica (copiada del original)
    memcpy(fileData + 0x20, configData, 16);
    
    // 2. ENCABEZADO NINTENDO OPUS (0x30-0x47)
    
    // 0x30: Chunk ID (01 00 00 80)
    u32 chunkHeaderId = 0x80000001;
    memcpy(fileData + 0x30, &chunkHeaderId, 4);
    
    // 0x34: Chunk size (18 00 00 00)
    u32 chunkSize = 0x18;
    memcpy(fileData + 0x34, &chunkSize, 4);
    
    // 0x38-0x3F: Bytes críticos (copiados del original)
    memcpy(fileData + 0x38, criticalBytes, 8);
    
    // 0x40: Version (00)
    fileData[0x40] = 0;
    
    // 0x41: Channel count
    fileData[0x41] = (u8)channelCount;
    
    // 0x42-0x43: Frame size (0000 para VBR)
    u16 frameSize16 = 0;
    memcpy(fileData + 0x42, &frameSize16, 2);
    
    // 0x44-0x47: Sample rate
    memcpy(fileData + 0x44, &sampleRate, 4);
    
    // 3. DATOS DE OFFSET NINTENDO (0x48-0x53)
    
    // 0x48: Data offset (siempre 18 00 00 00 relativo al header)
    u32 dataOffset = 0x18;
    memcpy(fileData + 0x48, &dataOffset, 4);
    
    // 0x4C-0x4F: Valores específicos
    memset(fileData + 0x4C, 0, 4);
    
    // 0x50-0x51: preSkip (siempre 0x0138 = 312 para 48kHz)
    u16 preSkip = (u16)preSkipSamples;
    memcpy(fileData + 0x50, &preSkip, 2);
    
    // 0x52-0x53: Padding
    memset(fileData + 0x52, 0, 2);
    
    // 4. CHUNK DE DATOS OPUS (0x54-fin)
    
    // Puntero al inicio del chunk de datos
    u8* dataChunk = fileData + 0x48;
    
    // 0x48-0x4B: 4 bytes de ceros
    memset(dataChunk, 0, 4);
    
    // 0x4C-0x4F: Tamaño del chunk de datos
    u32 dataSize = packetList.elementCount;
    memcpy(dataChunk + 4, &dataSize, 4);
    
    // 0x50-0x53: ID del chunk de datos (04 00 00 80)
    u32 dataChunkId = 0x80000004;
    memcpy(dataChunk + 8, &dataChunkId, 4);
    
    // 0x54-fin: Datos de paquetes OPUS
    memcpy(dataChunk + 12, packetList.data, packetList.elementCount);
    
    // Liberar la lista de paquetes
    ListDestroy(&packetList);
    
    return result;
}

#endif // OPUS_PROCESS_H
