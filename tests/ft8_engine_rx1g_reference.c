#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_engine.h"

static const uint8_t kExpectedPayload[FT8_PAYLOAD_BYTES] = {
    0x00, 0x00, 0x00, 0x20, 0x60, 0x16, 0x50, 0x0A, 0x19, 0x88
};

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
                bits != 16u || sample_rate != FT8_MONITOR_SAMPLE_RATE_HZ) {
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

int main(int argc, char **argv)
{
    Ft8EngineConfig config = ft8_engine_baseline_config();
    Ft8EngineRequirements req;
    Ft8Engine engine;
    Ft8ProtocolMessage messages[FT8_DECODER_CANDIDATE_CAPACITY];
    Ft8ProtocolSlot slot;
    FILE *f = NULL;
    void *workspace = NULL;
    uint32_t sample_count = 0u;
    uint32_t full_blocks;
    float block[FT8_MONITOR_BLOCK_SIZE];
    int rc = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <ft8_cq_w1xyz_fn42.wav>\n", argv[0]);
        return 2;
    }
    if (open_pcm_data(argv[1], &f, &sample_count) != 0) {
        fprintf(stderr, "cannot parse RX-1A WAV: %s\n", argv[1]);
        return 1;
    }
    if (ft8_engine_query_requirements(&config, &req) != FT8_ENGINE_OK ||
        posix_memalign(&workspace, req.alignment, req.workspace_bytes) != 0 ||
        workspace == NULL)
        goto cleanup;
    if (ft8_engine_init(&engine, &config, workspace, req.workspace_bytes) != FT8_ENGINE_OK)
        goto cleanup;
    if (ft8_engine_begin_window(&engine, 12345) != FT8_ENGINE_OK)
        goto cleanup_engine;

    full_blocks = sample_count / FT8_MONITOR_BLOCK_SIZE;
    for (uint32_t block_index = 0u; block_index < full_blocks; ++block_index) {
        for (uint32_t i = 0u; i < FT8_MONITOR_BLOCK_SIZE; ++i) {
            uint16_t raw;
            if (read_u16(f, &raw) != 0) {
                fprintf(stderr, "truncated RX-1A WAV\n");
                goto cleanup_engine;
            }
            block[i] = (float)(int16_t)raw / 32768.0f;
        }
        if (ft8_engine_process_block(&engine, block) != FT8_ENGINE_OK) {
            fprintf(stderr, "engine rejected block %u\n", block_index);
            goto cleanup_engine;
        }
    }
    fclose(f);
    f = NULL;

    if (ft8_engine_finalize_window(&engine,
                                   messages,
                                   FT8_DECODER_CANDIDATE_CAPACITY,
                                   &slot) != FT8_ENGINE_OK) {
        fprintf(stderr, "engine finalize did not produce a decoded message\n");
        goto cleanup_engine;
    }

    if (slot.slot_id != 12345 || slot.message_count != 1u) {
        fprintf(stderr, "slot result mismatch: id=%lld count=%zu\n",
                (long long)slot.slot_id, slot.message_count);
        goto cleanup_engine;
    }
    if (memcmp(slot.messages[0].payload, kExpectedPayload, FT8_PAYLOAD_BYTES) != 0) {
        fprintf(stderr, "RX-1G payload mismatch\n");
        goto cleanup_engine;
    }
    if (slot.messages[0].type != FT8_PROTOCOL_STANDARD ||
        slot.messages[0].parse_status != FT8_PROTOCOL_PARSE_OK ||
        strcmp(slot.messages[0].canonical_text, "CQ W1XYZ FN42") != 0) {
        fprintf(stderr, "RX-1G protocol result mismatch: type=%d parse=%d text='%s'\n",
                (int)slot.messages[0].type,
                (int)slot.messages[0].parse_status,
                slot.messages[0].canonical_text);
        goto cleanup_engine;
    }

    printf("RX1G slot=%lld messages=%zu payload=",
           (long long)slot.slot_id, slot.message_count);
    for (size_t i = 0u; i < FT8_PAYLOAD_BYTES; ++i)
        printf("%02X", slot.messages[0].payload[i]);
    printf(" text=\"%s\"\n", slot.messages[0].canonical_text);
    puts("ft8_engine_rx1g_reference: PASS");
    rc = 0;

cleanup_engine:
    ft8_engine_destroy(&engine);
cleanup:
    if (f)
        fclose(f);
    free(workspace);
    return rc;
}
