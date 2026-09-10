#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_monitor.h"

#define EXPECTED_BLOCKS 85u
#define EXPECTED_2X2_ACTIVE_BYTES 147220u
#define EXPECTED_2X2_FNV64 UINT64_C(0x25B10DF3C618A9CB)
#define EXPECTED_2X1_ACTIVE_BYTES 73610u
#define EXPECTED_2X1_FNV64 UINT64_C(0x18BE1E838FD9C6AF)

static uint64_t fnv1a64(const uint8_t *data, size_t bytes)
{
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0u; i < bytes; ++i) {
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

static int run_profile(const char *path,
                       uint32_t freq_osr,
                       size_t expected_active_bytes,
                       uint64_t expected_hash,
                       const char *label)
{
    Ft8MonitorConfig cfg = ft8_monitor_baseline_config();
    Ft8MonitorRequirements req;
    Ft8Monitor mon;
    Ft8WaterfallView wf;
    FILE *f = NULL;
    void *workspace = NULL;
    uint32_t sample_count = 0u;
    float block[FT8_MONITOR_BLOCK_SIZE];
    int rc = 1;

    cfg.freq_osr = freq_osr;
    if (open_pcm_data(path, &f, &sample_count) != 0)
        return 1;
    if (ft8_monitor_query_requirements(&cfg, &req) != FT8_MONITOR_OK ||
        posix_memalign(&workspace, req.alignment, req.total_bytes) != 0 || !workspace)
        goto cleanup;
    if (ft8_monitor_init(&mon, &cfg, workspace, req.total_bytes) != FT8_MONITOR_OK)
        goto cleanup;

    for (uint32_t block_index = 0u;
         block_index < sample_count / FT8_MONITOR_BLOCK_SIZE;
         ++block_index) {
        for (uint32_t i = 0u; i < FT8_MONITOR_BLOCK_SIZE; ++i) {
            uint16_t raw;
            if (read_u16(f, &raw) != 0)
                goto cleanup_monitor;
            block[i] = (float)(int16_t)raw / 32768.0f;
        }
        if (ft8_monitor_process_block(&mon, block) != FT8_MONITOR_OK)
            goto cleanup_monitor;
    }

    if (ft8_monitor_get_waterfall(&mon, &wf) != FT8_MONITOR_OK)
        goto cleanup_monitor;

    {
        size_t active_bytes = (size_t)wf.num_blocks * wf.block_stride;
        uint64_t hash = fnv1a64(wf.mag, active_bytes);
        printf("RX1C %s blocks=%u active_bytes=%zu fnv=%016llX workspace=%zu\n",
               label, wf.num_blocks, active_bytes,
               (unsigned long long)hash, req.total_bytes);
        if (wf.num_blocks != EXPECTED_BLOCKS ||
            active_bytes != expected_active_bytes ||
            hash != expected_hash) {
            fprintf(stderr, "RX-1C %s golden mismatch\n", label);
            goto cleanup_monitor;
        }
    }

    rc = 0;

cleanup_monitor:
    ft8_monitor_destroy(&mon);
cleanup:
    if (f)
        fclose(f);
    free(workspace);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <ft8_cq_w1xyz_fn42.wav>\n", argv[0]);
        return 2;
    }

    if (run_profile(argv[1], 2u,
                    EXPECTED_2X2_ACTIVE_BYTES,
                    EXPECTED_2X2_FNV64,
                    "2x2") != 0)
        return 1;

    if (run_profile(argv[1], 1u,
                    EXPECTED_2X1_ACTIVE_BYTES,
                    EXPECTED_2X1_FNV64,
                    "2x1") != 0)
        return 1;

    puts("ft8_monitor_rx1c_reference: PASS");
    return 0;
}
