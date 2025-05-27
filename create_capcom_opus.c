#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opusProcess.h"
#include "files.h"
#include "common.h"
#include "wavProcess.h"

// Constantes específicas para el formato Capcom
#define CAPCOM_HEADER_SIZE 0x30

// Función para extraer muestras de un archivo WAV
typedef struct {
    s16* samples;
    u32 sampleCount;
    u32 channelCount;
    u32 sampleRate;
} WavData;

WavData ExtractWavSamples(const u8* wavData, size_t size) {
    WavData result = {0};
    
    // Verificar formato WAV básico (simplificado)
    if (size < 44 || memcmp(wavData, "RIFF", 4) != 0 || memcmp(wavData + 8, "WAVE", 4) != 0) {
        printf("Error: No es un archivo WAV válido\n");
        return result;
    }
    
    // Extraer información de formato (típicamente en offset 20-36)
    result.channelCount = wavData[22] | (wavData[23] << 8);
    result.sampleRate = wavData[24] | (wavData[25] << 8) | (wavData[26] << 16) | (wavData[27] << 24);
    
    // Encontrar chunk 'data'
    u32 pos = 12; // Después de "WAVE"
    while (pos + 8 < size) {
        if (memcmp(wavData + pos, "data", 4) == 0) {
            u32 dataSize = wavData[pos + 4] | (wavData[pos + 5] << 8) | 
                          (wavData[pos + 6] << 16) | (wavData[pos + 7] << 24);
            
            result.samples = (s16*)(wavData + pos + 8);
            result.sampleCount = dataSize / 2; // 16 bits = 2 bytes por muestra
            break;
        }
        
        // Avanzar al siguiente chunk
        u32 chunkSize = wavData[pos + 4] | (wavData[pos + 5] << 8) | 
                       (wavData[pos + 6] << 16) | (wavData[pos + 7] << 24);
        pos += 8 + chunkSize;
        pos = (pos + 1) & ~1; // Alineación a 2 bytes
    }
    
    return result;
}

// Función simplificada para convertir WAV a Opus
// En un caso real, usaríamos libopus para codificar correctamente
// Esta es una versión simplificada para la demostración
u8* SimpleOpusEncoding(s16* samples, u32 sampleCount, u32 channelCount, size_t* outSize) {
    // Este es un placeholder - no es codificación real
    // En una implementación real, llamaríamos a libopus
    
    // Creamos un encabezado Opus básico - asegurando espacio suficiente
    size_t requiredSize = sampleCount * 2 + 0x38; // Espacio para datos + encabezado
    u8* opusData = malloc(requiredSize);
    if (!opusData) return NULL;
    
    // Inicializar memoria para prevenir accesos a memoria no inicializada
    memset(opusData, 0, requiredSize);
    
    // Cabecera estándar de Opus para Nintendo Switch (simplificada)
    u32* header = (u32*)opusData;
    header[0] = 0x80000001; // 'basic info' chunk ID
    header[1] = 0x24;       // chunk size
    header[2] = 0x00000000; // version (0)
    opusData[9] = channelCount; // channels
    header[3] = 48000;      // sample rate
    header[4] = 0x00000030; // data offset desde inicio
    header[5] = 0x00000000; // frame data offset (no usado)
    header[6] = 0x00000000; // context offset (no usado)
    
    // Cabecera de datos
    u32* dataHeader = (u32*)(opusData + 0x30);
    dataHeader[0] = 0x80000004; // 'data info' chunk ID
    dataHeader[1] = sampleCount * 2; // data size en bytes
    
    // Solo copiamos los datos originales como placeholder
    // (esto no es Opus real, pero sirve para probar la estructura)
    memcpy(opusData + 0x38, samples, sampleCount * 2);
    
    *outSize = requiredSize;
    return opusData;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Uso: %s <archivo_entrada.wav> <archivo_salida.opus> <loop_start> <loop_end>\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    const char *output_file = argv[2];
    int loop_start = atoi(argv[3]);
    int loop_end = atoi(argv[4]);

    printf("Creando archivo Opus formato Capcom usando:\n");
    printf("Entrada: %s\n", input_file);
    printf("Salida: %s\n", output_file);
    printf("Loop start: %d\n", loop_start);
    printf("Loop end: %d\n", loop_end);

    // Leer archivo WAV
    MemoryFile inputFileData = MemoryFileCreate(input_file);
    if (!inputFileData.data_void) {
        printf("Error: No se pudo leer el archivo de entrada\n");
        return 1;
    }

    // Extraer información del WAV
    WavData wavData = ExtractWavSamples((u8*)inputFileData.data_void, inputFileData.size);
    if (!wavData.samples) {
        printf("Error: No se pudo procesar el archivo WAV\n");
        free(inputFileData.data_void);
        return 1;
    }
    
    printf("Archivo WAV: Muestras: %u, Canales: %u, Sample rate: %u\n", 
           wavData.sampleCount, wavData.channelCount, wavData.sampleRate);

    // Generar datos Opus (simplificado)
    size_t opusSize;
    u8* opusData = SimpleOpusEncoding(wavData.samples, wavData.sampleCount, 
                                      wavData.channelCount, &opusSize);
    
    if (!opusData) {
        printf("Error: No se pudo codificar a Opus\n");
        free(inputFileData.data_void);
        return 1;
    }
    
    // Crear encabezado formato Capcom
    size_t totalSize = CAPCOM_HEADER_SIZE + opusSize;
    u8* capcomOpus = malloc(totalSize);
    if (!capcomOpus) {
        printf("Error: No se pudo asignar memoria\n");
        free(opusData);
        free(inputFileData.data_void);
        return 1;
    }
    
    // Llenar encabezado Capcom
    OpusCapcomHeader* header = (OpusCapcomHeader*)capcomOpus;
    memset(capcomOpus, 0, CAPCOM_HEADER_SIZE); // Inicializar la memoria del encabezado
    
    header->numSamples = wavData.sampleCount / wavData.channelCount;  // Total de muestras por canal
    header->channelCount = wavData.channelCount;
    
    // Establecer la información de loop
    if (loop_start >= 0 && loop_end > loop_start) {
        // Combine loop points into loopInfo (u64) as required by the OpusCapcomHeader structure
        header->loopInfo = ((u64)loop_start << 32) | (u64)loop_end;
    } else {
        // No loop - set to all FFs
        header->loopInfo = 0xFFFFFFFFFFFFFFFF;
    }
    
    header->unknown1 = 0xF8000000;  // Valor específico del original
    header->frameSize = 0x0960;     // Frame size con datos extra (típico para Opus Capcom)
    header->extraChunks = 0;        // Sin chunks extras
    header->padding1 = 0;           // Padding a cero
    header->dataOffset = CAPCOM_HEADER_SIZE;
    
    // Configuración estándar de Capcom
    u32* config = (u32*)header->configData;
    config[0] = 0x0077C102;
    config[1] = 0x04000000;
    config[2] = 0xE107070C;
    config[3] = 0x00000000;
    
    // Copiar los datos Opus después del encabezado Capcom
    memcpy(capcomOpus + CAPCOM_HEADER_SIZE, opusData, opusSize);
    
    // Escribir el archivo resultante
    FILE* outFile = fopen(output_file, "wb");
    if (!outFile) {
        printf("Error: No se pudo crear el archivo de salida\n");
        free(capcomOpus);
        free(opusData);
        free(inputFileData.data_void);
        return 1;
    }
    
    fwrite(capcomOpus, 1, totalSize, outFile);
    fclose(outFile);
    
    printf("Archivo Opus en formato Capcom creado exitosamente: %s\n", output_file);
    printf("Tamaño total: %zu bytes\n", totalSize);
    
    // Liberar memoria
    free(capcomOpus);
    free(opusData);
    free(inputFileData.data_void);
    
    return 0;
}