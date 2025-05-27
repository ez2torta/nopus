#include <stdlib.h>
#include <stdio.h>

#include "files.h"

#include "list.h"

#include "opusProcess.h"

#include "wavProcess.h"


#include <libgen.h>
#include <string.h>
#include "type.h"

// Helper para obtener el nombre base sin extensión
char* baseName(const char* path) {
    static char buf[256];
    snprintf(buf, sizeof(buf), "%s", path);
    char* base = basename(buf);
    char* dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    return base;
}

int main(int argc, char** argv) {
    printf(
        "Nintendo OPUS <-> WAV converter tool v1.2\n"
        "https://github.com/conhlee/nopus\n"
        "\n"
    );

    if (argc < 4) {
        printf("usage: %s <make_wav/make_opus/make_capcom_opus> <file in> <file out> [loop_start loop_end|auto]\n", argv[0]);
        printf("       'auto' can be used to automatically set loop from start to end of audio\n");
        return 1;
    }

    if (strcasecmp(argv[1], "make_wav") == 0) {
        printf("- Converting OPUS at path \"%s\" to WAV at path \"%s\"..\n\n", argv[2], argv[3]);

        MemoryFile mfOpus = MemoryFileCreate(argv[2]);
        if (!mfOpus.data_void || mfOpus.size == 0) {
            printf("Error: Could not read input OPUS file.\n");
            return 1;
        }

        OpusPreprocess(mfOpus.data_u8);

        u32 channelCount = OpusGetChannelCount(mfOpus.data_u8);
        u32 sampleRate = OpusGetSampleRate(mfOpus.data_u8);

        printf("Decoding..");
        fflush(stdout);

        ListData samples = OpusDecode(mfOpus.data_u8);
        if (!samples.data || samples.elementCount == 0) {
            printf("Error: Failed to decode OPUS file.\n");
            MemoryFileDestroy(&mfOpus);
            return 1;
        }

        printf(" OK\n");

        MemoryFileDestroy(&mfOpus);

        printf("Creating WAV..");
        fflush(stdout);

        MemoryFile mfWav = WavBuild(
            (s16*)samples.data, samples.elementCount,
            sampleRate, channelCount
        );

        ListDestroy(&samples);

        if (!mfWav.data_void || mfWav.size == 0) {
            printf("Error: Failed to build WAV file.\n");
            return 1;
        }

        MemoryFileWrite(&mfWav, argv[3]);
        MemoryFileDestroy(&mfWav);

        printf(" OK\n");
    }
    else if (strcasecmp(argv[1], "make_opus") == 0) {
        printf("- Converting WAV at path \"%s\" to OPUS at path \"%s\"..\n\n", argv[2], argv[3]);

        MemoryFile mfWav = MemoryFileCreate(argv[2]);
        if (!mfWav.data_void || mfWav.size == 0) {
            printf("Error: Could not read input WAV file.\n");
            return 1;
        }

        WavPreprocess(mfWav.data_u8, mfWav.size);

        u32 channelCount = WavGetChannelCount(mfWav.data_u8, mfWav.size);
        u32 sampleRate = WavGetSampleRate(mfWav.data_u8, mfWav.size);

        s16* samples = WavGetPCM16(mfWav.data_u8, mfWav.size);
        u32 sampleCount = WavGetSampleCount(mfWav.data_u8, mfWav.size);

        if (!samples || sampleCount == 0) {
            printf("Error: Failed to extract PCM samples from WAV.\n");
            MemoryFileDestroy(&mfWav);
            return 1;
        }

        printf("Encoding..");
        fflush(stdout);

        MemoryFile mfOpus = OpusBuild(samples, sampleCount, sampleRate, channelCount);
        if (!mfOpus.data_void || mfOpus.size == 0) {
            printf("Error: Failed to encode OPUS file.\n");
            free(samples);
            MemoryFileDestroy(&mfWav);
            return 1;
        }

        printf(" OK\n");
        
        free(samples);
        MemoryFileDestroy(&mfWav);

        printf("Writing OPUS..");
        fflush(stdout);

        MemoryFileWrite(&mfOpus, argv[3]);
        MemoryFileDestroy(&mfOpus);

        printf(" OK\n");
    }
    else if (strcasecmp(argv[1], "make_capcom_opus") == 0) {
        printf("- Converting WAV at path \"%s\" to Capcom OPUS at path \"%s\"..\n\n", argv[2], argv[3]);

        // Parse loop points if provided
        u32 loopStart = 0;
        u32 loopEnd = 0;
        int hasLoopPoints = 0;
        int useAutoLoop = 0;
        
        if (argc >= 5) {
            // Check if auto loop was requested
            if (strcmp(argv[4], "auto") == 0) {
                useAutoLoop = 1;
                printf("Auto loop points will be used (0 to end of sample)\n");
            } else if (argc >= 6) {
                loopStart = atoi(argv[4]);
                loopEnd = atoi(argv[5]);
                hasLoopPoints = 1;
                printf("Loop points: start=%u end=%u\n", loopStart, loopEnd);
            } else {
                printf("Warning: Both loop_start and loop_end must be provided. Using no loop points.\n");
            }
        }

        MemoryFile mfWav = MemoryFileCreate(argv[2]);

        WavPreprocess(mfWav.data_u8, mfWav.size);

        u32 channelCount = WavGetChannelCount(mfWav.data_u8, mfWav.size);
        u32 sampleRate = WavGetSampleRate(mfWav.data_u8, mfWav.size);

        s16* samples = WavGetPCM16(mfWav.data_u8, mfWav.size);
        u32 sampleCount = WavGetSampleCount(mfWav.data_u8, mfWav.size);

        // Calcular el número de muestras por canal para los puntos de loop
        u32 samplesPerChannel = sampleCount / channelCount;
        
        // For auto loop mode
        if (useAutoLoop) {
            loopStart = 0;
            loopEnd = samplesPerChannel;
            hasLoopPoints = 1;
            printf("Using auto loop points: start=0 end=%u samples (%0.3f seconds)\n", 
                   samplesPerChannel, (float)samplesPerChannel / sampleRate);
        }
        // Validate loop points if provided manually
        else if (hasLoopPoints) {
            if (loopEnd > samplesPerChannel) {
                printf("Warning: Loop end exceeds sample count. Clamping to %u samples.\n", samplesPerChannel);
                loopEnd = samplesPerChannel;
            }
            
            if (loopStart >= loopEnd) {
                printf("Warning: Loop start must be less than loop end. Disabling loops.\n");
                loopStart = 0;
                loopEnd = 0;
                hasLoopPoints = 0;
            } else {
                printf("Validated loop points: start=%u end=%u samples (%0.3f to %0.3f seconds)\n",
                       loopStart, loopEnd, (float)loopStart / sampleRate, (float)loopEnd / sampleRate);
            }
        }

        printf("Encoding to Capcom OPUS format..");
        fflush(stdout);

        // Si no hay puntos de loop, se pasan como 0
        if (!hasLoopPoints) {
            loopStart = 0;
            loopEnd = 0;
        }

        // (Llamada antigua eliminada, solo se usa la versión extendida más abajo)
        // Leer el archivo OPUS original para extraer header y tamaños de paquetes
        char original_opus_path[512];
        snprintf(original_opus_path, sizeof(original_opus_path), "opus_originales/%s.opus", baseName(argv[2]));
        MemoryFile mfOrigOpus = MemoryFileCreate(original_opus_path);
        if (!mfOrigOpus.data_void || mfOrigOpus.size == 0) {
            printf("Error: Could not read original OPUS file for header/packet matching.\n");
            free(samples);
            MemoryFileDestroy(&mfWav);
            return 1;
        }

        // Extraer configData (0x20, 16 bytes) y criticalBytes (0x38, 8 bytes)
        u8 configData[16];
        u8 criticalBytes[8];
        memcpy(configData, (u8*)mfOrigOpus.data_void + 0x20, 16);
        memcpy(criticalBytes, (u8*)mfOrigOpus.data_void + 0x38, 8);

        // Extraer tamaños de paquetes del original
        // Buscar el offset del primer paquete (header Capcom + header Nintendo + chunk header)
        size_t packet_data_offset = 0x30 + 0x18 + 0x0C;
        size_t orig_size = mfOrigOpus.size;
        size_t pos = packet_data_offset;
        #define MAX_PACKETS 4096
        u32 orig_packet_sizes[MAX_PACKETS];
        size_t orig_packet_count = 0;
        while (pos + 8 <= orig_size && orig_packet_count < MAX_PACKETS) {
            u32 sz = (mfOrigOpus.data_u8[pos] << 24) | (mfOrigOpus.data_u8[pos+1] << 16) | (mfOrigOpus.data_u8[pos+2] << 8) | (mfOrigOpus.data_u8[pos+3]);
            orig_packet_sizes[orig_packet_count++] = sz;
            u32 skip = sz + 8;
            pos += skip;
        }

        MemoryFile mfOpus = OpusBuildCapcom(samples, sampleCount, sampleRate, channelCount, loopStart, loopEnd, configData, criticalBytes, orig_packet_sizes, orig_packet_count);
        MemoryFileDestroy(&mfOrigOpus);
        if (!mfOpus.data_void || mfOpus.size == 0) {
            printf("Error: Failed to encode Capcom OPUS file.\n");
            free(samples);
            MemoryFileDestroy(&mfWav);
            return 1;
        }

        printf(" OK\n");
        
        free(samples);
        MemoryFileDestroy(&mfWav);

        printf("Writing Capcom OPUS..");
        fflush(stdout);

        MemoryFileWrite(&mfOpus, argv[3]);
        MemoryFileDestroy(&mfOpus);

        printf(" OK\n");
    }
    else {
        printf("Unknown command '%s'\n", argv[1]);
        printf("Use make_wav, make_opus or make_capcom_opus\n");
        return 1;
    }

    printf("\nAll done.\n");
}