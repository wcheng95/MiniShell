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

#define FT8_ENGINE_JOB_CANDIDATE_CAPACITY FT8_DECODER_CANDIDATE_CAPACITY
#define FT8_ENGINE_SEARCH_POSITIONS_PER_STEP 1024u

typedef enum {
    FT8_ENGINE_OK = 0,
    FT8_ENGINE_NO_MESSAGES = 1,
    FT8_ENGINE_WATERFALL_FULL = 2,
    FT8_ENGINE_BUSY = 3,
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

    /* Most recently latched UTC slot. The anchor is the absolute monitor block
     * sequence of the 960-sample block containing that boundary. */
    int slot_anchor_valid;
    int64_t slot_id;
    uint64_t slot_anchor_seq;

    Ft8EngineConfig config;
    Ft8Monitor monitor;
    Ft8HashStore hash_store;

    /* One decode job at a time, with bounded search work before LDPC. */
    int decode_active;
    int decode_search_active;
    int64_t decode_slot_id;
    uint64_t decode_anchor_seq;
    Ft8WaterfallView decode_search_waterfall;
    Ft8CandidateSearchState decode_search;
    size_t decode_candidate_count;
    size_t decode_next_candidate;
    float decode_noise_db;
    Ft8ProtocolSlot decode_slot;
    Ft8Candidate candidates[FT8_ENGINE_JOB_CANDIDATE_CAPACITY];
} Ft8Engine;

Ft8EngineConfig ft8_engine_baseline_config(void);
Ft8EngineStatus ft8_engine_query_requirements(const Ft8EngineConfig *config,
                                              Ft8EngineRequirements *out_req);
Ft8EngineStatus ft8_engine_init(Ft8Engine *engine,
                                const Ft8EngineConfig *config,
                                void *workspace,
                                size_t workspace_bytes);
void ft8_engine_destroy(Ft8Engine *engine);

/*
 * Latch the current continuous-waterfall block as the logical origin for one
 * UTC FT8 slot. This never resets the waterfall and is valid while a previous
 * slot's LDPC job is still finishing.
 */
Ft8EngineStatus ft8_engine_begin_window(Ft8Engine *engine, int64_t slot_id);

/* Reset DSP/ring continuity after a real stream discontinuity. */
Ft8EngineStatus ft8_engine_reset_stream(Ft8Engine *engine);

/* Process exactly one continuous 960-sample / 6 kHz mono-float block. */
Ft8EngineStatus ft8_engine_process_block(
    Ft8Engine *engine,
    const float samples[FT8_ENGINE_BLOCK_SIZE]);

/*
 * Start a resumable candidate-search job for the currently latched slot.
 * No full-grid scoring is done in this call. A busy previous decode job
 * returns FT8_ENGINE_BUSY; capture remains independent.
 */
Ft8EngineStatus ft8_engine_start_decode(
    Ft8Engine *engine,
    Ft8ProtocolMessage *message_storage,
    size_t message_capacity);
/*
 * Service one bounded decode unit: up to FT8_ENGINE_SEARCH_POSITIONS_PER_STEP
 * search positions while search is active, otherwise at most one LDPC
 * candidate. out_completed becomes non-zero when the slot job is complete.
 */
Ft8EngineStatus ft8_engine_decode_step(Ft8Engine *engine,
                                       int *out_completed,
                                       Ft8ProtocolSlot *out_slot);

int ft8_engine_decode_active(const Ft8Engine *engine);

/*
 * Compatibility synchronous decode for tools/tests. It performs the candidate
 * search from the current anchor in one call, but no longer resets the ring.
 */
Ft8EngineStatus ft8_engine_finalize_window(Ft8Engine *engine,
                                           Ft8ProtocolMessage *message_storage,
                                           size_t message_capacity,
                                           Ft8ProtocolSlot *out_slot);

#ifdef __cplusplus
}
#endif

#endif
