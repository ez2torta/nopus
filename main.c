#include <stdlib.h>
#include <stdio.h>

#include "files.h"

#include "list.h"

#include "opusProcess.h"

#include "wavProcess.h"

#include "type.h"

int main(int argc, char** argv) {
    printf(
        "Nintendo OPUS <-> WAV converter tool v1.2\n"
        "https://github.com/conhlee/nopus\n"
        "\n"
    );

    if (argc < 4) {
        printf("usage: %s <make_wav/make_opus> <file in> <file out>\n", argv[0]);
        return 1;
    }

    if (strcasecmp(argv[1], "make_wav") == 0) {
        printf("- Converting OPUS at path \"%s\" to WAV at path \"%s\"..\n\n", argv[2], argv[3]);

        MemoryFile mfOpus = MemoryFileCreate(argv[2]);

        OpusPreprocess(mfOpus.data_u8);

        u32 channelCount = OpusGetChannelCount(mfOpus.data_u8);
        u32 sampleRate = OpusGetSampleRate(mfOpus.data_u8);

        printf("Decoding..");
        fflush(stdout);

        ListData samples = OpusDecode(mfOpus.data_u8);

        printf(" OK\n");

        MemoryFileDestroy(&mfOpus);

        printf("Creating WAV..");
        fflush(stdout);

        MemoryFile mfWav = WavBuild(
            (s16*)samples.data, samples.elementCount,
            sampleRate, channelCount
        );

        ListDestroy(&samples);

        MemoryFileWrite(&mfWav, argv[3]);

        MemoryFileDestroy(&mfWav);

        printf(" OK\n");
    }
    else if (strcasecmp(argv[1], "make_opus") == 0) {
        printf("- Converting WAV at path \"%s\" to OPUS at path \"%s\"..\n\n", argv[2], argv[3]);

        MemoryFile mfWav = MemoryFileCreate(argv[2]);

        WavPreprocess(mfWav.data_u8, mfWav.size);

        u32 channelCount = WavGetChannelCount(mfWav.data_u8, mfWav.size);
        u32 sampleRate = WavGetSampleRate(mfWav.data_u8, mfWav.size);

        s16* samples = WavGetPCM16(mfWav.data_u8, mfWav.size);
        u32 sampleCount = WavGetSampleCount(mfWav.data_u8, mfWav.size);

        printf("Encoding..");
        fflush(stdout);

        MemoryFile mfOpus = OpusBuild(samples, sampleCount, sampleRate, channelCount);

        printf(" OK\n");
        
        free(samples);
        MemoryFileDestroy(&mfWav);

        printf("Writing OPUS..");
        fflush(stdout);

        MemoryFileWrite(&mfOpus, argv[3]);

        MemoryFileDestroy(&mfOpus);

        printf(" OK\n");
    }

    printf("\nAll done.\n");
}