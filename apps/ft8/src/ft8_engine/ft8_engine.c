#include "ft8_engine.h"

#include <math.h>
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

    if (engine->slot_anchor_valid && engine->pending_hash_ages != UINT32_MAX)
        ++engine->pending_hash_ages;

    engine->slot_id = slot_id;
    engine->slot_anchor_seq = ft8_monitor_next_block_sequence(&engine->monitor);
    engine->slot_anchor_valid = 1;
    return FT8_ENGINE_OK;
}

Ft8EngineStatus ft8_engine_reset_stream(Ft8Engine *engine)
{
    if (engine == NULL)
        return FT8_ENGINE_ERR_INVALID;
    if (!engine->initialized)
        return FT8_ENGINE_ERR_NOT_INITIALIZED;

    ft8_monitor_reset_stream(&engine->monitor);
    engine->slot_anchor_valid = 0;
    engine->decode_active = 0;
    engine->decode_search_active = 0;
    memset(&engine->decode_search, 0, sizeof(engine->decode_search));
    memset(&engine->decode_search_waterfall, 0, sizeof(engine->decode_search_waterfall));
    engine->decode_candidate_count = 0u;
    engine->decode_next_candidate = 0u;
    engine->decode_noise_ready = 0;
    memset(&engine->decode_slot, 0, sizeof(engine->decode_slot));
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

    status = ft8_monitor_process_block(&engine->monitor, samples);
    if (status == FT8_MONITOR_OK)
        return FT8_ENGINE_OK;
    if (status == FT8_MONITOR_WATERFALL_FULL)
        return FT8_ENGINE_WATERFALL_FULL;
    if (status == FT8_MONITOR_ERR_NOT_INITIALIZED)
        return FT8_ENGINE_ERR_NOT_INITIALIZED;
    return FT8_ENGINE_ERR_INTERNAL;
}

static const uint8_t *waterfall_block_ptr(const Ft8WaterfallView *waterfall,
                                          int logical_block)
{
    int64_t last_block;
    int64_t physical;

    if (waterfall == NULL || waterfall->mag == NULL || waterfall->max_blocks == 0u ||
        waterfall->anchor_index >= waterfall->max_blocks ||
        waterfall->num_blocks > waterfall->max_blocks)
        return NULL;

    last_block = (int64_t)waterfall->first_block + (int64_t)waterfall->num_blocks;
    if ((int64_t)logical_block < (int64_t)waterfall->first_block ||
        (int64_t)logical_block >= last_block)
        return NULL;

    physical = (int64_t)waterfall->anchor_index + (int64_t)logical_block;
    physical %= (int64_t)waterfall->max_blocks;
    if (physical < 0)
        physical += (int64_t)waterfall->max_blocks;

    return waterfall->mag + (size_t)physical * waterfall->block_stride;
}

/*
 * Preserve the MiniFT8-V2 RX SNR definition at the engine boundary. For I001
 * the percentile is taken over the currently retained circular view.
 */
static float rx_noise_floor_db(const Ft8WaterfallView *waterfall)
{
    uint32_t hist[256] = {0};
    size_t total = 0u;
    uint64_t target;
    uint64_t accum = 0u;
    int noise_scaled = 0;
    int v;

    if (waterfall == NULL || waterfall->mag == NULL || waterfall->num_blocks == 0u)
        return -120.0f;

    for (uint32_t b = 0u; b < waterfall->num_blocks; ++b) {
        int logical = waterfall->first_block + (int)b;
        const uint8_t *block = waterfall_block_ptr(waterfall, logical);
        if (block == NULL)
            continue;
        for (uint32_t i = 0u; i < waterfall->block_stride; ++i)
            ++hist[block[i]];
        total += waterfall->block_stride;
    }

    if (total == 0u)
        return -120.0f;

    target = ((uint64_t)total * 25u) / 100u;
    for (v = 0; v < 256; ++v) {
        accum += hist[v];
        if (accum >= target) {
            noise_scaled = v;
            break;
        }
    }
    return 0.5f * ((float)noise_scaled - 240.0f);
}

static int8_t rx_candidate_snr_db(const Ft8WaterfallView *waterfall,
                                  const Ft8Candidate *candidate,
                                  float noise_db)
{
    const uint8_t *block;
    size_t offset;
    float candidate_db = noise_db;
    int snr;

    if (waterfall == NULL || candidate == NULL || waterfall->mag == NULL)
        return 0;

    block = waterfall_block_ptr(waterfall, candidate->time_offset);
    if (block != NULL &&
        candidate->time_sub < waterfall->time_osr &&
        candidate->freq_sub < waterfall->freq_osr &&
        candidate->freq_offset >= 0 &&
        candidate->freq_offset < (int)waterfall->num_bins) {
        offset = (size_t)candidate->time_sub * waterfall->freq_osr * waterfall->num_bins;
        offset += (size_t)candidate->freq_sub * waterfall->num_bins;
        offset += (size_t)candidate->freq_offset;
        if (offset < waterfall->block_stride)
            candidate_db = 0.5f * ((float)block[offset] - 240.0f);
    }

    snr = (int)lrintf(candidate_db - noise_db);
    if (snr < -30) snr = -30;
    if (snr > 99) snr = 99;
    return (int8_t)snr;
}

static int16_t rx_candidate_offset_hz(const Ft8Engine *engine,
                                      const Ft8Candidate *candidate)
{
    float symbol_period;
    float freq_hz;
    long rounded;

    if (engine == NULL || candidate == NULL || engine->monitor.req.block_size == 0u ||
        engine->config.monitor.sample_rate_hz == 0u || engine->monitor.config.freq_osr == 0u)
        return 0;

    symbol_period = (float)engine->monitor.req.block_size /
                    (float)engine->config.monitor.sample_rate_hz;
    freq_hz = ((float)engine->monitor.req.min_bin + (float)candidate->freq_offset +
               (float)candidate->freq_sub / (float)engine->monitor.config.freq_osr) /
              symbol_period;
    rounded = lrintf(freq_hz);
    if (rounded < -32768L) rounded = -32768L;
    if (rounded > 32767L) rounded = 32767L;
    return (int16_t)rounded;
}

static Ft8EngineStatus decode_one_candidate(Ft8Engine *engine,
                                            const Ft8WaterfallView *waterfall,
                                            const Ft8Candidate *candidate,
                                            Ft8ProtocolSlot *slot,
                                            float noise_db)
{
    Ft8DecodedPayload decoded;
    Ft8ProtocolCodecStatus codec_status;
    Ft8ProtocolSlotAddStatus add_status;

    if (ft8_decoder_decode_candidate(waterfall,
                                     candidate,
                                     engine->config.max_ldpc_iterations,
                                     &decoded) != FT8_DECODER_OK)
        return FT8_ENGINE_OK;

    add_status = ft8_protocol_slot_decode_add(slot,
                                             &decoded,
                                             &engine->hash_store,
                                             &codec_status);
    if (add_status == FT8_PROTOCOL_SLOT_DUPLICATE)
        return FT8_ENGINE_OK;
    if (add_status == FT8_PROTOCOL_SLOT_ERR_FULL)
        return FT8_ENGINE_ERR_OUTPUT_FULL;
    if (add_status == FT8_PROTOCOL_SLOT_ERR_INVALID ||
        codec_status == FT8_PROTOCOL_CODEC_ERR_INVALID)
        return FT8_ENGINE_ERR_INTERNAL;

    if (add_status == FT8_PROTOCOL_SLOT_ADDED && slot->message_count > 0u) {
        Ft8ProtocolMessage *message = &slot->messages[slot->message_count - 1u];
        message->snr_db = rx_candidate_snr_db(waterfall, &decoded.candidate, noise_db);
        message->offset_hz = rx_candidate_offset_hz(engine, &decoded.candidate);
    }
    return FT8_ENGINE_OK;
}

static void apply_pending_hash_ages(Ft8Engine *engine)
{
    while (engine->pending_hash_ages > 0u) {
        ft8_hash_store_age_slot(&engine->hash_store);
        --engine->pending_hash_ages;
    }
}

Ft8EngineStatus ft8_engine_start_decode(
    Ft8Engine *engine,
    Ft8ProtocolMessage *message_storage,
    size_t message_capacity)
{
    Ft8WaterfallView waterfall;

    if (engine == NULL || (message_capacity > 0u && message_storage == NULL))
        return FT8_ENGINE_ERR_INVALID;
    if (!engine->initialized)
        return FT8_ENGINE_ERR_NOT_INITIALIZED;
    if (!engine->slot_anchor_valid)
        return FT8_ENGINE_ERR_STATE;
    if (engine->decode_active)
        return FT8_ENGINE_BUSY;

    if (ft8_monitor_get_waterfall_at(&engine->monitor,
                                     engine->slot_anchor_seq,
                                     &waterfall) != FT8_MONITOR_OK)
        return FT8_ENGINE_ERR_INTERNAL;

    if (ft8_decoder_candidate_search_begin(&engine->decode_search,
                                           engine->config.candidate_capacity,
                                           engine->config.min_score) != FT8_DECODER_OK)
        return FT8_ENGINE_ERR_INTERNAL;

    apply_pending_hash_ages(engine);
    engine->decode_slot_id = engine->slot_id;
    engine->decode_anchor_seq = engine->slot_anchor_seq;
    engine->decode_search_waterfall = waterfall;
    engine->decode_search_active = 1;
    engine->decode_candidate_count = 0u;
    engine->decode_next_candidate = 0u;
    engine->decode_noise_ready = 0;
    engine->decode_noise_db = 0.0f;
    ft8_protocol_slot_init(&engine->decode_slot,
                           engine->decode_slot_id,
                           message_storage,
                           message_capacity);
    engine->decode_active = 1;
    return FT8_ENGINE_OK;
}

Ft8EngineStatus ft8_engine_decode_step(Ft8Engine *engine,
                                       int *out_completed,
                                       Ft8ProtocolSlot *out_slot)
{
    Ft8WaterfallView waterfall;
    Ft8EngineStatus status;

    if (out_completed)
        *out_completed = 0;
    if (engine == NULL || out_completed == NULL || out_slot == NULL)
        return FT8_ENGINE_ERR_INVALID;
    if (!engine->initialized)
        return FT8_ENGINE_ERR_NOT_INITIALIZED;
    if (!engine->decode_active)
        return FT8_ENGINE_ERR_STATE;

    if (engine->decode_search_active) {
        int search_completed = 0;
        size_t candidate_count = 0u;

        if (ft8_decoder_candidate_search_step(&engine->decode_search_waterfall,
                                              &engine->decode_search,
                                              engine->candidates,
                                              FT8_ENGINE_SEARCH_POSITIONS_PER_STEP,
                                              &search_completed,
                                              &candidate_count) != FT8_DECODER_OK) {
            return FT8_ENGINE_ERR_INTERNAL;
        }

        if (!search_completed)
            return FT8_ENGINE_OK;

        engine->decode_search_active = 0;
        engine->decode_candidate_count = candidate_count;
        engine->decode_next_candidate = 0u;
        engine->decode_noise_ready = 0;

        if (candidate_count == 0u) {
            *out_slot = engine->decode_slot;
            *out_completed = 1;
            engine->decode_active = 0;
            return FT8_ENGINE_NO_MESSAGES;
        }

        /* Keep one service unit bounded: LDPC begins on the next RX step. */
        return FT8_ENGINE_OK;
    }

    if (!engine->decode_noise_ready) {
        engine->decode_noise_db = rx_noise_floor_db(&engine->decode_search_waterfall);
        engine->decode_noise_ready = 1;
        return FT8_ENGINE_OK;
    }

    if (engine->decode_next_candidate < engine->decode_candidate_count) {
        waterfall = engine->decode_search_waterfall;

        status = decode_one_candidate(engine,
                                      &waterfall,
                                      &engine->candidates[engine->decode_next_candidate],
                                      &engine->decode_slot,
                                      engine->decode_noise_db);
        ++engine->decode_next_candidate;
        if (status != FT8_ENGINE_OK)
            return status;
    }

    if (engine->decode_next_candidate >= engine->decode_candidate_count) {
        *out_slot = engine->decode_slot;
        *out_completed = 1;
        engine->decode_active = 0;
        return out_slot->message_count == 0u ? FT8_ENGINE_NO_MESSAGES : FT8_ENGINE_OK;
    }

    return FT8_ENGINE_OK;
}

int ft8_engine_decode_active(const Ft8Engine *engine)
{
    return engine != NULL && engine->initialized && engine->decode_active;
}

Ft8EngineStatus ft8_engine_cancel_decode(Ft8Engine *engine)
{
    if (engine == NULL)
        return FT8_ENGINE_ERR_INVALID;
    if (!engine->initialized)
        return FT8_ENGINE_ERR_NOT_INITIALIZED;

    engine->decode_active = 0;
    engine->decode_search_active = 0;
    memset(&engine->decode_search, 0, sizeof(engine->decode_search));
    memset(&engine->decode_search_waterfall, 0, sizeof(engine->decode_search_waterfall));
    engine->decode_candidate_count = 0u;
    engine->decode_next_candidate = 0u;
    engine->decode_noise_ready = 0;
    memset(&engine->decode_slot, 0, sizeof(engine->decode_slot));
    return FT8_ENGINE_OK;
}

Ft8EngineStatus ft8_engine_finalize_window(Ft8Engine *engine,
                                           Ft8ProtocolMessage *message_storage,
                                           size_t message_capacity,
                                           Ft8ProtocolSlot *out_slot)
{
    Ft8WaterfallView waterfall;
    size_t candidate_count = 0u;
    float noise_db;

    if (engine == NULL || out_slot == NULL ||
        (message_capacity > 0u && message_storage == NULL))
        return FT8_ENGINE_ERR_INVALID;
    if (!engine->initialized)
        return FT8_ENGINE_ERR_NOT_INITIALIZED;
    if (!engine->slot_anchor_valid || engine->decode_active)
        return FT8_ENGINE_ERR_STATE;

    apply_pending_hash_ages(engine);
    ft8_protocol_slot_init(out_slot,
                           engine->slot_id,
                           message_storage,
                           message_capacity);

    if (ft8_monitor_get_waterfall_at(&engine->monitor,
                                     engine->slot_anchor_seq,
                                     &waterfall) != FT8_MONITOR_OK)
        return FT8_ENGINE_ERR_INTERNAL;

    noise_db = rx_noise_floor_db(&waterfall);

    if (ft8_decoder_find_candidates(&waterfall,
                                    engine->candidates,
                                    engine->config.candidate_capacity,
                                    engine->config.min_score,
                                    &candidate_count) != FT8_DECODER_OK)
        return FT8_ENGINE_ERR_INTERNAL;

    for (size_t i = 0u; i < candidate_count; ++i) {
        Ft8EngineStatus status = decode_one_candidate(engine,
                                                      &waterfall,
                                                      &engine->candidates[i],
                                                      out_slot,
                                                      noise_db);
        if (status != FT8_ENGINE_OK)
            return status;
    }

    return out_slot->message_count == 0u ? FT8_ENGINE_NO_MESSAGES : FT8_ENGINE_OK;
}
