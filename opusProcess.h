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

    u8 data[0]; // Dynamically sized OpusFrames.
} OpusDataChunk;

typedef struct __attribute__((packed)) {
    u32 packetSize; // Length of the Opus packet. In big-endian for some reason.
    u32 finalRange; // Not sure what this is ..
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

#endif // OPUS_PROCESS_H
