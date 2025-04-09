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

#define OPUS_DEFAULT_BITRATE (96000)

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

#endif // OPUS_PROCESS_H
