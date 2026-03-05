#ifndef WAVPROCESS_H
#define WAVPROCESS_H

#include <stdlib.h>

#include <string.h>

#include <math.h>

#include "files.h"

#include "type.h"

#include "common.h"

#define RIFF_MAGIC IDENTIFIER_TO_U32('R', 'I', 'F', 'F')
#define WAVE_MAGIC IDENTIFIER_TO_U32('W', 'A', 'V', 'E')
#define FMT__MAGIC IDENTIFIER_TO_U32('f', 'm', 't', ' ')
#define DATA_MAGIC IDENTIFIER_TO_U32('d', 'a', 't', 'a')

#define FMT_FORMAT_PCM (0x0001)
#define FMT_FORMAT_FLOAT (0x0003)

typedef struct __attribute__((packed)) {
    u32 riffMagic; // Compare to RIFF_MAGIC
    u32 fileSize; // Not including magic and fileSize

    u32 waveMagic; // Compare to WAVE_MAGIC
} WavFileHeader;

typedef struct __attribute__((packed)) {
    u32 magic; // Compare to FMT__MAGIC
    u32 chunkSize; // 16 in our case, but could be different

    u16 format; // 0x0001 = PCM, 0x0003 = Float, 0x0006 = A-Law, 0x0007 = Mu-Law

    u16 channelCount;
    u32 sampleRate;

    u32 dataRate; // Bytes per second (sampleRate * sampleSize * channelCount)

    u16 blockSize; // sampleSize * channelCount

    u16 bitsPerSample; // 8 * sampleSize
} WavFmtChunk;

typedef struct __attribute__((packed)) {
    u32 magic; // Compare to DATA_MAGIC
    u32 chunkSize;

    u8 data[0];
} WavDataChunk;

typedef struct __attribute((packed)) {
    u32 magic;
    u32 chunkSize;
} WavChunkHeader;

const u8* _WavFindChunk(const u8* chunksStart, u32 chunksSize, u32 targetMagic) {
    const u8* ptr = chunksStart;
    const u8* end = chunksStart + chunksSize;

    while (ptr + sizeof(WavChunkHeader) <= end) {
        const WavChunkHeader* chunk = (const WavChunkHeader*)ptr;
        if (chunk->magic == targetMagic)
            return ptr;

        // Advance to next chunk
        ptr += sizeof(WavChunkHeader) + chunk->chunkSize;
        // Align to 2b
        if (chunk->chunkSize % 2 != 0)
            ptr++;
    }


    char* magicChars = (char*)&targetMagic;
    panic(
        "WAV '%c%c%c%c' chunk not found",
        magicChars[0], magicChars[1], magicChars[2], magicChars[3]
    );
}

void WavPreprocess(const u8* wavData, u32 wavDataSize) {
    if (!wavData)
        panic("WAV data is NULL");

    const WavFileHeader* fileHeader = (const WavFileHeader*)wavData;
    if (fileHeader->riffMagic != RIFF_MAGIC)
        panic("WAV RIFF magic is nonmatching");
    if (fileHeader->waveMagic != WAVE_MAGIC)
        panic("WAV WAVE magic is nonmatching");

    const u8* chunksStart = wavData + sizeof(WavFileHeader);
    u32 chunksSize = wavDataSize - sizeof(WavFileHeader);

    const WavFmtChunk* fmtChunk = (const WavFmtChunk*)_WavFindChunk(
        chunksStart, chunksSize,
        FMT__MAGIC
    );

    if (fmtChunk->format != FMT_FORMAT_PCM && fmtChunk->format != FMT_FORMAT_FLOAT)
        panic("WAV format is unsupported: %u", fmtChunk->format);

    if (
        fmtChunk->format == FMT_FORMAT_PCM &&
        fmtChunk->bitsPerSample != 16 && fmtChunk->bitsPerSample != 24
    )
        panic("%u-bit PCM isn't supported (expected 32-bit FLOAT, 16-bit PCM, or 24-bit PCM)", (unsigned)fmtChunk->bitsPerSample);
    else if (fmtChunk->format == FMT_FORMAT_FLOAT && fmtChunk->bitsPerSample != 32)
        panic("%u-bit FLOAT isn't supported (expected 32-bit FLOAT, 16-bit PCM, or 24-bit PCM)", (unsigned)fmtChunk->bitsPerSample);
}

u32 WavGetSampleRate(const u8* wavData, u32 wavDataSize) {
    const WavFmtChunk* fmtChunk = (const WavFmtChunk*)_WavFindChunk(
        wavData + sizeof(WavFileHeader),
        wavDataSize - sizeof(WavFileHeader),
        FMT__MAGIC
    );

    return fmtChunk->sampleRate;
}

u16 WavGetChannelCount(const u8* wavData, u32 wavDataSize) {
    const WavFmtChunk* fmtChunk = (const WavFmtChunk*)_WavFindChunk(
        wavData + sizeof(WavFileHeader),
        wavDataSize - sizeof(WavFileHeader),
        FMT__MAGIC
    );

    return fmtChunk->channelCount;
}

int WavGetSamplesAreFloat(const u8* wavData, u32 wavDataSize) {
    const WavFmtChunk* fmtChunk = (const WavFmtChunk*)_WavFindChunk(
        wavData + sizeof(WavFileHeader),
        wavDataSize - sizeof(WavFileHeader),
        FMT__MAGIC
    );

    return fmtChunk->format == FMT_FORMAT_FLOAT;
}

// In bytes.
u16 WavGetSampleSize(const u8* wavData, u32 wavDataSize) {
    const WavFmtChunk* fmtChunk = (const WavFmtChunk*)_WavFindChunk(
        wavData + sizeof(WavFileHeader),
        wavDataSize - sizeof(WavFileHeader),
        FMT__MAGIC
    );
    
    return fmtChunk->bitsPerSample / 8;
}

void* WavGetData(const u8* wavData, u32 wavDataSize) {
    const WavDataChunk* dataChunk = (const WavDataChunk*)_WavFindChunk(
        wavData + sizeof(WavFileHeader),
        wavDataSize - sizeof(WavFileHeader),
        DATA_MAGIC
    );

    return (void*)dataChunk->data;
}

u32 WavGetDataSize(const u8* wavData, u32 wavDataSize) {
    const WavDataChunk* dataChunk = (const WavDataChunk*)_WavFindChunk(
        wavData + sizeof(WavFileHeader),
        wavDataSize - sizeof(WavFileHeader),
        DATA_MAGIC
    );

    return dataChunk->chunkSize;
}

u32 WavGetSampleCount(const u8* wavData, u32 wavDataSize) {
    const u8* chunksStart = wavData + sizeof(WavFileHeader);
    u32 chunksSize = wavDataSize - sizeof(WavFileHeader);

    const WavFmtChunk* fmtChunk = (const WavFmtChunk*)_WavFindChunk(
        chunksStart, chunksSize,
        FMT__MAGIC
    );
    const WavDataChunk* dataChunk = (const WavDataChunk*)_WavFindChunk(
        chunksStart, chunksSize,
        DATA_MAGIC
    );

    return dataChunk->chunkSize / (fmtChunk->bitsPerSample / 8);
}

// Dynamically allocated.
s16* WavGetPCM16(const u8* wavData, u32 wavDataSize) {
    const u8* chunksStart = wavData + sizeof(WavFileHeader);
    u32 chunksSize = wavDataSize - sizeof(WavFileHeader);

    const WavFmtChunk* fmtChunk = (const WavFmtChunk*)_WavFindChunk(
        chunksStart, chunksSize,
        FMT__MAGIC
    );
    const WavDataChunk* dataChunk = (const WavDataChunk*)_WavFindChunk(
        chunksStart, chunksSize,
        DATA_MAGIC
    );

    u32 sampleCount = dataChunk->chunkSize / (fmtChunk->bitsPerSample / 8);

    s16* dstSamples = malloc(sizeof(s16) * sampleCount);
    if (dstSamples == NULL)
        panic("WavGetPCM16: failed to allocate result buf");

    // FLOAT.
    if (fmtChunk->format == FMT_FORMAT_FLOAT) {
        const float* srcSamples = (const float*)dataChunk->data;

        for (unsigned i = 0; i < sampleCount; i++) {
            float sample = srcSamples[i] * 32768.f;
    
            if (sample > 32767.f)
                sample = 32767.f;
            else if (sample < -32768.f)
                sample = -32768.f;
    
            dstSamples[i] = (s16)roundf(sample);
        }
    }
    // PCM16. Simply copy.
    else if (fmtChunk->format == FMT_FORMAT_PCM && fmtChunk->bitsPerSample == 16) {
        memcpy(dstSamples, dataChunk->data, sizeof(s16) * sampleCount);
    }
    // PCM24.
    else if (fmtChunk->format == FMT_FORMAT_PCM && fmtChunk->bitsPerSample == 24) {
        const u8* srcSamples = (const u8*)dataChunk->data;

        for (unsigned i = 0; i < sampleCount; i++) {
            // :[
            s32 sample = ((s32)(srcSamples[i * 3 + 0]) << 8)  | 
                         ((s32)(srcSamples[i * 3 + 1]) << 16) | 
                         ((s32)(srcSamples[i * 3 + 2]) << 24);

            s32 normalizedSample = (sample >> 8);
            if (normalizedSample > 32767)
                normalizedSample = 32767;
            else if (normalizedSample < -32768)
                normalizedSample = -32768;

            dstSamples[i] = (s16)normalizedSample;
        }
    }
    else
        panic("WavGetPCM16: no convert condition met");
    
    return dstSamples;
}

// Requires PCM16 samples.
MemoryFile WavBuild(s16* samples, u32 sampleCount, u32 sampleRate, u16 channelCount) {
    MemoryFile mfResult;

    u32 dataSize = sampleCount * sizeof(s16);
    u32 fileSize =
        sizeof(WavFileHeader) + sizeof(WavFmtChunk) +
        sizeof(WavDataChunk) + dataSize;

    mfResult.data_void = malloc(fileSize);
    if (!mfResult.data_void)
        panic("WavBuild: failed to allocate memfile");
    mfResult.size = fileSize;

    WavFileHeader* wavFileHeader = (WavFileHeader*)mfResult.data_void;
    WavFmtChunk* wavFmtChunk = (WavFmtChunk*)(wavFileHeader + 1);
    WavDataChunk* wavDataChunk = (WavDataChunk*)(wavFmtChunk + 1);

    s16* dataStart = (s16*)wavDataChunk->data;

    wavFileHeader->riffMagic = RIFF_MAGIC;
    wavFileHeader->waveMagic = WAVE_MAGIC;

    wavFmtChunk->magic = FMT__MAGIC;
    wavFmtChunk->chunkSize = 16; // Size of the fmt chunk (16 for PCM)

    wavFmtChunk->format = FMT_FORMAT_PCM;
    wavFmtChunk->channelCount = channelCount;
    wavFmtChunk->sampleRate = sampleRate;
    wavFmtChunk->dataRate = sampleRate * sizeof(s16) * channelCount;
    wavFmtChunk->blockSize = sizeof(s16) * channelCount;
    wavFmtChunk->bitsPerSample = 8 * sizeof(s16);

    wavDataChunk->magic = DATA_MAGIC;
    wavDataChunk->chunkSize = dataSize;

    wavFileHeader->fileSize = fileSize - 8; // Subtract 8 for RIFF header size
    
    memcpy(dataStart, samples, dataSize);

    return mfResult;
}

#endif // WAVPROCESS_H
