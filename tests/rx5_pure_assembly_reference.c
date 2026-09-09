#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_engine.h"
#include "rx_frontend.h"
#include "rx_result_builder.h"
#include "rx_slot_framer.h"

#define TRANSPORT_FRAMES 257u
#define FRONTEND_OUT_CAPACITY ((TRANSPORT_FRAMES + 1u) / 2u)

static const uint8_t kExpectedPayload[FT8_PAYLOAD_BYTES] = {
    0x00, 0x00, 0x00, 0x20, 0x60, 0x16, 0x50, 0x0A, 0x19, 0x88
};

typedef struct {
    Ft8Engine *engine;
    RxResultBuilder *builder;
    Ft8ProtocolMessage protocol_messages[FT8_DECODER_CANDIDATE_CAPACITY];
    RxMessage rx_messages[FT8_DECODER_CANDIDATE_CAPACITY];
    RxBatch batch;
    size_t begin_count;
    size_t block_count;
    size_t finalize_count;
} AssemblySink;

static int read_u16(FILE *f, uint16_t *out)
{
    uint8_t b[2];
    if (fread(b, 1, sizeof(b), f) != sizeof(b))
        return -1;
    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return 0;
}

static int read_u32(FILE *f, uint32_t *out)
{
    uint8_t b[4];
    if (fread(b, 1, sizeof(b), f) != sizeof(b))
        return -1;
    *out = (uint32_t)b[0] |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
    return 0;
}

static int open_6k_mono_s16(const char *path, FILE **out_file, uint32_t *out_samples)
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
    if (f == NULL)
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
            if (chunk_size > 16u && fseek(f, (long)(chunk_size - 16u), SEEK_CUR) != 0) {
                fclose(f);
                return -1;
            }
            have_fmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            if (!have_fmt || audio_format != 1u || channels != 1u ||
                bits != 16u || sample_rate != FT8_ENGINE_SAMPLE_RATE_HZ ||
                (chunk_size & 1u) != 0u) {
                fclose(f);
                return -1;
            }
            *out_file = f;
            *out_samples = chunk_size / 2u;
            return 0;
        } else {
            uint32_t skip = chunk_size + (chunk_size & 1u);
            if (fseek(f, (long)skip, SEEK_CUR) != 0)
                break;
        }
    }

    fclose(f);
    return -1;
}

static int assembly_emit(void *ctx, const RxSlotFramerEvent *event)
{
    AssemblySink *sink = (AssemblySink *)ctx;

    if (sink == NULL || event == NULL)
        return -1;

    switch (event->type) {
    case RX_SLOT_FRAMER_EVENT_BEGIN_WINDOW:
        ++sink->begin_count;
        return ft8_engine_begin_window(sink->engine, event->slot_id) == FT8_ENGINE_OK ? 0 : -1;

    case RX_SLOT_FRAMER_EVENT_ENGINE_BLOCK:
        ++sink->block_count;
        return ft8_engine_process_block(sink->engine, event->samples) == FT8_ENGINE_OK ? 0 : -1;

    case RX_SLOT_FRAMER_EVENT_FINALIZE_WINDOW: {
        Ft8ProtocolSlot slot;
        Ft8EngineStatus status;
        RxResultStatus result_status;

        ++sink->finalize_count;
        status = ft8_engine_finalize_window(sink->engine,
                                            sink->protocol_messages,
                                            FT8_DECODER_CANDIDATE_CAPACITY,
                                            &slot);
        if (status != FT8_ENGINE_OK && status != FT8_ENGINE_NO_MESSAGES)
            return -1;
        result_status = rx_result_builder_build(sink->builder,
                                                &slot,
                                                sink->rx_messages,
                                                FT8_DECODER_CANDIDATE_CAPACITY,
                                                &sink->batch);
        return result_status == RX_RESULT_OK ? 0 : -1;
    }

    case RX_SLOT_FRAMER_EVENT_STREAM_RESET:
        return ft8_engine_reset_stream(sink->engine) == FT8_ENGINE_OK ? 0 : -1;
    }

    return -1;
}

static int flush_transport(RxFrontend *frontend,
                           RxSlotFramer *framer,
                           AssemblySink *sink,
                           const int16_t *transport,
                           size_t frame_count)
{
    float out[FRONTEND_OUT_CAPACITY];
    size_t out_count = 0u;

    if (rx_frontend_process(frontend,
                            transport,
                            frame_count,
                            out,
                            FRONTEND_OUT_CAPACITY,
                            &out_count) != RX_FRONTEND_OK)
        return -1;

    if (out_count > 0u &&
        rx_slot_framer_process(framer,
                               out,
                               out_count,
                               assembly_emit,
                               sink) != RX_SLOT_FRAMER_OK)
        return -1;
    return 0;
}

int main(int argc, char **argv)
{
    Ft8EngineConfig engine_config = ft8_engine_baseline_config();
    Ft8EngineRequirements req;
    Ft8Engine engine;
    RxFrontendConfig frontend_config = rx_frontend_baseline_config();
    RxFrontend frontend;
    RxSlotFramer framer;
    RxResultBuilderConfig builder_config = rx_result_builder_default_config();
    RxResultBuilder builder;
    AssemblySink sink;
    FILE *f = NULL;
    void *workspace = NULL;
    uint32_t source_samples = 0u;
    uint32_t engine_sample;
    int16_t transport[TRANSPORT_FRAMES * RX_FRONTEND_INPUT_CHANNELS];
    size_t transport_fill = 0u;
    int engine_initialized = 0;
    int frontend_initialized = 0;
    int framer_initialized = 0;
    int builder_initialized = 0;
    int rc = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <pinned-6khz-mono-s16.wav>\n", argv[0]);
        return 2;
    }

    if (open_6k_mono_s16(argv[1], &f, &source_samples) != 0) {
        fprintf(stderr, "cannot open pinned 6 kHz WAV: %s\n", argv[1]);
        return 1;
    }
    if (source_samples > RX_SLOT_FRAMER_SLOT_SAMPLES) {
        fprintf(stderr, "reference WAV exceeds one FT8 slot\n");
        goto cleanup;
    }

    if (ft8_engine_query_requirements(&engine_config, &req) != FT8_ENGINE_OK ||
        posix_memalign(&workspace, req.alignment, req.workspace_bytes) != 0 ||
        workspace == NULL)
        goto cleanup;
    if (ft8_engine_init(&engine, &engine_config, workspace, req.workspace_bytes) != FT8_ENGINE_OK)
        goto cleanup;
    engine_initialized = 1;

    if (rx_frontend_init(&frontend, &frontend_config) != RX_FRONTEND_OK)
        goto cleanup;
    frontend_initialized = 1;

    if (rx_slot_framer_init(&framer, 12345, 0u) != RX_SLOT_FRAMER_OK)
        goto cleanup;
    framer_initialized = 1;

    strcpy(builder_config.local_callsign, "AG6AQ");
    if (rx_result_builder_init(&builder, &builder_config) != RX_RESULT_OK)
        goto cleanup;
    builder_initialized = 1;

    memset(&sink, 0, sizeof(sink));
    sink.engine = &engine;
    sink.builder = &builder;

    /*
     * Synthesize the locked 12 kHz/S16/stereo transport from the pinned 6 kHz
     * reference. Each 6 kHz sample becomes two identical 12 kHz stereo frames.
     * The rest of the 15-second slot is zero padded. TRANSPORT_FRAMES is odd so
     * frontend decimation pairs are repeatedly split across transport chunks.
     */
    for (engine_sample = 0u; engine_sample < RX_SLOT_FRAMER_SLOT_SAMPLES; ++engine_sample) {
        int16_t sample = 0;
        unsigned repeat;

        if (engine_sample < source_samples) {
            uint16_t raw;
            if (read_u16(f, &raw) != 0)
                goto cleanup;
            sample = (int16_t)raw;
        }

        for (repeat = 0u; repeat < 2u; ++repeat) {
            transport[transport_fill * 2u] = sample;
            transport[transport_fill * 2u + 1u] = sample;
            ++transport_fill;
            if (transport_fill == TRANSPORT_FRAMES) {
                if (flush_transport(&frontend, &framer, &sink,
                                    transport, transport_fill) != 0)
                    goto cleanup;
                transport_fill = 0u;
            }
        }
    }

    if (transport_fill > 0u &&
        flush_transport(&frontend, &framer, &sink,
                        transport, transport_fill) != 0)
        goto cleanup;

    if (sink.begin_count != 1u || sink.block_count != 93u || sink.finalize_count != 1u)
        goto cleanup;
    if (sink.batch.slot_id != 12345 || sink.batch.message_count != 1u)
        goto cleanup;
    if (memcmp(sink.batch.messages[0].payload, kExpectedPayload, FT8_PAYLOAD_BYTES) != 0 ||
        sink.batch.messages[0].protocol_type != FT8_PROTOCOL_STANDARD ||
        sink.batch.messages[0].parse_status != FT8_PROTOCOL_PARSE_OK ||
        !sink.batch.messages[0].is_cq || sink.batch.messages[0].is_to_me ||
        strcmp(sink.batch.messages[0].canonical_text, "CQ W1XYZ FN42") != 0 ||
        strcmp(sink.batch.messages[0].call_de, "W1XYZ") != 0 ||
        strcmp(sink.batch.messages[0].extra, "FN42") != 0)
        goto cleanup;

    printf("RX5 slot=%lld blocks=%zu messages=%zu cq=%d to_me=%d text=\"%s\"\n",
           (long long)sink.batch.slot_id,
           sink.block_count,
           sink.batch.message_count,
           sink.batch.messages[0].is_cq ? 1 : 0,
           sink.batch.messages[0].is_to_me ? 1 : 0,
           sink.batch.messages[0].canonical_text);
    puts("rx5_pure_assembly_reference: PASS");
    rc = 0;

cleanup:
    if (builder_initialized)
        rx_result_builder_destroy(&builder);
    if (framer_initialized)
        rx_slot_framer_destroy(&framer);
    if (frontend_initialized)
        rx_frontend_destroy(&frontend);
    if (engine_initialized)
        ft8_engine_destroy(&engine);
    if (f != NULL)
        fclose(f);
    free(workspace);
    return rc;
}
