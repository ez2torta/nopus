#include <stdlib.h>
#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include "files.h"
#include "list.h"
#include "opusProcess.h"
#include "type.h"
#include "wavProcess.h"

static MemoryFile nopusLastResult = {0};

static void NopusSetResult(MemoryFile* result) {
    MemoryFileDestroy(&nopusLastResult);
    nopusLastResult = *result;
    result->data_void = NULL;
    result->size = 0;
}

EMSCRIPTEN_KEEPALIVE
int nopus_make_wav(const u8* inputData, u32 inputSize) {
    (void)inputSize;

    OpusPreprocess((u8*)inputData);

    u32 channelCount = OpusGetChannelCount((u8*)inputData);
    u32 sampleRate = OpusGetSampleRate((u8*)inputData);

    ListData samples = OpusDecode((u8*)inputData);
    if (!samples.data || samples.elementCount == 0)
        return 1;

    MemoryFile wav = WavBuild((s16*)samples.data, samples.elementCount, sampleRate, channelCount);
    ListDestroy(&samples);

    if (!wav.data_void || wav.size == 0)
        return 1;

    NopusSetResult(&wav);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int nopus_make_opus(const u8* inputData, u32 inputSize) {
    WavPreprocess(inputData, inputSize);

    u32 channelCount = WavGetChannelCount(inputData, inputSize);
    u32 sampleRate = WavGetSampleRate(inputData, inputSize);
    s16* samples = WavGetPCM16(inputData, inputSize);
    u32 sampleCount = WavGetSampleCount(inputData, inputSize);

    if (!samples || sampleCount == 0)
        return 1;

    MemoryFile opus = OpusBuild(samples, sampleCount, sampleRate, channelCount);
    free(samples);

    if (!opus.data_void || opus.size == 0)
        return 1;

    NopusSetResult(&opus);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int nopus_make_capcom_opus(const u8* inputData, u32 inputSize, u32 loopStart, u32 loopEnd, int autoLoop) {
    WavPreprocess(inputData, inputSize);

    u32 channelCount = WavGetChannelCount(inputData, inputSize);
    u32 sampleRate = WavGetSampleRate(inputData, inputSize);
    s16* samples = WavGetPCM16(inputData, inputSize);
    u32 sampleCount = WavGetSampleCount(inputData, inputSize);
    u32 samplesPerChannel = channelCount > 0 ? sampleCount / channelCount : 0;

    if (!samples || sampleCount == 0 || channelCount == 0)
        return 1;

    if (autoLoop) {
        loopStart = 0;
        loopEnd = samplesPerChannel;
    }

    u8 configData[16] = {
        0x00, 0x77, 0xC1, 0x02,
        0x04, 0x00, 0x00, 0x00,
        0xE6, 0x07, 0x0C, 0x0E,
        0x0D, 0x10, 0x23, 0x00
    };
    u8 criticalBytes[8] = {0x00, 0x02, 0xF8, 0x00, 0x80, 0xBB, 0x00, 0x00};

    MemoryFile opus = OpusBuildCapcom(
        samples, sampleCount, sampleRate, channelCount,
        loopStart, loopEnd, configData, criticalBytes, NULL, 0
    );
    free(samples);

    if (!opus.data_void || opus.size == 0)
        return 1;

    NopusSetResult(&opus);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int nopus_make_capcom_wav(const u8* inputData, u32 inputSize) {
    (void)inputSize;

    u32 channelCount = OpusCapcomGetChannelCount(inputData);
    u32 sampleRate = OpusCapcomGetSampleRate(inputData);

    ListData samples = OpusDecodeCapcom((u8*)inputData);
    if (!samples.data || samples.elementCount == 0)
        return 1;

    MemoryFile wav = WavBuild((s16*)samples.data, samples.elementCount, sampleRate, channelCount);
    ListDestroy(&samples);

    if (!wav.data_void || wav.size == 0)
        return 1;

    NopusSetResult(&wav);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
uintptr_t nopus_result_ptr(void) {
    return (uintptr_t)nopusLastResult.data_u8;
}

EMSCRIPTEN_KEEPALIVE
u32 nopus_result_size(void) {
    return (u32)nopusLastResult.size;
}

EMSCRIPTEN_KEEPALIVE
void nopus_result_free(void) {
    MemoryFileDestroy(&nopusLastResult);
}
