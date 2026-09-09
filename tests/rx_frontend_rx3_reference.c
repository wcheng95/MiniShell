#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_engine.h"
#include "rx_frontend.h"

#define TRANSPORT_FRAMES 257u
#define FRONTEND_OUT_CAPACITY ((TRANSPORT_FRAMES + 1u) / 2u)

static const uint8_t kExpectedPayload[FT8_PAYLOAD_BYTES] = {
    0x00, 0x00, 0x00, 0x20, 0x60, 0x16, 0x50, 0x0A, 0x19, 0x88
};

static int read_u16(FILE *f, uint16_t *out)
{
    uint8_t b[2];
    if (fread(b, 1, sizeof(b), f) != sizeof(b))
        return -1;
    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return 0;
}

static int read_u32(FILE *f, uint32_t *out)
{
    uint8_t b[4];
    if (fread(b, 1, sizeof(b), f) != sizeof(b))
        return -1;
    *out = (uint32_t)b[0] |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
    return 0;
}

static int open_6k_mono_s16(const char *path, FILE **out_file, uint32_t *out_samples)
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
    if (f == NULL)
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
            if (chunk_size > 16u && fseek(f, (long)(chunk_size - 16u), SEEK_CUR) != 0) {
                fclose(f);
                return -1;
            }
            have_fmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            if (!have_fmt || audio_format != 1u || channels != 1u ||
                bits != 16u || sample_rate != FT8_ENGINE_SAMPLE_RATE_HZ ||
                (chunk_size & 1u) != 0u) {
                fclose(f);
                return -1;
            }
            *out_file = f;
            *out_samples = chunk_size / 2u;
            return 0;
        } else {
            uint32_t skip = chunk_size + (chunk_size & 1u);
            if (fseek(f, (long)skip, SEEK_CUR) != 0)
                break;
        }
    }

    fclose(f);
    return -1;
}

static int feed_frontend_block(RxFrontend *frontend,
                               Ft8Engine *engine,
                               const int16_t *transport,
                               size_t frame_count,
                               float engine_block[FT8_ENGINE_BLOCK_SIZE],
                               size_t *engine_fill,
                               size_t *engine_blocks)
{
    float frontend_out[FRONTEND_OUT_CAPACITY];
    size_t out_count = 0u;
    size_t i;

    if (rx_frontend_process(frontend,
                            transport,
                            frame_count,
                            frontend_out,
                            FRONTEND_OUT_CAPACITY,
                            &out_count) != RX_FRONTEND_OK)
        return -1;

    for (i = 0u; i < out_count; ++i) {
        engine_block[(*engine_fill)++] = frontend_out[i];
        if (*engine_fill == FT8_ENGINE_BLOCK_SIZE) {
            if (ft8_engine_process_block(engine, engine_block) != FT8_ENGINE_OK)
                return -1;
            *engine_fill = 0u;
            ++(*engine_blocks);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    Ft8EngineConfig engine_config = ft8_engine_baseline_config();
    Ft8EngineRequirements req;
    Ft8Engine engine;
    RxFrontendConfig frontend_config = rx_frontend_baseline_config();
    RxFrontend frontend;
    Ft8ProtocolMessage messages[FT8_DECODER_CANDIDATE_CAPACITY];
    Ft8ProtocolSlot slot;
    FILE *f = NULL;
    void *workspace = NULL;
    int16_t transport[TRANSPORT_FRAMES * RX_FRONTEND_INPUT_CHANNELS];
    size_t transport_fill = 0u;
    float engine_block[FT8_ENGINE_BLOCK_SIZE];
    size_t engine_fill = 0u;
    size_t engine_blocks = 0u;
    uint32_t source_samples = 0u;
    uint32_t i;
    int engine_initialized = 0;
    int frontend_initialized = 0;
    int rc = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <pinned-6khz-mono-s16.wav>\n", argv[0]);
        return 2;
    }

    if (open_6k_mono_s16(argv[1], &f, &source_samples) != 0) {
        fprintf(stderr, "cannot open pinned 6 kHz WAV: %s\n", argv[1]);
        return 1;
    }

    if (ft8_engine_query_requirements(&engine_config, &req) != FT8_ENGINE_OK ||
        posix_memalign(&workspace, req.alignment, req.workspace_bytes) != 0 ||
        workspace == NULL)
        goto cleanup;
    if (ft8_engine_init(&engine, &engine_config, workspace, req.workspace_bytes) != FT8_ENGINE_OK)
        goto cleanup;
    engine_initialized = 1;
    if (rx_frontend_init(&frontend, &frontend_config) != RX_FRONTEND_OK)
        goto cleanup;
    frontend_initialized = 1;
    if (ft8_engine_begin_window(&engine, 0) != FT8_ENGINE_OK)
        goto cleanup;

    /*
     * Expand each pinned 6 kHz sample into two identical 12 kHz stereo frames.
     * TRANSPORT_FRAMES is deliberately odd, so flush boundaries repeatedly cut
     * through decimation pairs and exercise retained frontend phase.
     */
    for (i = 0u; i < source_samples; ++i) {
        uint16_t raw;
        int16_t sample;
        unsigned repeat;

        if (read_u16(f, &raw) != 0)
            goto cleanup;
        sample = (int16_t)raw;

        for (repeat = 0u; repeat < 2u; ++repeat) {
            transport[transport_fill * 2u] = sample;
            transport[transport_fill * 2u + 1u] = sample;
            ++transport_fill;

            if (transport_fill == TRANSPORT_FRAMES) {
                if (feed_frontend_block(&frontend, &engine,
                                        transport, transport_fill,
                                        engine_block, &engine_fill,
                                        &engine_blocks) != 0)
                    goto cleanup;
                transport_fill = 0u;
            }
        }
    }

    if (transport_fill > 0u &&
        feed_frontend_block(&frontend, &engine,
                            transport, transport_fill,
                            engine_block, &engine_fill,
                            &engine_blocks) != 0)
        goto cleanup;

    if (engine_blocks == 0u)
        goto cleanup;

    {
        Ft8EngineStatus status = ft8_engine_finalize_window(
            &engine, messages, FT8_DECODER_CANDIDATE_CAPACITY, &slot);
        if (status != FT8_ENGINE_OK)
            goto cleanup;
    }

    if (slot.message_count != 1u ||
        memcmp(slot.messages[0].payload, kExpectedPayload, FT8_PAYLOAD_BYTES) != 0 ||
        slot.messages[0].type != FT8_PROTOCOL_STANDARD ||
        slot.messages[0].parse_status != FT8_PROTOCOL_PARSE_OK ||
        strcmp(slot.messages[0].canonical_text, "CQ W1XYZ FN42") != 0)
        goto cleanup;

    printf("RX3 blocks=%zu tail=%zu text=\"%s\"\n",
           engine_blocks, engine_fill, slot.messages[0].canonical_text);
    puts("rx_frontend_rx3_reference: PASS");
    rc = 0;

cleanup:
    if (frontend_initialized)
        rx_frontend_destroy(&frontend);
    if (engine_initialized)
        ft8_engine_destroy(&engine);
    if (f != NULL)
        fclose(f);
    free(workspace);
    return rc;
}
