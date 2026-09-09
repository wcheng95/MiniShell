#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_engine.h"

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            ++failures; \
        } \
    } while (0)

static void *alloc_workspace(const Ft8EngineRequirements *req)
{
    void *workspace = NULL;
    if (posix_memalign(&workspace, req->alignment, req->workspace_bytes) != 0)
        return NULL;
    return workspace;
}

static void test_requirements_and_lifecycle(void)
{
    Ft8EngineConfig config = ft8_engine_baseline_config();
    Ft8EngineRequirements req;
    Ft8Engine engine;
    Ft8ProtocolSlot slot;
    float zero_block[FT8_MONITOR_BLOCK_SIZE] = {0};
    void *workspace;

    CHECK(ft8_engine_query_requirements(&config, &req) == FT8_ENGINE_OK);
    CHECK(req.workspace_bytes > 0u);
    CHECK(req.workspace_bytes == req.monitor_workspace_bytes);
    CHECK(req.alignment > 0u);
    CHECK(req.engine_instance_bytes == sizeof(Ft8Engine));
    CHECK(req.candidate_storage_bytes == sizeof(engine.candidates));
    CHECK(req.hash_store_bytes == sizeof(Ft8HashStore));

    workspace = alloc_workspace(&req);
    CHECK(workspace != NULL);
    if (workspace == NULL)
        return;

    memset(&engine, 0xA5, sizeof(engine));
    CHECK(ft8_engine_init(&engine, &config, workspace, req.workspace_bytes - 1u) ==
          FT8_ENGINE_ERR_WORKSPACE);
    CHECK(engine.initialized == 0);

    CHECK(ft8_engine_init(&engine, &config, workspace, req.workspace_bytes) ==
          FT8_ENGINE_OK);
    CHECK(engine.initialized == 1);
    CHECK(ft8_hash_store_count(&engine.hash_store) == 0u);

    CHECK(ft8_engine_process_block(&engine, zero_block) == FT8_ENGINE_ERR_STATE);
    CHECK(ft8_engine_finalize_window(&engine, NULL, 0u, &slot) == FT8_ENGINE_ERR_STATE);

    CHECK(ft8_engine_begin_window(&engine, 41) == FT8_ENGINE_OK);
    CHECK(ft8_engine_begin_window(&engine, 42) == FT8_ENGINE_ERR_STATE);
    CHECK(ft8_engine_process_block(&engine, zero_block) == FT8_ENGINE_OK);
    CHECK(ft8_engine_finalize_window(&engine, NULL, 0u, &slot) == FT8_ENGINE_NO_MESSAGES);
    CHECK(slot.slot_id == 41);
    CHECK(slot.message_count == 0u);
    CHECK(slot.status == FT8_PROTOCOL_SLOT_EMPTY);

    CHECK(ft8_engine_begin_window(&engine, 42) == FT8_ENGINE_OK);
    CHECK(ft8_engine_reset_stream(&engine) == FT8_ENGINE_OK);
    CHECK(ft8_engine_process_block(&engine, zero_block) == FT8_ENGINE_ERR_STATE);

    CHECK(ft8_engine_begin_window(&engine, 43) == FT8_ENGINE_OK);
    CHECK(ft8_engine_process_block(&engine, zero_block) == FT8_ENGINE_OK);
    CHECK(ft8_engine_finalize_window(&engine, NULL, 0u, &slot) == FT8_ENGINE_NO_MESSAGES);
    CHECK(slot.slot_id == 43);

    ft8_engine_destroy(&engine);
    CHECK(engine.initialized == 0);
    CHECK(ft8_engine_begin_window(&engine, 44) == FT8_ENGINE_ERR_NOT_INITIALIZED);

    free(workspace);
}

static void test_two_instances_are_independent(void)
{
    Ft8EngineConfig config = ft8_engine_baseline_config();
    Ft8EngineRequirements req;
    Ft8Engine a;
    Ft8Engine b;
    void *wa;
    void *wb;

    CHECK(ft8_engine_query_requirements(&config, &req) == FT8_ENGINE_OK);
    wa = alloc_workspace(&req);
    wb = alloc_workspace(&req);
    CHECK(wa != NULL && wb != NULL);
    if (wa == NULL || wb == NULL) {
        free(wa);
        free(wb);
        return;
    }

    CHECK(ft8_engine_init(&a, &config, wa, req.workspace_bytes) == FT8_ENGINE_OK);
    CHECK(ft8_engine_init(&b, &config, wb, req.workspace_bytes) == FT8_ENGINE_OK);
    CHECK(ft8_engine_begin_window(&a, 100) == FT8_ENGINE_OK);
    CHECK(ft8_engine_begin_window(&b, 200) == FT8_ENGINE_OK);
    CHECK(a.slot_id == 100);
    CHECK(b.slot_id == 200);
    CHECK(a.monitor.workspace == wa);
    CHECK(b.monitor.workspace == wb);
    CHECK(a.monitor.workspace != b.monitor.workspace);
    CHECK(&a.hash_store != &b.hash_store);

    ft8_engine_destroy(&a);
    ft8_engine_destroy(&b);
    free(wa);
    free(wb);
}

int main(void)
{
    test_requirements_and_lifecycle();
    test_two_instances_are_independent();

    if (failures != 0) {
        fprintf(stderr, "ft8_engine_rx1g_unit: %d failure(s)\n", failures);
        return 1;
    }

    puts("ft8_engine_rx1g_unit: PASS");
    return 0;
}
