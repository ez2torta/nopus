#ifndef WAVPROCESS_H
#define WAVPROCESS_H

#include <stdlib.h>

#include <string.h>

#include "files.h"

#include "type.h"

#include "common.h"

#define RIFF_MAGIC IDENTIFIER_TO_U32('R', 'I', 'F', 'F')
#define WAVE_MAGIC IDENTIFIER_TO_U32('W', 'A', 'V', 'E')
#define FMT__MAGIC IDENTIFIER_TO_U32('f', 'm', 't', ' ')
#define DATA_MAGIC IDENTIFIER_TO_U32('d', 'a', 't', 'a')

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
} WavDataChunk;

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

    s16* dataStart = (s16*)(wavDataChunk + 1);

    wavFileHeader->riffMagic = RIFF_MAGIC;
    wavFileHeader->waveMagic = WAVE_MAGIC;

    wavFmtChunk->magic = FMT__MAGIC;
    wavFmtChunk->chunkSize = 16; // Size of the fmt chunk (16 for PCM)

    wavFmtChunk->format = 0x0001; // PCM format
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
