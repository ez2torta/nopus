#include "meta.h"
#include "../coding/coding.h"
#include "../layout/layout.h"
#include "opus_interleave_streamfile.h"

/* Nintendo OPUS - from Switch games, including header variations (not the same as Ogg Opus) */

static VGMSTREAM* init_vgmstream_opus(STREAMFILE* sf, meta_t meta_type, off_t offset, int32_t num_samples, int32_t loop_start, int32_t loop_end) {
    VGMSTREAM* vgmstream = NULL;
    off_t start_offset;
    int loop_flag = 0, channels, sample_rate;
    off_t data_offset, context_offset, multistream_offset = 0;
    size_t data_size, skip = 0;

    /* header chunk */
    if (read_u32le(offset + 0x00,sf) != 0x80000001) /* 'basic info' chunk */
        goto fail;
    /* 0x04: chunk size (should be 0x24) */

    /* 0x08: version (0) */
    channels = read_u8(offset + 0x09, sf);
    /* 0x0a: frame size if CBR, 0 if VBR */
    sample_rate = read_u32le(offset + 0x0c,sf);
    data_offset = read_u32le(offset + 0x10, sf);
    /* 0x14: 'frame data offset' (seek table? not seen) */
    context_offset = read_u32le(offset + 0x18, sf);
    skip = read_u16le(offset + 0x1c, sf); /* pre-skip sample count */
    /* 0x1e: officially padding (non-zero in Lego Movie 2 (Switch)) */
    /* (no offset to multistream chunk, maybe meant to go after seek/context chunks?) */

    /* 0x80000002: 'offset info' chunk (seek table?), not seen */

    /* 'context info' chunk, rare [Famicom Detective Club (Switch), SINce Memories (Switch)] */
    if (context_offset && read_u32le(offset + context_offset, sf) == 0x80000003) {
        /* maybe should give priority to external info? */
        context_offset += offset;
        /* 0x08: null*/
        loop_flag   = read_u8   (context_offset + 0x09, sf);
        num_samples = read_s32le(context_offset + 0x0c, sf); /* slightly smaller than manual count */
        loop_start  = read_s32le(context_offset + 0x10, sf);
        loop_end    = read_s32le(context_offset + 0x14, sf);
        /* rest (~0x38) reserved/alignment? */
        /* values seem to take encoder delay into account */
    }
    else {
        loop_flag = (loop_end > 0); /* -1 when not set */
    }


    /* 'multistream info' chunk, rare [Clannad (Switch)] */
    if (read_u32le(offset + 0x20, sf) == 0x80000005) {
        multistream_offset = offset + 0x20;
    }

    /* Opus can only do 48000 but some games store original rate [Grandia HD Collection, Lego Marvel] */
    if (sample_rate != 48000) {
        VGM_LOG("OPUS: ignored non-standard sample rate of %i\n", sample_rate);
        sample_rate = 48000;
    }


    /* 'data info' chunk */
    data_offset += offset;
    if (read_u32le(data_offset, sf) != 0x80000004)
        goto fail;

    data_size = read_u32le(data_offset + 0x04, sf);

    start_offset = data_offset + 0x08;


    /* build the VGMSTREAM */
    vgmstream = allocate_vgmstream(channels,loop_flag);
    if (!vgmstream) goto fail;

    vgmstream->meta_type = meta_type;
    vgmstream->sample_rate = sample_rate;
    vgmstream->num_samples = num_samples;
    vgmstream->loop_start_sample = loop_start;
    vgmstream->loop_end_sample = loop_end;
    vgmstream->stream_size = data_size; /* to avoid inflated sizes from fake OggS IO */

#ifdef VGM_USE_FFMPEG
    {
        opus_config cfg = {0};

        cfg.channels = vgmstream->channels;
        cfg.skip = skip;
        cfg.sample_rate = vgmstream->sample_rate;

        if (multistream_offset && vgmstream->channels <= 8) {
            int i;
            cfg.stream_count = read_u8(multistream_offset + 0x08,sf);
            cfg.coupled_count = read_u8(multistream_offset + 0x09,sf); /* stereo streams */
            for (i = 0; i < vgmstream->channels; i++) {
                cfg.channel_mapping[i] = read_u8(multistream_offset + 0x0a + i,sf);
            }
        }

        vgmstream->codec_data = init_ffmpeg_switch_opus_config(sf, start_offset,data_size, &cfg);
        if (!vgmstream->codec_data) goto fail;
        vgmstream->coding_type = coding_FFmpeg;
        vgmstream->layout_type = layout_none;
        vgmstream->channel_layout = ffmpeg_get_channel_layout(vgmstream->codec_data);

        if (vgmstream->num_samples <= 0) {
            vgmstream->num_samples = switch_opus_get_samples(start_offset, data_size, sf) - skip;
            if (num_samples < 0 && vgmstream->loop_end_sample > vgmstream->num_samples) /* special flag for weird cases */
                vgmstream->loop_end_sample = vgmstream->num_samples;
        }
    }
#else
    goto fail;
#endif

    if (!vgmstream_open_stream(vgmstream, sf, start_offset))
        goto fail;
    return vgmstream;

fail:
    close_vgmstream(vgmstream);
    return NULL;
}


/* standard Switch Opus, Nintendo header + raw data (generated by opus_test.c?) [Lego City Undercover (Switch)] */
VGMSTREAM* init_vgmstream_opus_std(STREAMFILE* sf) {
    STREAMFILE* psi_sf = NULL;
    off_t offset;
    int num_samples, loop_start, loop_end;

    /* checks */
    if (read_u32le(0x00,sf) != 0x80000001) /* 'basic info' chunk */
        goto fail;

    /* .opus: standard / .lopus: for plugins
     * .bgm: Cotton Reboot (Switch)
     * .opu: Ys Memoire: The Oath in Felghana (Switch)
     * .ogg: Trouble Witches Origin (Switch)
     * .opusnx: Sweet Café Collection (Switch) */
    if (!check_extensions(sf,"opus,lopus,bgm,opu,ogg,logg,opusnx"))
        goto fail;

    offset = 0x00;

    /* BlazBlue: Cross Tag Battle (Switch) PSI Metadata for corresponding Opus */
    /* Maybe future Arc System Works games will use this too? */
    psi_sf = open_streamfile_by_ext(sf, "psi");
    if (psi_sf) {
        num_samples = read_s32le(0x8C, psi_sf);
        loop_start = read_s32le(0x84, psi_sf);
        loop_end = read_s32le(0x88, psi_sf);
        close_streamfile(psi_sf);
    }
    else {
        num_samples = 0;
        loop_start = 0;
        loop_end = 0;
    }

    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples,loop_start,loop_end);
fail:
    return NULL;
}

/* Nippon1 variation [Disgaea 5 (Switch)] */
VGMSTREAM* init_vgmstream_opus_n1(STREAMFILE* sf) {
    off_t offset;
    int num_samples, loop_start, loop_end;

    /* checks */
    if (!((read_u32be(0x04,sf) == 0x00000000 && read_u32be(0x0c,sf) == 0x00000000) ||
          (read_u32be(0x04,sf) == 0xFFFFFFFF && read_u32be(0x0c,sf) == 0xFFFFFFFF)))
        goto fail;
    if (!check_extensions(sf,"opus,lopus"))
        goto fail;

    offset = 0x10;
    num_samples = 0;
    loop_start = read_s32le(0x00,sf);
    loop_end = read_s32le(0x08,sf);

    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples,loop_start,loop_end);
fail:
    return NULL;
}

/* Capcom variation [Ultra Street Fighter II (Switch), Resident Evil: Revelations (Switch)] */
VGMSTREAM* init_vgmstream_opus_capcom(STREAMFILE* sf) {
    VGMSTREAM *vgmstream = NULL;
    off_t offset;
    int num_samples, loop_start, loop_end;
    int channels;

    /* checks */
    if (!check_extensions(sf,"opus,lopus"))
        goto fail;

    channels = read_32bitLE(0x04,sf);
    if (channels != 1 && channels != 2 && channels != 6)
        goto fail; /* unknown stream layout */

    num_samples = read_32bitLE(0x00,sf);
    /* 0x04: channels, >2 uses interleaved streams (2ch+2ch+2ch) */
    loop_start = read_32bitLE(0x08,sf);
    loop_end = read_32bitLE(0x0c,sf);
    /* 0x10: frame size (with extra data) */
    /* 0x14: extra chunk count */
    /* 0x18: null */
    offset = read_32bitLE(0x1c,sf);
    /* 0x20-8: config? (0x0077C102 04000000 E107070C) */
    /* 0x2c: some size? */
    /* 0x30+: extra chunks (0x00: 0x7f, 0x04: num_sample), alt loop starts/regions? */

    if (channels == 6) {
        /* 2ch multistream hacky-hacks in RE:RE, don't try this at home. We'll end up with:
         * main vgmstream > N vgmstream layers > substream IO deinterleaver > opus meta > Opus IO transmogrifier (phew) */
        layered_layout_data* data = NULL;
        int layers = channels / 2;
        int i;
        int loop_flag = (loop_end > 0);


        /* build the VGMSTREAM */
        vgmstream = allocate_vgmstream(channels,loop_flag);
        if (!vgmstream) goto fail;

        vgmstream->layout_type = layout_layered;

        /* init layout */
        data = init_layout_layered(layers);
        if (!data) goto fail;
        vgmstream->layout_data = data;

        /* open each layer subfile */
        for (i = 0; i < layers; i++) {
            STREAMFILE* temp_sf = setup_opus_interleave_streamfile(sf, offset, i, layers);
            if (!temp_sf) goto fail;

            data->layers[i] = init_vgmstream_opus(temp_sf, meta_OPUS, 0x00, num_samples,loop_start,loop_end);
            close_streamfile(temp_sf);
            if (!data->layers[i]) goto fail;
        }

        /* setup layered VGMSTREAMs */
        if (!setup_layout_layered(data))
            goto fail;

        vgmstream->sample_rate = data->layers[0]->sample_rate;
        vgmstream->num_samples = data->layers[0]->num_samples;
        vgmstream->loop_start_sample = data->layers[0]->loop_start_sample;
        vgmstream->loop_end_sample = data->layers[0]->loop_end_sample;
        vgmstream->meta_type = meta_OPUS;
        vgmstream->coding_type = data->layers[0]->coding_type;

        return vgmstream;
    }
    else {
        return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples,loop_start,loop_end);
    }


fail:
    close_vgmstream(vgmstream);
    return NULL;
}

/* Procyon Studio variation [Xenoblade Chronicles 2 (Switch)] */
VGMSTREAM* init_vgmstream_opus_nop(STREAMFILE* sf) {
    off_t offset;
    int num_samples, loop_start = 0, loop_end = 0, loop_flag;

    /* checks */
    if (!is_id32be(0x00, sf, "sadf") ||
        !is_id32be(0x08, sf, "opus"))
        goto fail;
    if (!check_extensions(sf,"nop"))
        goto fail;

    offset = read_32bitLE(0x1c, sf);
    num_samples = read_32bitLE(0x28, sf);
    loop_flag = read_8bit(0x19, sf);
    if (loop_flag) {
        loop_start = read_32bitLE(0x2c, sf);
        loop_end = read_32bitLE(0x30, sf);
    }

    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples,loop_start,loop_end);
fail:
    return NULL;
}

/* Shin'en variation [Fast RMX (Switch)] */
VGMSTREAM* init_vgmstream_opus_shinen(STREAMFILE* sf) {
    off_t offset = 0;
    int num_samples, loop_start, loop_end;

    /* checks */
    if (read_u32be(0x08,sf) != 0x01000080)
        goto fail;
    if (!check_extensions(sf,"opus,lopus"))
        goto fail;

    offset = 0x08;
    loop_start = read_s32le(0x00,sf);
    loop_end = read_s32le(0x04,sf); /* 0 if no loop */

    /* tepaneca.opus has loop_end slightly bigger than samples, but doesn't seem an encoder delay thing since
     * several tracks do full loops to 0 and sound ok. Mark with a special flag to allow this case. */
    num_samples = -1;

    if (loop_start > loop_end)
        goto fail; /* just in case */

    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples, loop_start, loop_end);
fail:
    return NULL;
}

/* Bandai Namco Opus (found in NUS3Banks) [Taiko no Tatsujin: Nintendo Switch Version!] */
VGMSTREAM* init_vgmstream_opus_nus3(STREAMFILE* sf) {
    off_t offset = 0;
    int num_samples = 0, loop_start = 0, loop_end = 0, loop_flag;

    /* checks */
    if (!is_id32be(0x00, sf, "OPUS"))
        goto fail;

    /* .opus: header ID (they only exist inside .nus3bank) */
    if (!check_extensions(sf, "opus,lopus"))
        goto fail;

    /* Here's an interesting quirk, OPUS header contains big endian values
       while the Nintendo Opus header and data that follows remain little endian as usual */
    offset = read_32bitBE(0x20, sf);
    num_samples = read_32bitBE(0x08, sf);

    /* Check if there's a loop end value to determine loop_flag*/
    loop_flag = read_32bitBE(0x18, sf);
    if (loop_flag) {
        loop_start = read_32bitBE(0x14, sf);
        loop_end = read_32bitBE(0x18, sf);
    }

    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples, loop_start, loop_end);
fail:
    return NULL;
}

/* Nippon Ichi SPS wrapper (non-segmented) [Ys VIII: Lacrimosa of Dana (Switch)] */
VGMSTREAM* init_vgmstream_opus_sps_n1(STREAMFILE* sf) {
    off_t offset;
    int num_samples, loop_start = 0, loop_end = 0, loop_flag;

    /* checks */
    if (read_u32be(0x00, sf) != 0x09000000) /* file type (see other N1 SPS) */
        goto fail;

    /* .sps: Labyrinth of Refrain: Coven of Dusk (Switch)
     * .nlsd: Disgaea Refine (Switch), Ys VIII (Switch)
     * .at9: void tRrLM(); //Void Terrarium (Switch)
     * .opus: Asatsugutori (Switch) */
    if (!check_extensions(sf, "sps,nlsd,at9,opus,lopus"))
        goto fail;

    num_samples = read_32bitLE(0x0C, sf);

    if (read_32bitBE(0x1c, sf) == 0x01000080) {
        offset = 0x1C;

        /* older games loop section (remnant of segmented opus_sps_n1): */
        loop_start = read_32bitLE(0x10, sf); /* intro samples */
        loop_end = loop_start + read_32bitLE(0x14, sf); /* loop samples */
        /* 0x18: end samples (all must add up to num_samples) */
        loop_flag = read_32bitLE(0x18, sf); /* with loop disabled only loop_end has a value */
    }
    else {
        offset = 0x18;

        /* newer games loop section: */
        loop_start = read_32bitLE(0x10, sf);
        loop_end = read_32bitLE(0x14, sf);
        loop_flag = loop_start != loop_end; /* with loop disabled start and end are the same as num samples */
    }

    if (!loop_flag) {
        loop_start = 0;
        loop_end = 0;
    }

    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples, loop_start, loop_end);
fail:
    return NULL;
}

/* AQUASTYLE wrapper [Touhou Genso Wanderer -Reloaded- (Switch)] */
VGMSTREAM* init_vgmstream_opus_opusx(STREAMFILE* sf) {
    off_t offset;
    int num_samples, loop_start = 0, loop_end = 0;
    float modifier;

    /* checks */
    if (!is_id32be(0x00, sf, "OPUS"))
        goto fail;
    if (!check_extensions(sf, "opusx"))
        goto fail;

    offset = 0x10;
    /* values are for the original 44100 files, but Opus resamples to 48000 */
    modifier = 48000.0f / 44100.0f;
    num_samples = 0;//read_32bitLE(0x04, sf) * modifier; /* better use calc'd num_samples */
    loop_start = read_32bitLE(0x08, sf) * modifier;
    loop_end = read_32bitLE(0x0c, sf) * modifier;

    /* resampling calcs are slighly off and may to over num_samples, but by removing delay seems ok */
    if (loop_start >= 120) {
        loop_start -= 128;
        loop_end -= 128;
    }
    else {
        loop_end = 0;
    }

    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples, loop_start, loop_end);
fail:
    return NULL;
}

/* Prototype variation [Clannad (Switch)] */
VGMSTREAM* init_vgmstream_opus_prototype(STREAMFILE* sf) {
    off_t offset = 0;
    int num_samples = 0, loop_start = 0, loop_end = 0, loop_flag;

    /* checks */
    if (!is_id32be(0x00, sf, "OPUS"))
        goto fail;
    if (!check_extensions(sf, "opus,lopus"))
        goto fail;
    if (read_32bitBE(0x18, sf) != 0x01000080)
        goto fail;

    offset = 0x18;
    num_samples = read_32bitLE(0x08, sf);

    /* Check if there's a loop end value to determine loop_flag*/
    loop_flag = read_32bitLE(0x10, sf);
    if (loop_flag) {
        loop_start = read_32bitLE(0x0C, sf);
        loop_end = read_32bitLE(0x10, sf);
    }

    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples, loop_start, loop_end);
fail:
    return NULL;
}

/* Edelweiss variation [Astebreed (Switch)] */
VGMSTREAM* init_vgmstream_opus_opusnx(STREAMFILE* sf) {
    off_t offset = 0;
    int num_samples = 0, loop_start = 0, loop_end = 0;

    /* checks */
    if (!is_id64be(0x00, sf,"OPUSNX\0\0"))
        goto fail;
    if (!check_extensions(sf, "opus,lopus"))
        goto fail;

    offset = 0x10;
    num_samples = 0; //read_32bitLE(0x08, sf); /* samples with encoder delay */
    if (read_32bitLE(0x0c, sf) != 0)
        goto fail;

    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples, loop_start, loop_end);
fail:
    return NULL;
}

/* Edelweiss variation [Sakuna: Of Rice and Ruin (Switch)] */
VGMSTREAM* init_vgmstream_opus_nsopus(STREAMFILE* sf) {
    off_t offset = 0;
    int num_samples = 0, loop_start = 0, loop_end = 0;

    /* checks */
    if (!is_id32be(0x00, sf,"EWNO"))
        goto fail;
    if (!check_extensions(sf, "nsopus"))
        goto fail;

    offset = 0x08;
    num_samples = 0; //read_32bitLE(0x08, sf); /* samples without encoder delay? (lower than count) */

    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples, loop_start, loop_end);
fail:
    return NULL;
}

/* Square Enix variation [Dragon Quest I-III (Switch)] */
VGMSTREAM* init_vgmstream_opus_sqex(STREAMFILE* sf) {
    off_t offset = 0;
    int num_samples = 0, loop_start = 0, loop_end = 0, loop_flag;

    /* checks */
    if (read_u32be(0x00, sf) != 0x01000000)
        goto fail;

    /* .wav: original */
    if (!check_extensions(sf, "wav,lwav"))
        goto fail;
    /* 0x04: channels */
    /* 0x08: data_size */
    offset = read_32bitLE(0x0C, sf);
    num_samples = read_32bitLE(0x1C, sf);

    loop_flag = read_32bitLE(0x18, sf);
    if (loop_flag) {
        loop_start = read_32bitLE(0x14, sf);
        loop_end = read_32bitLE(0x18, sf);
    }

    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples, loop_start, loop_end);
fail:
    return NULL;
}


/* Idea Factory(?) variation [Birushana: Ichijuu no Kaze (Switch)] */
VGMSTREAM* init_vgmstream_opus_rsnd(STREAMFILE* sf) {
    off_t offset = 0;
    int num_samples = 0, loop_start = 0, loop_end = 0, loop_flag;

    /* checks */
    if (!is_id32be(0x00, sf,"RSND"))
        goto fail;
    if (!check_extensions(sf, "rsnd"))
        goto fail;
    /* 0x04: 00? (16b)*/
    /* 0x06: 00? (8b)*/
    loop_flag = read_u8(0x07, sf);
    if (loop_flag) { /* not really needed as both will be 0 */
        loop_start = read_s32le(0x08, sf);
        loop_end = read_s32le(0x0c, sf);
    }
    offset = read_u32le(0x10, sf); /* always 0x40 */
    /* 0x14: offset again? */
    /* 0x18+: null? (unknown numbers in bgm050) */
    num_samples = 0; /* not loop_end as it isn't set when looping is disabled */


    return init_vgmstream_opus(sf, meta_OPUS, offset, num_samples, loop_start, loop_end);
fail:
    return NULL;
}