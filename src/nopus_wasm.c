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

// Single in-flight result buffer for the minimal WASM interface.
// This wrapper assumes one conversion at a time from a single-threaded browser UI.
static MemoryFile nopusLastResult = {0};
// Default Capcom wrapper values copied from reference sample files in this repository.
// They are good defaults for the included samples, but other Capcom titles may need different values.
static u8 nopusCapcomConfigData[16] = {
    0x00, 0x77, 0xC1, 0x02,
    0x04, 0x00, 0x00, 0x00,
    0xE6, 0x07, 0x0C, 0x0E,
    0x0D, 0x10, 0x23, 0x00
};
static u8 nopusCapcomCriticalBytes[8] = {0x00, 0x02, 0xF8, 0x00, 0x80, 0xBB, 0x00, 0x00};

static void NopusSetResult(MemoryFile* result) {
    MemoryFileDestroy(&nopusLastResult);
    nopusLastResult = *result;
    result->data_void = NULL;
    result->size = 0;
}

EMSCRIPTEN_KEEPALIVE
int nopus_make_wav(u8* inputData, u32 inputSize) {
    if (!inputData || inputSize == 0)
        return 1;

    OpusPreprocess(inputData);

    u32 channelCount = OpusGetChannelCount(inputData);
    u32 sampleRate = OpusGetSampleRate(inputData);

    ListData samples = OpusDecode(inputData);
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
int nopus_make_opus(u8* inputData, u32 inputSize) {
    if (!inputData || inputSize == 0)
        return 1;

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
int nopus_make_capcom_opus(u8* inputData, u32 inputSize, u32 loopStart, u32 loopEnd, int autoLoop) {
    if (!inputData || inputSize == 0)
        return 1;

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

    // configData: 16 game-specific bytes embedded in the Capcom wrapper header.
    // These values are copied from known-good Capcom sample files included in this repo.
    // criticalBytes: fixed header values for the CBR frame unit size and sample rate.
    MemoryFile opus = OpusBuildCapcom(
        samples, sampleCount, sampleRate, channelCount,
        loopStart, loopEnd, nopusCapcomConfigData, nopusCapcomCriticalBytes, NULL, 0
    );
    free(samples);

    if (!opus.data_void || opus.size == 0)
        return 1;

    NopusSetResult(&opus);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int nopus_make_capcom_wav(u8* inputData, u32 inputSize) {
    if (!inputData || inputSize == 0)
        return 1;

    u32 channelCount = OpusCapcomGetChannelCount(inputData);
    u32 sampleRate = OpusCapcomGetSampleRate(inputData);

    ListData samples = OpusDecodeCapcom(inputData);
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
