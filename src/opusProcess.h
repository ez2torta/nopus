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
#define CHUNK_CAPCOM_DATA_ID (0x80000004)

#define OPUS_VERSION (0)

// The identifier for regular old Ogg Opus files.
#define OGG_OPUS_ID IDENTIFIER_TO_U32('O','g','g','S')

typedef struct __attribute__((packed)) {
    u32 chunkId; // Compare to CHUNK_HEADER_ID.
    u32 chunkSize; // Exclusive of chunkId and chunkSize. Usually 0x18.

    u8 version; // Compare to OPUS_VERSION.

    u8 channelCount; // Values that are not 1 or 2 are illegal.

    u16 frameSize; // Frame size if constant bitrate, 0 if variable bitrate.

    u32 sampleRate; // Allowed values: 48000, 24000, 16000, 12000, and 8000.

    u32 dataOffset; // Offset to the data chunk (OpusDataChunk).
    u32 _unk14; // Offset to a section with the ID 0x80000002, can't find it anywhere ..
    u32 contextOffset; // Offset to a section with the ID 0x80000003, supposedly holds looping info.

    u16 preSkipSamples; // The amount of samples that should be skipped at the beginning of playback.

    u16 _pad16;
} OpusFileHeader;

typedef struct __attribute__((packed)) {
    u32 chunkId; // Compare to CHUNK_DATA_ID.
    u32 chunkSize; // Exclusive of chunkId and chunkSize.

    u8 data[0]; // Dynamically sized Opus packets (OpusPacketHeader).
} OpusDataChunk;

typedef struct __attribute__((packed)) {
    // Note: both of these fields are in big-endian, might just be the DSPs endianness..
    u32 packetSize; // Length of the Opus packet.
    u32 finalRange; // Encoder final range.
    u8 packet[0];
} OpusPacketHeader;

// Capcom OPUS file header (first 0x30 bytes of the file).
// Immediately followed by a standard Nintendo OpusFileHeader at the offset
// stored in dataOffset.
typedef struct __attribute__((packed)) {
    u32 numSamples;    // Total decoded samples per channel
    u32 channelCount;  // Number of channels (1 or 2)
    u64 loopInfo;      // 8 bytes: loopStart (u32) + loopEnd (u32); all 0xFF if no loop
    u32 unknown1;      // CBR frame unit size including 8-byte OpusPacketHeader (e.g. 0xF8)
    u32 frameSize;     // CBR frame unit size at offset 0x10
    u32 extraChunks;   // Number of extra chunks after this header (usually 0)
    u32 padding1;      // Reserved / null
    u32 dataOffset;    // Offset to the embedded Nintendo OpusFileHeader (typically 0x30)
    u8 configData[16]; // Game-specific configuration bytes (ignored by vgmstream)
} OpusCapcomHeader;

void OpusPreprocess(u8* opusData) {
    OpusFileHeader* fileHeader = (OpusFileHeader*)opusData;

    if (fileHeader->chunkId == OGG_OPUS_ID)
        panic("A Ogg Opus file was passed; please pass in a Nintendo Opus file.");
    if (fileHeader->chunkId != CHUNK_HEADER_ID)
        panic("OPUS file header ID is nonmatching");

    if (fileHeader->contextOffset != 0)
        warn("OPUS context is present but will be ignored");
    
    if (
        fileHeader->sampleRate != 48000 &&
        fileHeader->sampleRate != 24000 &&
        fileHeader->sampleRate != 16000 &&
        fileHeader->sampleRate != 12000 &&
        fileHeader->sampleRate != 8000
    )
        panic("Invalid OPUS sample rate (%uhz)", fileHeader->sampleRate);

    if (
        fileHeader->channelCount != 1 &&
        fileHeader->channelCount != 2
    )
        panic("Invalid OPUS channel count (%u)", fileHeader->channelCount);

    OpusDataChunk* dataChunk = (OpusDataChunk*)(opusData + fileHeader->dataOffset);
    if (dataChunk->chunkId != CHUNK_DATA_ID)
        panic("OPUS data chunk ID is nonmatching");
}

u32 OpusGetChannelCount(u8* opusData) {
    return ((OpusFileHeader*)opusData)->channelCount;
}
u32 OpusGetSampleRate(u8* opusData) {
    return ((OpusFileHeader*)opusData)->sampleRate;
}

ListData OpusDecode(u8* opusData) {
    OpusFileHeader* fileHeader = (OpusFileHeader*)opusData;
    OpusDataChunk* dataChunk = (OpusDataChunk*)(opusData + fileHeader->dataOffset);

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

    int samplesLeftToSkip = fileHeader->preSkipSamples;

    while (offset < dataChunk->chunkSize) {
        OpusPacketHeader* packetHeader = (OpusPacketHeader*)(dataChunk->data + offset);
        u32 packetSize = __builtin_bswap32(packetHeader->packetSize);

        offset += sizeof(OpusPacketHeader) + packetSize;

        int samplesDecoded = opus_decode(decoder, packetHeader->packet, packetSize, tempSamples, coFrameSize, 0);
        if (samplesDecoded < 0)
            panic("OpusProcess: opus_decode fail: %s", opus_strerror(samplesDecoded));

        if (samplesLeftToSkip > 0) {
            // Full skip.
            if (samplesDecoded <= samplesLeftToSkip) {
                samplesLeftToSkip -= samplesDecoded;
                continue;
            }
            // Partial skip.
            else {
                int remainingSamples = samplesDecoded - samplesLeftToSkip;
                ListAddRange(&samples, tempSamples + samplesLeftToSkip, remainingSamples * fileHeader->channelCount);
                samplesLeftToSkip = 0;
            }
        }
        // No skip.
        else
            ListAddRange(&samples, tempSamples, samplesDecoded * fileHeader->channelCount);
    }

    opus_decoder_destroy(decoder);
    
    free(tempSamples);

    return samples;
}

#define OPUS_PACKETSIZE_MAX (1275)

// Default bitrate for the standard Nintendo Opus format (make_opus command).
// Capcom Opus uses 96000 bps (240 bytes per 20ms frame) - see OpusBuildCapcom.
#define OPUS_DEFAULT_BITRATE (99000)

typedef struct OpusBuildPacket {
    struct OpusBuildPacket* next;

    u32 packetLen;
    u32 finalRange;
    u8 packetData[0];
} OpusBuildPacket;

MemoryFile OpusBuild(s16* samples, u32 sampleCount, u32 sampleRate, u32 channelCount) {
    if (
        sampleRate != 48000 && sampleRate != 24000 &&
        sampleRate != 16000 && sampleRate != 12000 &&
        sampleRate != 8000
    ) {
        panic(
            "OpusBuild: Invalid sample rate (%uhz)\n"
            "Allowed sample rates are: 48000, 24000, 16000, 12000, and 8000",
            sampleRate
        );
    }

    if (
        channelCount != 1 && channelCount != 2
    ) {
        panic(
            "OpusBuild: Invalid channel count (%u)\n"
            "Only one or two channels are allowed.",
            channelCount
        );
    }

    MemoryFile mfResult;

    int opusError;

    OpusEncoder* encoder = opus_encoder_create(sampleRate, channelCount, OPUS_APPLICATION_AUDIO, &opusError);
    if (opusError < 0)
        panic("OpusBuild: opus_encoder_create failed: %s", opus_strerror(opusError));

    opusError = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(OPUS_DEFAULT_BITRATE));
    if (opusError < 0)
        panic("OpusBuild: failed to set Opus bitrate to %u", OPUS_DEFAULT_BITRATE);

    opusError = opus_encoder_ctl(encoder, OPUS_SET_VBR(1));
    if (opusError < 0)
        panic("OpusBuild: failed to enable Opus VBR");

    opusError = opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(0));
    if (opusError < 0)
        panic("OpusBuild: failed to disable Opus VBR constraint");

    u32 frameDurationMs = 20;
    u32 frameSize = (sampleRate / 1000) * frameDurationMs;
    u32 samplesPerFrame = frameSize * channelCount;

    int preSkipSamples;
    opusError = opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&preSkipSamples));
    if (opusError < 0)
        panic("OpusBuild: failed to get pre-skip sample count");

    // Root packet has no data. packetLen holds the packet count.
    OpusBuildPacket* rootPacket = (OpusBuildPacket*)malloc(sizeof(OpusBuildPacket));
    if (rootPacket == NULL)
        panic("OpusBuild: failed to allocate root packet");

    rootPacket->next = NULL;
    rootPacket->packetLen = 0;
    rootPacket->finalRange = 0;

    OpusBuildPacket* currentPacket = rootPacket;

    u8 buffer[OPUS_PACKETSIZE_MAX];

    for (u32 i = 0; i + samplesPerFrame <= sampleCount; i += samplesPerFrame) {
        int nbBytes = opus_encode(
            encoder, samples + i, frameSize, buffer, sizeof(buffer)
        );
        if (nbBytes < 0)
            panic("OpusBuild: opus_encode failed: %s", opus_strerror(nbBytes));

        OpusBuildPacket* packet = (OpusBuildPacket*)malloc(sizeof(OpusBuildPacket) + nbBytes);
        if (packet == NULL)
            panic("OpusBuild: failed to allocate packet");

        packet->next = NULL;

        packet->packetLen = nbBytes;
        memcpy(packet->packetData, buffer, nbBytes);

        opusError = opus_encoder_ctl(encoder, OPUS_GET_FINAL_RANGE(&packet->finalRange));
        if (opusError < 0)
            panic("OpusBuild: failed to get encoder final range");

        currentPacket->next = packet;
        currentPacket = packet;

        rootPacket->packetLen++;
    }

    opus_encoder_destroy(encoder);

    // Now to actually write the file.

    mfResult.size = sizeof(OpusFileHeader) + sizeof(OpusDataChunk);

    currentPacket = rootPacket->next;
    while (currentPacket != NULL) {
        mfResult.size += sizeof(OpusPacketHeader) + currentPacket->packetLen;
        currentPacket = currentPacket->next;
    }

    mfResult.data_void = malloc(mfResult.size);
    if (mfResult.data_void == NULL)
        panic("OpusBuild: failed to allocate file buffer");

    OpusFileHeader* fileHeader = (OpusFileHeader*)mfResult.data_void;

    fileHeader->chunkId = CHUNK_HEADER_ID;
    fileHeader->chunkSize = sizeof(OpusFileHeader) - 8;

    fileHeader->version = OPUS_VERSION;

    fileHeader->channelCount = channelCount;

    fileHeader->frameSize = 0; // VBR.

    fileHeader->sampleRate = sampleRate;

    fileHeader->dataOffset = sizeof(OpusFileHeader);
    fileHeader->_unk14 = 0x00000000;
    fileHeader->contextOffset = 0x00000000;

    fileHeader->preSkipSamples = preSkipSamples;

    fileHeader->_pad16 = 0x0000;

    OpusDataChunk* dataChunk = (OpusDataChunk*)(fileHeader + 1);

    dataChunk->chunkId = CHUNK_DATA_ID;
    dataChunk->chunkSize = mfResult.size - sizeof(OpusFileHeader) - sizeof(OpusDataChunk);

    OpusPacketHeader* currentPacketHeader = (OpusPacketHeader*)dataChunk->data;

    currentPacket = rootPacket->next;
    while (currentPacket != NULL) {
        currentPacketHeader->packetSize = __builtin_bswap32(currentPacket->packetLen);
        currentPacketHeader->finalRange = __builtin_bswap32(currentPacket->finalRange);

        memcpy(
            currentPacketHeader->packet,
            currentPacket->packetData, currentPacket->packetLen
        );

        currentPacketHeader = (OpusPacketHeader*)(
            (u8*)(currentPacketHeader + 1) + currentPacket->packetLen
        );

        currentPacket = currentPacket->next;
    }

    // Free packets.
    currentPacket = rootPacket;
    while (currentPacket) {
        OpusBuildPacket* nextPacket = currentPacket->next;
        free(currentPacket);
        currentPacket = nextPacket;
    }

    return mfResult;
}

// Build a Capcom-format OPUS file from PCM samples.
// Replicates the format used in Capcom Switch games (e.g. Resident Evil: Revelations).
// Uses CELT-only CBR encoding at 96kbps (240 bytes/packet, 20ms frames, pre-skip=120)
// to match the original Capcom encoder output.
//
// File layout:
//   0x00-0x2F : Capcom header (48 bytes)
//   0x30-0x4F : Nintendo Opus header (32 bytes)
//   0x50-0x57 : Data chunk header (8 bytes)
//   0x58+     : Opus packets (each: 4B BE size + 4B BE finalRange + data)
//
// criticalBytes and orig_packet_sizes/orig_packet_count are kept for API compatibility
// but are no longer used; all values are derived from the audio parameters.
MemoryFile OpusBuildCapcom(s16* samples, u32 sampleCount, u32 sampleRate,
    u32 channelCount, u32 loopStart, u32 loopEnd,
    u8* configData, u8* criticalBytes, u32* orig_packet_sizes, size_t orig_packet_count)
{
    // Capcom uses CELT-only CBR encoding:
    //   96000 bps / 8 bits / 50 frames-per-sec = 240 bytes per 20ms packet
    //   frame_size field in Nintendo header = 240 + 8 (OpusPacketHeader) = 248 = 0xF8
    const u32 bitRate = 96000;
    const u32 frameDurationMs = 20;
    // Samples per channel per frame (e.g. 960 at 48 kHz for 20 ms)
    const u32 frameSize = (sampleRate / 1000) * frameDurationMs;
    // Total interleaved samples consumed per encode call
    const u32 samplesPerFrame = frameSize * channelCount;

    if (
        sampleRate != 48000 && sampleRate != 24000 &&
        sampleRate != 16000 && sampleRate != 12000 &&
        sampleRate != 8000
    ) {
        panic(
            "OpusBuildCapcom: Invalid sample rate (%uhz)\n"
            "Allowed sample rates are: 48000, 24000, 16000, 12000, and 8000",
            sampleRate
        );
    }

    if (channelCount != 1 && channelCount != 2) {
        panic(
            "OpusBuildCapcom: Invalid channel count (%u)\n"
            "Only one or two channels are allowed.",
            channelCount
        );
    }

    u32 samplesPerChannel = sampleCount / channelCount;

    // Validate loop points
    if (loopEnd > samplesPerChannel) {
        warn("OpusBuildCapcom: loop_end exceeds sample count, clamping");
        loopEnd = samplesPerChannel;
    }
    if (loopStart > 0 && loopStart >= loopEnd) {
        warn("OpusBuildCapcom: loop_start >= loop_end, disabling loops");
        loopStart = 0;
        loopEnd = 0;
    }

    int opusError = 0;

    // OPUS_APPLICATION_RESTRICTED_LOWDELAY forces CELT-only mode.
    // This gives encoder pre-skip = 120 samples, matching original Capcom files.
    OpusEncoder* encoder = opus_encoder_create(
        sampleRate, channelCount, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &opusError);
    if (opusError < 0)
        panic("OpusBuildCapcom: opus_encoder_create failed: %s", opus_strerror(opusError));

    opusError = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitRate));
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set Opus bitrate");

    // CBR mode: each packet will be the same size (240 bytes at 96kbps/50fps)
    opusError = opus_encoder_ctl(encoder, OPUS_SET_VBR(0));
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set CBR mode");

    opusError = opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(1));
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set hard CBR constraint");

    opusError = opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(10));
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set Opus complexity");

    opusError = opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set Opus signal type");

    opusError = opus_encoder_ctl(encoder, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to set Opus bandwidth");

    // Read the actual pre-skip reported by the encoder
    int preSkipSamples = 0;
    opusError = opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&preSkipSamples));
    if (opusError < 0)
        panic("OpusBuildCapcom: failed to get pre-skip sample count");

    // Encode all complete frames and accumulate raw packet bytes
    ListData packetList;
    ListInit(&packetList, sizeof(u8), 65536);

    for (u32 i = 0; i + samplesPerFrame <= sampleCount; i += samplesPerFrame) {
        u8 buffer[4096];
        int nbBytes = opus_encode(encoder, samples + i, frameSize, buffer, sizeof(buffer));
        if (nbBytes < 0)
            panic("OpusBuildCapcom: opus_encode failed: %s", opus_strerror(nbBytes));

        // Packet header: size and finalRange stored big-endian (matching OpusPacketHeader)
        u32 packetSizeBE = __builtin_bswap32((u32)nbBytes);
        ListAddRange(&packetList, (u8*)&packetSizeBE, 4);

        u32 finalRange = 0;
        opusError = opus_encoder_ctl(encoder, OPUS_GET_FINAL_RANGE(&finalRange));
        if (opusError < 0)
            panic("OpusBuildCapcom: failed to get encoder final range");
        u32 finalRangeBE = __builtin_bswap32(finalRange);
        ListAddRange(&packetList, (u8*)&finalRangeBE, 4);

        ListAddRange(&packetList, buffer, nbBytes);
    }

    opus_encoder_destroy(encoder);

    // -----------------------------------------------------------------------
    // Build the output file
    //
    // Layout (all offsets absolute):
    //   [0x00-0x2F]  Capcom header         (0x30 bytes)
    //   [0x30-0x4F]  Nintendo Opus header  (sizeof(OpusFileHeader) = 0x20 bytes)
    //   [0x50-0x57]  Data chunk header     (sizeof(OpusDataChunk)  = 0x08 bytes)
    //   [0x58+]      Opus packet data
    // -----------------------------------------------------------------------

    const u32 capcomHdrSize   = 0x30;
    const u32 totalSize = capcomHdrSize + (u32)sizeof(OpusFileHeader)
                        + (u32)sizeof(OpusDataChunk) + packetList.elementCount;

    MemoryFile result;
    result.size = totalSize;
    result.data_void = malloc(totalSize);
    if (!result.data_void)
        panic("OpusBuildCapcom: failed to allocate output buffer");
    memset(result.data_void, 0, totalSize);

    u8* fileData = (u8*)result.data_void;

    // --- Capcom header (0x00-0x2F) ---
    // 0x00: total decoded samples per channel
    memcpy(fileData + 0x00, &samplesPerChannel, 4);
    // 0x04: channel count
    memcpy(fileData + 0x04, &channelCount, 4);
    // 0x08-0x0F: loop points (0xFFFFFFFF = no loop, matching vgmstream convention)
    if (loopStart == 0 && loopEnd == 0) {
        memset(fileData + 0x08, 0xFF, 8);
    } else {
        memcpy(fileData + 0x08, &loopStart, 4);
        memcpy(fileData + 0x0C, &loopEnd, 4);
    }
    // 0x10: CBR frame unit size (packet data + 8-byte OpusPacketHeader = 248 = 0xF8)
    u32 frameUnitSize = (u32)(bitRate / 8 / (1000 / frameDurationMs))
                      + (u32)sizeof(OpusPacketHeader);
    memcpy(fileData + 0x10, &frameUnitSize, 4);
    // 0x14: extra chunk count = 0
    // 0x18: null = 0  (already zeroed)
    // 0x1C: offset to Nintendo Opus header = 0x30
    u32 nintendoOff = capcomHdrSize;
    memcpy(fileData + 0x1C, &nintendoOff, 4);
    // 0x20-0x2F: game-specific config bytes (ignored by vgmstream)
    memcpy(fileData + 0x20, configData, 16);

    // --- Nintendo Opus header (0x30-0x4F) ---
    OpusFileHeader* nintendoHdr = (OpusFileHeader*)(fileData + capcomHdrSize);
    nintendoHdr->chunkId       = CHUNK_HEADER_ID;
    nintendoHdr->chunkSize     = (u32)sizeof(OpusFileHeader) - 8; // 0x18
    nintendoHdr->version       = OPUS_VERSION;
    nintendoHdr->channelCount  = (u8)channelCount;
    // CBR packet size including 8-byte OpusPacketHeader (matches 0x10 in Capcom header)
    // e.g. 240 data bytes + 8 header bytes = 248 = 0xF8 at 96kbps/50fps
    nintendoHdr->frameSize     = (u16)frameUnitSize;
    nintendoHdr->sampleRate    = sampleRate;
    // dataOffset is relative to the start of this Nintendo header
    nintendoHdr->dataOffset    = (u32)sizeof(OpusFileHeader); // = 0x20 -> data at 0x50
    nintendoHdr->_unk14        = 0;
    nintendoHdr->contextOffset = 0;
    nintendoHdr->preSkipSamples = (u16)preSkipSamples;
    nintendoHdr->_pad16        = 0;

    // --- Data chunk (0x50-0x57) ---
    OpusDataChunk* dataChunk = (OpusDataChunk*)(fileData + capcomHdrSize
                                                + sizeof(OpusFileHeader));
    dataChunk->chunkId   = CHUNK_DATA_ID;
    dataChunk->chunkSize = packetList.elementCount;

    // --- Packet data (0x58+) ---
    memcpy(dataChunk->data, packetList.data, packetList.elementCount);

    ListDestroy(&packetList);

    return result;
}

// Decode a Capcom-format OPUS file to interleaved s16 PCM samples.
// The Capcom header (0x00-0x2F) is parsed to find the embedded Nintendo
// Opus header; standard Opus decoding then proceeds from that offset.
ListData OpusDecodeCapcom(u8* capcomData) {
    // Validate basic structure
    if (!capcomData)
        panic("OpusDecodeCapcom: null input");

    // Read the offset of the Nintendo Opus header from the Capcom header
    u32 nintendoOff;
    memcpy(&nintendoOff, capcomData + 0x1C, 4);

    u8* nintendoData = capcomData + nintendoOff;
    OpusFileHeader* fileHeader = (OpusFileHeader*)nintendoData;

    if (fileHeader->chunkId != CHUNK_HEADER_ID)
        panic("OpusDecodeCapcom: invalid Nintendo OPUS chunk ID at offset 0x%X", nintendoOff);

    OpusDataChunk* dataChunk = (OpusDataChunk*)(nintendoData + fileHeader->dataOffset);
    if (dataChunk->chunkId != CHUNK_DATA_ID)
        panic("OpusDecodeCapcom: invalid data chunk ID");

    int error;
    OpusDecoder* decoder = opus_decoder_create(
        fileHeader->sampleRate, fileHeader->channelCount, &error);
    if (error != OPUS_OK)
        panic("OpusDecodeCapcom: opus_decoder_create failed: %s", opus_strerror(error));

    // Allocate a decode buffer large enough for the maximum Opus frame (120 ms)
    const u32 maxSamplesPerChannel = fileHeader->sampleRate / 1000 * 120;
    s16* tempSamples = (s16*)malloc(
        maxSamplesPerChannel * fileHeader->channelCount * sizeof(s16));
    if (!tempSamples)
        panic("OpusDecodeCapcom: failed to alloc temp buffer");

    ListData samples;
    ListInit(&samples, sizeof(s16),
             maxSamplesPerChannel * fileHeader->channelCount * 16);

    unsigned offset = 0;
    int samplesLeftToSkip = fileHeader->preSkipSamples;

    while (offset < dataChunk->chunkSize) {
        OpusPacketHeader* packetHeader =
            (OpusPacketHeader*)(dataChunk->data + offset);
        u32 packetSize = __builtin_bswap32(packetHeader->packetSize);

        offset += (u32)sizeof(OpusPacketHeader) + packetSize;

        int samplesDecoded = opus_decode(
            decoder, packetHeader->packet, packetSize,
            tempSamples, maxSamplesPerChannel, 0);
        if (samplesDecoded < 0)
            panic("OpusDecodeCapcom: opus_decode failed: %s",
                  opus_strerror(samplesDecoded));

        if (samplesLeftToSkip > 0) {
            if (samplesDecoded <= samplesLeftToSkip) {
                samplesLeftToSkip -= samplesDecoded;
                continue;
            } else {
                int remaining = samplesDecoded - samplesLeftToSkip;
                // Skip samplesLeftToSkip * channelCount interleaved values
                ListAddRange(&samples,
                    tempSamples + samplesLeftToSkip * fileHeader->channelCount,
                    remaining * fileHeader->channelCount);
                samplesLeftToSkip = 0;
            }
        } else {
            ListAddRange(&samples, tempSamples,
                         samplesDecoded * fileHeader->channelCount);
        }
    }

    opus_decoder_destroy(decoder);
    free(tempSamples);

    return samples;
}

// Return the channel count stored in a Capcom OPUS file.
u32 OpusCapcomGetChannelCount(u8* capcomData) {
    u32 channels;
    memcpy(&channels, capcomData + 0x04, 4);
    return channels;
}

// Return the sample rate stored in the embedded Nintendo Opus header.
u32 OpusCapcomGetSampleRate(u8* capcomData) {
    u32 nintendoOff;
    memcpy(&nintendoOff, capcomData + 0x1C, 4);
    return ((OpusFileHeader*)(capcomData + nintendoOff))->sampleRate;
}

#endif // OPUS_PROCESS_H
