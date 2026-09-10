#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_monitor.h"

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } \
} while (0)

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

static void fill_block(float *block, int block_index)
{
    int i;
    for (i = 0; i < (int)FT8_MONITOR_BLOCK_SIZE; ++i) {
        double n = (double)(block_index * (int)FT8_MONITOR_BLOCK_SIZE + i);
        double t = n / (double)FT8_MONITOR_SAMPLE_RATE_HZ;
        block[i] = (float)(0.27 * sin(2.0 * 3.14159265358979323846 * 731.25 * t) +
                           0.11 * cos(2.0 * 3.14159265358979323846 * 1525.0 * t));
    }
}

static void test_requirements(void)
{
    Ft8MonitorConfig cfg = ft8_monitor_baseline_config();
    Ft8MonitorRequirements req;

    /* Production default: time_osr=2, freq_osr=2. */
    CHECK(cfg.time_osr == 2u);
    CHECK(cfg.freq_osr == 2u);
    CHECK(ft8_monitor_query_requirements(&cfg, &req) == FT8_MONITOR_OK);
    CHECK(req.block_size == 960u);
    CHECK(req.subblock_size == 480u);
    CHECK(req.nfft == 1920u);
    CHECK(req.min_bin == 32u);
    CHECK(req.max_bin == 465u);
    CHECK(req.num_bins == 433u);
    CHECK(req.max_blocks == 93u);
    CHECK(req.block_stride == 1732u);
    CHECK(req.waterfall_bytes == 161076u);
    CHECK(req.total_bytes > req.waterfall_bytes);
    CHECK(req.fft_plan_bytes > 0u);
    CHECK(req.alignment >= _Alignof(void *));

    /* Explicit low-memory/V2-compatible fallback remains valid. */
    cfg.freq_osr = 1u;
    CHECK(ft8_monitor_query_requirements(&cfg, &req) == FT8_MONITOR_OK);
    CHECK(req.nfft == 960u);
    CHECK(req.block_stride == 866u);
    CHECK(req.waterfall_bytes == 80538u);

    cfg.sample_rate_hz = 12000u;
    CHECK(ft8_monitor_query_requirements(&cfg, &req) == FT8_MONITOR_ERR_INVALID);
}

static void test_workspace_failures(void)
{
    Ft8MonitorConfig cfg = ft8_monitor_baseline_config();
    Ft8MonitorRequirements req;
    Ft8Monitor mon;
    void *workspace = NULL;
    uint8_t *raw;

    CHECK(ft8_monitor_query_requirements(&cfg, &req) == FT8_MONITOR_OK);
    CHECK(posix_memalign(&workspace, req.alignment, req.total_bytes) == 0);
    if (!workspace)
        return;

    CHECK(ft8_monitor_init(&mon, &cfg, workspace, req.total_bytes - 1u) == FT8_MONITOR_ERR_WORKSPACE);
    CHECK(mon.initialized == 0);
    free(workspace);

    raw = (uint8_t *)malloc(req.total_bytes + req.alignment);
    CHECK(raw != NULL);
    if (raw) {
        CHECK(ft8_monitor_init(&mon, &cfg, raw + 1u, req.total_bytes) == FT8_MONITOR_ERR_WORKSPACE);
        CHECK(mon.initialized == 0);
        free(raw);
    }
}

static void test_instance_independence(void)
{
    Ft8MonitorConfig cfg = ft8_monitor_baseline_config();
    Ft8MonitorRequirements req;
    Ft8Monitor a, b;
    Ft8WaterfallView wa, wb;
    void *mem_a = NULL;
    void *mem_b = NULL;
    float block[FT8_MONITOR_BLOCK_SIZE];
    int i;

    CHECK(ft8_monitor_query_requirements(&cfg, &req) == FT8_MONITOR_OK);
    CHECK(posix_memalign(&mem_a, req.alignment, req.total_bytes) == 0);
    CHECK(posix_memalign(&mem_b, req.alignment, req.total_bytes) == 0);
    if (!mem_a || !mem_b)
        goto out;

    CHECK(ft8_monitor_init(&a, &cfg, mem_a, req.total_bytes) == FT8_MONITOR_OK);
    CHECK(ft8_monitor_init(&b, &cfg, mem_b, req.total_bytes) == FT8_MONITOR_OK);
    CHECK(a.waterfall != b.waterfall);
    CHECK(a.history != b.history);
    CHECK(a.fft_cfg != b.fft_cfg);

    for (i = 0; i < 8; ++i) {
        fill_block(block, i);
        CHECK(ft8_monitor_process_block(&a, block) == FT8_MONITOR_OK);
        CHECK(ft8_monitor_process_block(&b, block) == FT8_MONITOR_OK);
    }

    CHECK(ft8_monitor_get_waterfall(&a, &wa) == FT8_MONITOR_OK);
    CHECK(ft8_monitor_get_waterfall(&b, &wb) == FT8_MONITOR_OK);
    CHECK(wa.num_blocks == 8u);
    CHECK(wb.num_blocks == 8u);
    CHECK(memcmp(wa.mag, wb.mag, (size_t)wa.num_blocks * wa.block_stride) == 0);
    CHECK(fnv1a64(wa.mag, (size_t)wa.num_blocks * wa.block_stride) ==
          fnv1a64(wb.mag, (size_t)wb.num_blocks * wb.block_stride));

    ft8_monitor_destroy(&a);
    ft8_monitor_destroy(&b);
    CHECK(a.initialized == 0 && a.workspace == NULL);
    CHECK(b.initialized == 0 && b.workspace == NULL);

out:
    free(mem_a);
    free(mem_b);
}

static void test_reset_semantics(void)
{
    Ft8MonitorConfig cfg = ft8_monitor_baseline_config();
    Ft8MonitorRequirements req;
    Ft8Monitor mon;
    void *workspace = NULL;
    float block[FT8_MONITOR_BLOCK_SIZE];
    float *saved = NULL;

    CHECK(ft8_monitor_query_requirements(&cfg, &req) == FT8_MONITOR_OK);
    CHECK(posix_memalign(&workspace, req.alignment, req.total_bytes) == 0);
    if (!workspace)
        return;
    CHECK(ft8_monitor_init(&mon, &cfg, workspace, req.total_bytes) == FT8_MONITOR_OK);

    fill_block(block, 0);
    CHECK(ft8_monitor_process_block(&mon, block) == FT8_MONITOR_OK);
    saved = (float *)malloc(req.history_bytes);
    CHECK(saved != NULL);
    if (!saved)
        goto out;
    memcpy(saved, mon.history, req.history_bytes);

    ft8_monitor_begin_window(&mon);
    CHECK(mon.num_blocks == 0u);
    CHECK(memcmp(saved, mon.history, req.history_bytes) == 0);

    ft8_monitor_reset_stream(&mon);
    CHECK(mon.num_blocks == 0u);
    {
        size_t i;
        for (i = 0; i < req.nfft; ++i)
            CHECK(mon.history[i] == 0.0f);
    }

out:
    free(saved);
    ft8_monitor_destroy(&mon);
    free(workspace);
}

static void test_full_waterfall(void)
{
    Ft8MonitorConfig cfg = ft8_monitor_baseline_config();
    Ft8MonitorRequirements req;
    Ft8Monitor mon;
    void *workspace = NULL;
    float block[FT8_MONITOR_BLOCK_SIZE] = {0};
    uint32_t i;

    CHECK(ft8_monitor_query_requirements(&cfg, &req) == FT8_MONITOR_OK);
    CHECK(posix_memalign(&workspace, req.alignment, req.total_bytes) == 0);
    if (!workspace)
        return;
    CHECK(ft8_monitor_init(&mon, &cfg, workspace, req.total_bytes) == FT8_MONITOR_OK);

    for (i = 0u; i < req.max_blocks; ++i)
        CHECK(ft8_monitor_process_block(&mon, block) == FT8_MONITOR_OK);
    CHECK(mon.num_blocks == req.max_blocks);
    CHECK(ft8_monitor_process_block(&mon, block) == FT8_MONITOR_WATERFALL_FULL);
    CHECK(mon.num_blocks == req.max_blocks);

    ft8_monitor_destroy(&mon);
    free(workspace);
}

int main(void)
{
    test_requirements();
    test_workspace_failures();
    test_instance_independence();
    test_reset_semantics();
    test_full_waterfall();

    if (failures != 0) {
        fprintf(stderr, "ft8_monitor_rx1c: %d failure(s)\n", failures);
        return 1;
    }
    puts("ft8_monitor_rx1c: PASS");
    return 0;
}
