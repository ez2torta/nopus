#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opusProcess.h"
#include "files.h"
#include "common.h"
#include "wavProcess.h"

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Uso: %s <entrada.wav> <salida.opus> <loop_start> <loop_end>\n", argv[0]);
        printf("     %s <entrada.wav> <salida.opus> auto\n", argv[0]);
        printf("       'auto' establece loop desde 0 hasta el final del audio\n");
        return 1;
    }

    const char *input_file  = argv[1];
    const char *output_file = argv[2];

    u32 loopStart    = 0;
    u32 loopEnd      = 0;
    int hasLoopPoints = 0;
    int useAutoLoop   = 0;

    if (argc >= 4) {
        if (strcmp(argv[3], "auto") == 0) {
            useAutoLoop = 1;
        } else if (argc >= 5) {
            char *endptr;
            loopStart = (u32)strtoul(argv[3], &endptr, 10);
            if (*endptr != '\0') {
                printf("Error: loop_start no es un número válido: '%s'\n", argv[3]);
                return 1;
            }
            loopEnd = (u32)strtoul(argv[4], &endptr, 10);
            if (*endptr != '\0') {
                printf("Error: loop_end no es un número válido: '%s'\n", argv[4]);
                return 1;
            }
            hasLoopPoints = 1;
        } else {
            printf("Advertencia: se necesitan loop_start y loop_end. Se usará sin puntos de loop.\n");
        }
    }

    // Leer archivo WAV
    MemoryFile mfWav = MemoryFileCreate(input_file);
    if (!mfWav.data_void || mfWav.size == 0) {
        printf("Error: No se pudo leer el archivo de entrada '%s'\n", input_file);
        return 1;
    }

    WavPreprocess(mfWav.data_u8, mfWav.size);

    u32 channelCount = WavGetChannelCount(mfWav.data_u8, mfWav.size);
    u32 sampleRate   = WavGetSampleRate(mfWav.data_u8, mfWav.size);
    s16 *samples     = WavGetPCM16(mfWav.data_u8, mfWav.size);
    u32 sampleCount  = WavGetSampleCount(mfWav.data_u8, mfWav.size);

    printf("WAV: muestras=%u, canales=%u, sampleRate=%u\n",
           sampleCount, channelCount, sampleRate);

    if (!samples || sampleCount == 0 || channelCount == 0) {
        printf("Error: No se pudo extraer PCM del archivo WAV\n");
        MemoryFileDestroy(&mfWav);
        return 1;
    }

    u32 samplesPerChannel = sampleCount / channelCount;

    if (useAutoLoop) {
        loopStart    = 0;
        loopEnd      = samplesPerChannel;
        hasLoopPoints = 1;
        printf("Loop automático: inicio=0 fin=%u muestras (%.3f segundos)\n",
               samplesPerChannel, (float)samplesPerChannel / sampleRate);
    } else if (hasLoopPoints) {
        if (loopEnd > samplesPerChannel) {
            printf("Advertencia: loop_end excede las muestras disponibles. Se ajusta a %u.\n",
                   samplesPerChannel);
            loopEnd = samplesPerChannel;
        }
        if (loopStart >= loopEnd) {
            printf("Advertencia: loop_start debe ser menor que loop_end. Se desactivan los loops.\n");
            loopStart    = 0;
            loopEnd      = 0;
            hasLoopPoints = 0;
        } else {
            printf("Loop: inicio=%u fin=%u muestras (%.3f a %.3f segundos)\n",
                   loopStart, loopEnd,
                   (float)loopStart / sampleRate,
                   (float)loopEnd   / sampleRate);
        }
    }

    if (!hasLoopPoints) {
        loopStart = 0;
        loopEnd   = 0;
    }

    printf("Codificando a formato Capcom OPUS..");
    fflush(stdout);

    // configData: 16 game-specific bytes embedded in the Capcom header (offset 0x20).
    // These values match the original Capcom game files and are ignored by vgmstream.
    // criticalBytes: 8 bytes at offset 0x10 of the Capcom header encoding the CBR
    // frame unit size (0xF8 = 248 bytes = 240-byte Opus packet + 8-byte packet header)
    // and the sample rate (0xBB80 = 48000 Hz).
    u8 configData[16]   = {0x00, 0x77, 0xC1, 0x02, 0x04, 0x00, 0x00, 0x00,
                           0xE6, 0x07, 0x0C, 0x0E, 0x0D, 0x10, 0x23, 0x00};
    u8 criticalBytes[8] = {0x00, 0x02, 0xF8, 0x00, 0x80, 0xBB, 0x00, 0x00};

    MemoryFile mfOpus = OpusBuildCapcom(
        samples, sampleCount, sampleRate, channelCount,
        loopStart, loopEnd, configData, criticalBytes, NULL, 0
    );

    free(samples);
    MemoryFileDestroy(&mfWav);

    if (!mfOpus.data_void || mfOpus.size == 0) {
        printf(" Error: No se pudo codificar el archivo Capcom OPUS\n");
        return 1;
    }

    printf(" OK\n");
    printf("Escribiendo '%s'..", output_file);
    fflush(stdout);

    MemoryFileWrite(&mfOpus, output_file);
    MemoryFileDestroy(&mfOpus);

    printf(" OK\n\nListo.\n");
    return 0;
}