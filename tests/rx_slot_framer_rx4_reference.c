#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_engine.h"
#include "rx_slot_framer.h"

static const uint8_t kExpectedPayload[FT8_PAYLOAD_BYTES] = {
    0x00, 0x00, 0x00, 0x20, 0x60, 0x16, 0x50, 0x0A, 0x19, 0x88
};

typedef struct {
    Ft8Engine *engine;
    Ft8ProtocolMessage messages[FT8_DECODER_CANDIDATE_CAPACITY];
    Ft8ProtocolSlot slot;
    size_t begin_count;
    size_t block_count;
    size_t finalize_count;
    Ft8EngineStatus finalize_status;
} EngineSink;

static int read_u16(FILE *f, uint16_t *out)
{
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2)
        return -1;
    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return 0;
}

static int read_u32(FILE *f, uint32_t *out)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4)
        return -1;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

static int open_pcm_data(const char *path, FILE **out_file, uint32_t *out_samples)
{
    FILE *f;
    char id[4];
    uint32_t ignored32;
    uint16_t audio_format = 0u;
    uint16_t channels = 0u;
    uint16_t bits = 0u;
    uint32_t sample_rate = 0u;
    int have_fmt = 0;

    f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "RIFF", 4) != 0 ||
        read_u32(f, &ignored32) != 0 ||
        fread(id, 1, 4, f) != 4 || memcmp(id, "WAVE", 4) != 0) {
        fclose(f);
        return -1;
    }

    for (;;) {
        uint32_t chunk_size;
        if (fread(id, 1, 4, f) != 4 || read_u32(f, &chunk_size) != 0)
            break;

        if (memcmp(id, "fmt ", 4) == 0) {
            uint32_t byte_rate;
            uint16_t block_align;
            if (chunk_size < 16u ||
                read_u16(f, &audio_format) != 0 ||
                read_u16(f, &channels) != 0 ||
                read_u32(f, &sample_rate) != 0 ||
                read_u32(f, &byte_rate) != 0 ||
                read_u16(f, &block_align) != 0 ||
                read_u16(f, &bits) != 0) {
                fclose(f);
                return -1;
            }
            if (chunk_size > 16u)
                fseek(f, (long)(chunk_size - 16u), SEEK_CUR);
            have_fmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            if (!have_fmt || audio_format != 1u || channels != 1u ||
                bits != 16u || sample_rate != RX_SLOT_FRAMER_SAMPLE_RATE_HZ ||
                (chunk_size & 1u) != 0u) {
                fclose(f);
                return -1;
            }
            *out_file = f;
            *out_samples = chunk_size / 2u;
            return 0;
        } else {
            fseek(f, (long)(chunk_size + (chunk_size & 1u)), SEEK_CUR);
        }
    }

    fclose(f);
    return -1;
}

static int engine_event(void *ctx, const RxSlotFramerEvent *event)
{
    EngineSink *sink = (EngineSink *)ctx;
    Ft8EngineStatus status;

    if (!sink || !event || !sink->engine)
        return -1;

    switch (event->type) {
    case RX_SLOT_FRAMER_EVENT_BEGIN_WINDOW:
        sink->begin_count++;
        return ft8_engine_begin_window(sink->engine, event->slot_id) == FT8_ENGINE_OK ? 0 : -1;

    case RX_SLOT_FRAMER_EVENT_ENGINE_BLOCK:
        sink->block_count++;
        if (!event->samples || event->sample_count != FT8_ENGINE_BLOCK_SIZE)
            return -1;
        return ft8_engine_process_block(sink->engine, event->samples) == FT8_ENGINE_OK ? 0 : -1;

    case RX_SLOT_FRAMER_EVENT_FINALIZE_WINDOW:
        sink->finalize_count++;
        status = ft8_engine_finalize_window(sink->engine,
                                            sink->messages,
                                            FT8_DECODER_CANDIDATE_CAPACITY,
                                            &sink->slot);
        sink->finalize_status = status;
        return (status == FT8_ENGINE_OK || status == FT8_ENGINE_NO_MESSAGES) ? 0 : -1;

    case RX_SLOT_FRAMER_EVENT_STREAM_RESET:
        return ft8_engine_reset_stream(sink->engine) == FT8_ENGINE_OK ? 0 : -1;

    default:
        return -1;
    }
}

int main(int argc, char **argv)
{
    Ft8EngineConfig config = ft8_engine_baseline_config();
    Ft8EngineRequirements req;
    Ft8Engine engine;
    RxSlotFramer framer;
    EngineSink sink;
    FILE *f = NULL;
    void *workspace = NULL;
    uint32_t wav_samples = 0u;
    uint32_t consumed = 0u;
    float samples[257];
    int rc = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <ft8_cq_w1xyz_fn42.wav>\n", argv[0]);
        return 2;
    }
    if (open_pcm_data(argv[1], &f, &wav_samples) != 0) {
        fprintf(stderr, "cannot parse RX-4 reference WAV: %s\n", argv[1]);
        return 1;
    }
    if (wav_samples > RX_SLOT_FRAMER_SLOT_SAMPLES) {
        fprintf(stderr, "reference WAV is longer than one FT8 slot\n");
        goto cleanup;
    }

    if (ft8_engine_query_requirements(&config, &req) != FT8_ENGINE_OK ||
        posix_memalign(&workspace, req.alignment, req.workspace_bytes) != 0 ||
        workspace == NULL)
        goto cleanup;
    if (ft8_engine_init(&engine, &config, workspace, req.workspace_bytes) != FT8_ENGINE_OK)
        goto cleanup;
    if (rx_slot_framer_init(&framer, 12345, 0u) != RX_SLOT_FRAMER_OK)
        goto cleanup_engine;

    memset(&sink, 0, sizeof(sink));
    sink.engine = &engine;

    while (consumed < wav_samples) {
        uint32_t n = wav_samples - consumed;
        if (n > 257u)
            n = 257u;

        for (uint32_t i = 0u; i < n; ++i) {
            uint16_t raw;
            if (read_u16(f, &raw) != 0) {
                fprintf(stderr, "truncated RX-4 reference WAV\n");
                goto cleanup_engine;
            }
            samples[i] = (float)(int16_t)raw / 32768.0f;
        }

        if (rx_slot_framer_process(&framer, samples, n, engine_event, &sink) !=
            RX_SLOT_FRAMER_OK) {
            fprintf(stderr, "framer rejected reference audio at sample %u\n", consumed);
            goto cleanup_engine;
        }
        consumed += n;
    }

    fclose(f);
    f = NULL;

    memset(samples, 0, sizeof(samples));
    while (consumed < RX_SLOT_FRAMER_SLOT_SAMPLES) {
        uint32_t n = RX_SLOT_FRAMER_SLOT_SAMPLES - consumed;
        if (n > 257u)
            n = 257u;
        if (rx_slot_framer_process(&framer, samples, n, engine_event, &sink) !=
            RX_SLOT_FRAMER_OK) {
            fprintf(stderr, "framer rejected slot-end silence at sample %u\n", consumed);
            goto cleanup_engine;
        }
        consumed += n;
    }

    if (sink.begin_count != 1u || sink.block_count != 93u || sink.finalize_count != 1u ||
        sink.finalize_status != FT8_ENGINE_OK) {
        fprintf(stderr,
                "RX-4 lifecycle mismatch: begin=%zu blocks=%zu finalize=%zu status=%d\n",
                sink.begin_count, sink.block_count, sink.finalize_count,
                (int)sink.finalize_status);
        goto cleanup_engine;
    }
    if (sink.slot.slot_id != 12345 || sink.slot.message_count != 1u) {
        fprintf(stderr, "RX-4 slot mismatch: id=%lld count=%zu\n",
                (long long)sink.slot.slot_id, sink.slot.message_count);
        goto cleanup_engine;
    }
    if (memcmp(sink.slot.messages[0].payload, kExpectedPayload, FT8_PAYLOAD_BYTES) != 0 ||
        strcmp(sink.slot.messages[0].canonical_text, "CQ W1XYZ FN42") != 0) {
        fprintf(stderr, "RX-4 decoded result mismatch: '%s'\n",
                sink.slot.messages[0].canonical_text);
        goto cleanup_engine;
    }

    printf("RX4 samples=%u blocks=%zu slot=%lld text=\"%s\"\n",
           consumed, sink.block_count, (long long)sink.slot.slot_id,
           sink.slot.messages[0].canonical_text);
    puts("rx_slot_framer_rx4_reference: PASS");
    rc = 0;

cleanup_engine:
    rx_slot_framer_destroy(&framer);
    ft8_engine_destroy(&engine);
cleanup:
    if (f)
        fclose(f);
    free(workspace);
    return rc;
}
