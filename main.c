#include <stdlib.h>
#include <stdio.h>

#include "files.h"

#include "list.h"

#include "opusProcess.h"

#include "wavProcess.h"

#include "type.h"

int main(int argc, char** argv) {
    printf(
        "Nintendo OPUS to WAV converter tool v1.0\n"
        "https://github.com/conhlee/nopus\n"
        "\n"
    );

    if (argc < 3) {
        printf("usage: %s <opus file in> <wav file out>\n", argv[0]);
        return 1;
    }

    printf("- Converting OPUS at path \"%s\" to WAV at path \"%s\"..\n\n", argv[1], argv[2]);

    MemoryFile mfOpus = MemoryFileCreate(argv[1]);

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

    MemoryFileWrite(&mfWav, argv[2]);

    MemoryFileDestroy(&mfWav);

    printf(" OK\n");

    printf("\nAll done.\n");
}