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
    u32 chunkSize; // Not including chunkId and chunkSize. Usually 0x18.

    u8 version; // Compare to OPUS_VERSION.

    u8 channelCount;

    u16 frameSize; // Frame size if constant bitrate, 0 if variable bitrate.

    u32 sampleRate;

    u32 dataOffset;
    u32 _unk14; // 'frame data offset' (seek table? not seen)
    u32 contextOffset;

    u16 preSkipSamples; // Pre-skip sample count.

    u16 _pad16;
} OpusFileHeader;

typedef struct __attribute__((packed)) {
    u32 chunkId; // Compare to CHUNK_DATA_ID.
    u32 chunkSize; // Not including chunkId and chunkSize.

    u8 data[0]; // Dynamically sized OpusFrames.
} OpusDataChunk;

typedef struct __attribute__((packed)) {
    u32 length;
    u32 finalRange;
    u8 data[0];
} OpusFrame;

typedef struct __attribute__((packed)) {
    u32 chunkId;
    u32 chunkSize; // Exclusive of chunkId and chunkSize.
} OpusDummyChunk;

void* _OpusFindChunk(u8* opusData, u8* opusDataEnd, u32 chunkId) {
    OpusDummyChunk* currentChunk = (OpusDummyChunk*)opusData;
    while (opusDataEnd ? ((u8*)currentChunk < opusDataEnd) : 1) {
        if (currentChunk->chunkId == chunkId)
            return (void*)currentChunk;
        
        currentChunk = (OpusDummyChunk*)(
            (u8*)(currentChunk + 1) + currentChunk->chunkSize
        );
    }

    return NULL;
}

void OpusPreprocess(u8* opusData, u8* opusDataEnd) {
    OpusFileHeader* fileHeader = (OpusFileHeader*)opusData;

    if (fileHeader->chunkId == OGG_OPUS_ID)
        panic("A Ogg Opus file was passed; please pass in a Nintendo Opus file.");
    if (fileHeader->chunkId != CHUNK_HEADER_ID)
        panic("OPUS file header ID is nonmatching");

    if (fileHeader->contextOffset != 0)
        warn("OPUS context is present but will be ignored");

    OpusDataChunk* dataChunk = _OpusFindChunk(opusData, opusDataEnd, CHUNK_DATA_ID);
    if (!dataChunk)
        panic("OPUS data chunk was not found");
}

u32 OpusGetChannelCount(u8* opusData) {
    return ((OpusFileHeader*)opusData)->channelCount;
}
u32 OpusGetSampleRate(u8* opusData) {
    return ((OpusFileHeader*)opusData)->sampleRate;
}

ListData OpusDecode(u8* opusData) {
    OpusFileHeader* fileHeader = (OpusFileHeader*)opusData;
    OpusDataChunk* dataChunk = _OpusFindChunk(opusData, NULL, CHUNK_DATA_ID);

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

    while (offset < dataChunk->chunkSize) {
        OpusFrame* opusFrame = (OpusFrame*)(dataChunk->data + offset);
        u32 length = __builtin_bswap32(opusFrame->length);

        int samplesDecoded = opus_decode(decoder, opusFrame->data, length, tempSamples, coFrameSize, 0);
        if (samplesDecoded < 0)
            panic("OpusProcess: opus_decode fail: %s", opus_strerror(samplesDecoded));

        ListAddRange(&samples, tempSamples, samplesDecoded * fileHeader->channelCount);

        offset += sizeof(OpusFrame) + length;
    }

    opus_decoder_destroy(decoder);
    
    free(tempSamples);

    return samples;
}

#endif // OPUS_PROCESS_H
