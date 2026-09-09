#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_monitor.h"

#define EXPECTED_BLOCKS 85u
#define EXPECTED_ACTIVE_BYTES 73610u
#define EXPECTED_FNV64 UINT64_C(0x18BE1E838FD9C6AF)

static uint64_t fnv1a64(const uint8_t *data, size_t bytes)
{
    uint64_t h = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < bytes; ++i) {
        h ^= data[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

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
    Ft8MonitorConfig cfg = ft8_monitor_baseline_config();
    Ft8MonitorRequirements req;
    Ft8Monitor mon;
    Ft8WaterfallView wf;
    FILE *f = NULL;
    void *workspace = NULL;
    uint32_t sample_count = 0u;
    uint32_t full_blocks;
    uint32_t block_index;
    float block[FT8_MONITOR_BLOCK_SIZE];
    size_t active_bytes;
    uint64_t hash;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <ft8_cq_w1xyz_fn42.wav>\n", argv[0]);
        return 2;
    }
    if (open_pcm_data(argv[1], &f, &sample_count) != 0) {
        fprintf(stderr, "cannot parse RX-1A WAV: %s\n", argv[1]);
        return 1;
    }
    if (ft8_monitor_query_requirements(&cfg, &req) != FT8_MONITOR_OK ||
        posix_memalign(&workspace, req.alignment, req.total_bytes) != 0 || !workspace) {
        fclose(f);
        return 1;
    }
    if (ft8_monitor_init(&mon, &cfg, workspace, req.total_bytes) != FT8_MONITOR_OK) {
        free(workspace);
        fclose(f);
        return 1;
    }

    full_blocks = sample_count / FT8_MONITOR_BLOCK_SIZE;
    for (block_index = 0u; block_index < full_blocks; ++block_index) {
        uint32_t i;
        for (i = 0u; i < FT8_MONITOR_BLOCK_SIZE; ++i) {
            uint16_t raw;
            int16_t sample;
            if (read_u16(f, &raw) != 0) {
                fprintf(stderr, "truncated RX-1A WAV\n");
                return 1;
            }
            sample = (int16_t)raw;
            block[i] = (float)sample / 32768.0f;
        }
        if (ft8_monitor_process_block(&mon, block) != FT8_MONITOR_OK) {
            fprintf(stderr, "monitor rejected block %u\n", block_index);
            return 1;
        }
    }
    fclose(f);

    if (ft8_monitor_get_waterfall(&mon, &wf) != FT8_MONITOR_OK)
        return 1;
    active_bytes = (size_t)wf.num_blocks * wf.block_stride;
    hash = fnv1a64(wf.mag, active_bytes);

    printf("RX1C blocks=%u active_bytes=%zu fnv=%016llX workspace=%zu\n",
           wf.num_blocks, active_bytes, (unsigned long long)hash, req.total_bytes);

    if (wf.num_blocks != EXPECTED_BLOCKS ||
        active_bytes != EXPECTED_ACTIVE_BYTES ||
        hash != EXPECTED_FNV64) {
        fprintf(stderr,
                "RX-1A mismatch: blocks=%u/%u bytes=%zu/%u hash=%016llX/%016llX\n",
                wf.num_blocks, EXPECTED_BLOCKS,
                active_bytes, EXPECTED_ACTIVE_BYTES,
                (unsigned long long)hash,
                (unsigned long long)EXPECTED_FNV64);
        return 1;
    }

    ft8_monitor_destroy(&mon);
    free(workspace);
    puts("ft8_monitor_rx1c_reference: PASS");
    return 0;
}
