#include "ft8_engine.h"

#include <string.h>

Ft8EngineConfig ft8_engine_baseline_config(void)
{
    Ft8EngineConfig config;

    config.monitor = ft8_monitor_baseline_config();
    config.candidate_capacity = FT8_DECODER_CANDIDATE_CAPACITY;
    config.min_score = FT8_DECODER_MIN_SCORE;
    config.max_ldpc_iterations = FT8_DECODER_MAX_LDPC_ITERATIONS;
    return config;
}

static int config_valid(const Ft8EngineConfig *config)
{
    if (config == NULL)
        return 0;
    if (config->candidate_capacity == 0u ||
        config->candidate_capacity > FT8_DECODER_CANDIDATE_CAPACITY)
        return 0;
    if (config->max_ldpc_iterations <= 0)
        return 0;
    return 1;
}

Ft8EngineStatus ft8_engine_query_requirements(const Ft8EngineConfig *config,
                                              Ft8EngineRequirements *out_req)
{
    Ft8MonitorRequirements monitor_req;

    if (!config_valid(config) || out_req == NULL)
        return FT8_ENGINE_ERR_INVALID;

    if (ft8_monitor_query_requirements(&config->monitor, &monitor_req) != FT8_MONITOR_OK)
        return FT8_ENGINE_ERR_INVALID;

    memset(out_req, 0, sizeof(*out_req));
    out_req->workspace_bytes = monitor_req.total_bytes;
    out_req->alignment = monitor_req.alignment;
    out_req->monitor_workspace_bytes = monitor_req.total_bytes;
    out_req->engine_instance_bytes = sizeof(Ft8Engine);
    out_req->candidate_storage_bytes = sizeof(((Ft8Engine *)0)->candidates);
    out_req->hash_store_bytes = sizeof(Ft8HashStore);
    return FT8_ENGINE_OK;
}

Ft8EngineStatus ft8_engine_init(Ft8Engine *engine,
                                const Ft8EngineConfig *config,
                                void *workspace,
                                size_t workspace_bytes)
{
    Ft8EngineRequirements req;
    Ft8MonitorStatus monitor_status;

    if (engine == NULL || config == NULL || workspace == NULL)
        return FT8_ENGINE_ERR_INVALID;

    memset(engine, 0, sizeof(*engine));

    if (ft8_engine_query_requirements(config, &req) != FT8_ENGINE_OK)
        return FT8_ENGINE_ERR_INVALID;
    if (workspace_bytes < req.workspace_bytes)
        return FT8_ENGINE_ERR_WORKSPACE;

    monitor_status = ft8_monitor_init(&engine->monitor,
                                      &config->monitor,
                                      workspace,
                                      workspace_bytes);
    if (monitor_status != FT8_MONITOR_OK) {
        ft8_monitor_destroy(&engine->monitor);
        memset(engine, 0, sizeof(*engine));
        if (monitor_status == FT8_MONITOR_ERR_WORKSPACE)
            return FT8_ENGINE_ERR_WORKSPACE;
        return FT8_ENGINE_ERR_INVALID;
    }

    engine->config = *config;
    ft8_hash_store_init(&engine->hash_store);
    engine->initialized = 1;
    return FT8_ENGINE_OK;
}

void ft8_engine_destroy(Ft8Engine *engine)
{
    if (engine == NULL)
        return;

    ft8_monitor_destroy(&engine->monitor);
    memset(engine, 0, sizeof(*engine));
}

Ft8EngineStatus ft8_engine_begin_window(Ft8Engine *engine, int64_t slot_id)
{
    if (engine == NULL)
        return FT8_ENGINE_ERR_INVALID;
    if (!engine->initialized)
        return FT8_ENGINE_ERR_NOT_INITIALIZED;
    if (engine->window_active)
        return FT8_ENGINE_ERR_STATE;

    if (engine->has_completed_window)
        ft8_hash_store_age_slot(&engine->hash_store);

    ft8_monitor_begin_window(&engine->monitor);
    engine->slot_id = slot_id;
    engine->window_active = 1;
    return FT8_ENGINE_OK;
}

Ft8EngineStatus ft8_engine_reset_stream(Ft8Engine *engine)
{
    if (engine == NULL)
        return FT8_ENGINE_ERR_INVALID;
    if (!engine->initialized)
        return FT8_ENGINE_ERR_NOT_INITIALIZED;

    ft8_monitor_reset_stream(&engine->monitor);
    engine->window_active = 0;
    return FT8_ENGINE_OK;
}

Ft8EngineStatus ft8_engine_process_block(
    Ft8Engine *engine,
    const float samples[FT8_MONITOR_BLOCK_SIZE])
{
    Ft8MonitorStatus status;

    if (engine == NULL || samples == NULL)
        return FT8_ENGINE_ERR_INVALID;
    if (!engine->initialized)
        return FT8_ENGINE_ERR_NOT_INITIALIZED;
    if (!engine->window_active)
        return FT8_ENGINE_ERR_STATE;

    status = ft8_monitor_process_block(&engine->monitor, samples);
    if (status == FT8_MONITOR_OK)
        return FT8_ENGINE_OK;
    if (status == FT8_MONITOR_WATERFALL_FULL)
        return FT8_ENGINE_WATERFALL_FULL;
    if (status == FT8_MONITOR_ERR_NOT_INITIALIZED)
        return FT8_ENGINE_ERR_NOT_INITIALIZED;
    return FT8_ENGINE_ERR_INTERNAL;
}

Ft8EngineStatus ft8_engine_finalize_window(Ft8Engine *engine,
                                            Ft8ProtocolMessage *message_storage,
                                            size_t message_capacity,
                                            Ft8ProtocolSlot *out_slot)
{
    Ft8WaterfallView waterfall;
    size_t candidate_count = 0u;
    size_t i;

    if (engine == NULL || out_slot == NULL ||
        (message_capacity > 0u && message_storage == NULL))
        return FT8_ENGINE_ERR_INVALID;
    if (!engine->initialized)
        return FT8_ENGINE_ERR_NOT_INITIALIZED;
    if (!engine->window_active)
        return FT8_ENGINE_ERR_STATE;

    ft8_protocol_slot_init(out_slot,
                           engine->slot_id,
                           message_storage,
                           message_capacity);

    if (ft8_monitor_get_waterfall(&engine->monitor, &waterfall) != FT8_MONITOR_OK)
        return FT8_ENGINE_ERR_INTERNAL;

    if (ft8_decoder_find_candidates(&waterfall,
                                    engine->candidates,
                                    engine->config.candidate_capacity,
                                    engine->config.min_score,
                                    &candidate_count) != FT8_DECODER_OK)
        return FT8_ENGINE_ERR_INTERNAL;

    for (i = 0u; i < candidate_count; ++i) {
        Ft8DecodedPayload decoded;
        Ft8ProtocolCodecStatus codec_status;
        Ft8ProtocolSlotAddStatus add_status;

        if (ft8_decoder_decode_candidate(&waterfall,
                                         &engine->candidates[i],
                                         engine->config.max_ldpc_iterations,
                                         &decoded) != FT8_DECODER_OK)
            continue;

        add_status = ft8_protocol_slot_decode_add(out_slot,
                                                  &decoded,
                                                  &engine->hash_store,
                                                  &codec_status);
        if (add_status == FT8_PROTOCOL_SLOT_DUPLICATE)
            continue;
        if (add_status == FT8_PROTOCOL_SLOT_ERR_FULL)
            return FT8_ENGINE_ERR_OUTPUT_FULL;
        if (add_status == FT8_PROTOCOL_SLOT_ERR_INVALID ||
            codec_status == FT8_PROTOCOL_CODEC_ERR_INVALID)
            return FT8_ENGINE_ERR_INTERNAL;
    }

    engine->window_active = 0;
    engine->has_completed_window = 1;

    if (out_slot->message_count == 0u)
        return FT8_ENGINE_NO_MESSAGES;
    return FT8_ENGINE_OK;
}
