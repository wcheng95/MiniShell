#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "minishell/api.h"
#include "ft8_engine.h"
#include "rx_audio_adapter.h"
#include "rx_frontend.h"
#include "rx_result_builder.h"
#include "rx_slot_framer.h"

#define TRANSPORT_FRAMES 257u
#define FRONTEND_OUT_CAPACITY ((TRANSPORT_FRAMES + 1u) / 2u)
#define SLOT_ID 12345

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
} ProbeSink;

static void write_line(const mini_api_t *api, const char *text)
{
    if (api->console != NULL && api->console->write != NULL)
        api->console->write(text);
    else if (api->system != NULL && api->system->write != NULL)
        api->system->write(text);
}

static int emit_event(void *ctx, const RxSlotFramerEvent *event)
{
    ProbeSink *sink = (ProbeSink *)ctx;

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
        Ft8EngineStatus engine_status;
        RxResultStatus result_status;

        ++sink->finalize_count;
        engine_status = ft8_engine_finalize_window(sink->engine,
                                                   sink->protocol_messages,
                                                   FT8_DECODER_CANDIDATE_CAPACITY,
                                                   &slot);
        if (engine_status != FT8_ENGINE_OK && engine_status != FT8_ENGINE_NO_MESSAGES)
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

static int feed_transport(RxFrontend *frontend,
                          RxSlotFramer *framer,
                          ProbeSink *sink,
                          const int16_t *frames,
                          size_t frame_count)
{
    float out[FRONTEND_OUT_CAPACITY];
    size_t out_count = 0u;

    if (rx_frontend_process(frontend, frames, frame_count,
                            out, FRONTEND_OUT_CAPACITY, &out_count) != RX_FRONTEND_OK)
        return -1;

    if (out_count > 0u &&
        rx_slot_framer_process(framer, out, out_count,
                               emit_event, sink) != RX_SLOT_FRAMER_OK)
        return -1;
    return 0;
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    Ft8EngineConfig engine_config = ft8_engine_baseline_config();
    Ft8EngineRequirements req;
    Ft8Engine engine;
    RxFrontendConfig frontend_config = rx_frontend_baseline_config();
    RxFrontend frontend;
    RxSlotFramer framer;
    RxResultBuilderConfig builder_config = rx_result_builder_default_config();
    RxResultBuilder builder;
    RxAudioAdapter audio;
    ProbeSink sink;
    void *allocation = NULL;
    void *workspace = NULL;
    uint32_t allocation_bytes;
    int16_t frames[TRANSPORT_FRAMES * RX_AUDIO_ADAPTER_CHANNELS];
    size_t total_frames = 0u;
    int engine_initialized = 0;
    int frontend_initialized = 0;
    int framer_initialized = 0;
    int builder_initialized = 0;
    int audio_initialized = 0;
    int rc = 1;

    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->memory == NULL || api->memory->alloc == NULL || api->memory->free == NULL ||
        api->audio == NULL) {
        return 2;
    }
    if (argc != 2) {
        write_line(api, "usage: ft8_rx_probe <rx-endpoint>\n");
        return 3;
    }

    if (ft8_engine_query_requirements(&engine_config, &req) != FT8_ENGINE_OK ||
        req.workspace_bytes > UINT32_MAX || req.alignment == 0u ||
        req.alignment - 1u > UINT32_MAX - req.workspace_bytes) {
        goto cleanup;
    }
    allocation_bytes = (uint32_t)(req.workspace_bytes + req.alignment - 1u);
    if (api->memory->alloc(allocation_bytes, &allocation) != MINI_OK || allocation == NULL)
        goto cleanup;
    {
        uintptr_t p = (uintptr_t)allocation;
        uintptr_t aligned = (p + req.alignment - 1u) & ~((uintptr_t)req.alignment - 1u);
        workspace = (void *)aligned;
    }

    if (ft8_engine_init(&engine, &engine_config, workspace, req.workspace_bytes) != FT8_ENGINE_OK)
        goto cleanup;
    engine_initialized = 1;
    if (rx_frontend_init(&frontend, &frontend_config) != RX_FRONTEND_OK)
        goto cleanup;
    frontend_initialized = 1;
    if (rx_slot_framer_init(&framer, SLOT_ID, 0u) != RX_SLOT_FRAMER_OK)
        goto cleanup;
    framer_initialized = 1;

    strcpy(builder_config.local_callsign, "AG6AQ");
    if (rx_result_builder_init(&builder, &builder_config) != RX_RESULT_OK)
        goto cleanup;
    builder_initialized = 1;

    memset(&sink, 0, sizeof(sink));
    sink.engine = &engine;
    sink.builder = &builder;

    if (rx_audio_adapter_init(&audio, api->audio) != RX_AUDIO_ADAPTER_OK)
        goto cleanup;
    audio_initialized = 1;
    if (rx_audio_adapter_open(&audio, argv[1]) != RX_AUDIO_ADAPTER_OK ||
        rx_audio_adapter_start(&audio) != RX_AUDIO_ADAPTER_OK)
        goto cleanup;

    for (;;) {
        size_t got = 0u;
        RxAudioAdapterStatus status = rx_audio_adapter_read(
            &audio, frames, TRANSPORT_FRAMES, &got, MINI_WAIT_NONE);

        if (status == RX_AUDIO_ADAPTER_END_OF_STREAM)
            break;
        if (status != RX_AUDIO_ADAPTER_OK || got == 0u)
            goto cleanup;
        if (feed_transport(&frontend, &framer, &sink, frames, got) != 0)
            goto cleanup;
        total_frames += got;
    }

    if (rx_audio_adapter_close(&audio) != RX_AUDIO_ADAPTER_OK)
        goto cleanup;

    if (total_frames != (size_t)RX_SLOT_FRAMER_SLOT_SAMPLES * 2u ||
        sink.begin_count != 1u || sink.block_count != 93u || sink.finalize_count != 1u ||
        sink.batch.slot_id != SLOT_ID || sink.batch.message_count != 1u)
        goto cleanup;

    if (memcmp(sink.batch.messages[0].payload, kExpectedPayload, FT8_PAYLOAD_BYTES) != 0 ||
        sink.batch.messages[0].protocol_type != FT8_PROTOCOL_STANDARD ||
        !sink.batch.messages[0].is_cq || sink.batch.messages[0].is_to_me ||
        strcmp(sink.batch.messages[0].canonical_text, "CQ W1XYZ FN42") != 0)
        goto cleanup;

    {
        char line[160];
        (void)snprintf(line, sizeof(line),
                       "RX6 frames=%zu slot=%lld blocks=%zu messages=%zu text=\"%s\"\n",
                       total_frames, (long long)sink.batch.slot_id,
                       sink.block_count, sink.batch.message_count,
                       sink.batch.messages[0].canonical_text);
        write_line(api, line);
        write_line(api, "ft8_rx_probe: PASS\n");
    }
    rc = 0;

cleanup:
    if (audio_initialized && audio.open)
        (void)rx_audio_adapter_close(&audio);
    if (builder_initialized)
        rx_result_builder_destroy(&builder);
    if (framer_initialized)
        rx_slot_framer_destroy(&framer);
    if (frontend_initialized)
        rx_frontend_destroy(&frontend);
    if (engine_initialized)
        ft8_engine_destroy(&engine);
    if (allocation != NULL)
        (void)api->memory->free(allocation);
    if (rc != 0)
        write_line(api, "ft8_rx_probe: FAIL\n");
    return rc;
}
