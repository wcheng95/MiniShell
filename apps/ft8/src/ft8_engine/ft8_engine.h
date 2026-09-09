#ifndef FT8_ENGINE_H
#define FT8_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "ft8_decoder.h"
#include "ft8_hash_store.h"
#include "ft8_message_codec.h"
#include "ft8_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Public engine-native RX contract. */
#define FT8_ENGINE_SAMPLE_RATE_HZ FT8_MONITOR_SAMPLE_RATE_HZ
#define FT8_ENGINE_BLOCK_SIZE FT8_MONITOR_BLOCK_SIZE

typedef enum {
    FT8_ENGINE_OK = 0,
    FT8_ENGINE_NO_MESSAGES = 1,
    FT8_ENGINE_WATERFALL_FULL = 2,
    FT8_ENGINE_ERR_INVALID = -1,
    FT8_ENGINE_ERR_WORKSPACE = -2,
    FT8_ENGINE_ERR_NOT_INITIALIZED = -3,
    FT8_ENGINE_ERR_STATE = -4,
    FT8_ENGINE_ERR_OUTPUT_FULL = -5,
    FT8_ENGINE_ERR_INTERNAL = -6
} Ft8EngineStatus;

typedef struct {
    Ft8MonitorConfig monitor;
    size_t candidate_capacity;
    int min_score;
    int max_ldpc_iterations;
} Ft8EngineConfig;

typedef struct {
    size_t workspace_bytes;
    size_t alignment;
    size_t monitor_workspace_bytes;

    /* Fixed storage visible in the caller-owned Ft8Engine instance. */
    size_t engine_instance_bytes;
    size_t candidate_storage_bytes;
    size_t hash_store_bytes;
} Ft8EngineRequirements;

typedef struct {
    int initialized;
    int window_active;
    int has_completed_window;
    int64_t slot_id;

    Ft8EngineConfig config;
    Ft8Monitor monitor;
    Ft8HashStore hash_store;
    Ft8Candidate candidates[FT8_DECODER_CANDIDATE_CAPACITY];
} Ft8Engine;

Ft8EngineConfig ft8_engine_baseline_config(void);
Ft8EngineStatus ft8_engine_query_requirements(const Ft8EngineConfig *config,
                                              Ft8EngineRequirements *out_req);
Ft8EngineStatus ft8_engine_init(Ft8Engine *engine,
                                const Ft8EngineConfig *config,
                                void *workspace,
                                size_t workspace_bytes);
void ft8_engine_destroy(Ft8Engine *engine);

/* Begin one complete FT8 decode window. slot_id is ordinary caller-owned identity. */
Ft8EngineStatus ft8_engine_begin_window(Ft8Engine *engine, int64_t slot_id);

/* Reset sample/DSP continuity without discarding persistent protocol knowledge. */
Ft8EngineStatus ft8_engine_reset_stream(Ft8Engine *engine);

/* Process exactly one engine-native 960-sample / 6 kHz mono-float block. */
Ft8EngineStatus ft8_engine_process_block(
    Ft8Engine *engine,
    const float samples[FT8_ENGINE_BLOCK_SIZE]);

/*
 * Decode the current completed window into caller-supplied protocol-message
 * storage. Valid payloads are deduplicated by exact 10-byte payload identity.
 * A successful no-decode window returns FT8_ENGINE_NO_MESSAGES.
 */
Ft8EngineStatus ft8_engine_finalize_window(Ft8Engine *engine,
                                            Ft8ProtocolMessage *message_storage,
                                            size_t message_capacity,
                                            Ft8ProtocolSlot *out_slot);

#ifdef __cplusplus
}
#endif

#endif
