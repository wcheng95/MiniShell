#ifndef JS8_LIVE_H
#define JS8_LIVE_H
#include "minishell/api.h"
#include "js8_decoder.h"
#include "js8_activity_json.h"
#include "js8_frontend.h"
#include "js8_slot.h"
#include <stdatomic.h>

typedef struct {
    const char *rx, *cat, *log;
    int have_dial;
    int64_t dial_hz;
    uint32_t slots;
} Js8LiveOptions;
int js8_live_options(int argc, char **argv, Js8LiveOptions *out);
/* The platform composition starts/joins a worker. App/engine code never uses
 * native threads. One immutable waterfall job; release/acquire transfers ownership. */
typedef struct Js8Live {
    const mini_api_t *api;
    Js8Monitor monitor;
    void *monitor_allocation, *job_allocation;
    Js8Frontend frontend;
    Js8SlotScheduler scheduler;
    float block[960];
    size_t block_fill;
    atomic_int job_state; /* 0 idle, 1 requested/worker-owned, 2 finished */
    atomic_uint generation;
    uint32_t job_generation, job_slot;
    Js8WaterfallView job_view;
    Js8DecodedPayload decoded[JS8_DECODER_CANDIDATE_CAPACITY];
    size_t decoded_count, candidate_count;
    int decode_error;
    uint64_t decode_us, max_read_gap_us, last_read_us;
    uint32_t dropped, discontinuities, processed;
    Js8RxReassembly reassembly;
    mini_file_t log_file, dictionary_file;
    Js8JscDictionary dictionary;
    int dictionary_attempted, dictionary_ready;
    Js8LogMetadata metadata;
    const char *error;
} Js8Live;
typedef struct { int (*start)(Js8Live *); void (*stop)(Js8Live *); } Js8Worker;
int js8chat_run(const mini_api_t *, int argc, char **argv, const Js8Worker *);
int js8_live_init(Js8Live *, const mini_api_t *);
void js8_live_destroy(Js8Live *);
void js8_live_reset(Js8Live *);
int js8_live_chunk(Js8Live *, const int16_t *, size_t frames);
/* Called only by the single platform worker; returns whether work was done. */
int js8_live_decode_step(Js8Live *);
int js8_live_publish(Js8Live *);
int js8_live_payload(Js8Live *, uint32_t slot, const Js8DecodedPayload *);
int js8_live_emit(Js8Live *, const Js8Activity *);
mini_result_t js8_qmx_open(const mini_serial_api_t *, const char *, uint32_t, mini_serial_t *);
#endif
