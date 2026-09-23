/* Test composition only: real MiniShell Audio/FS/Serial and Linux worker, with
 * deterministic per-read UTC. Pacing is test-only, never app timing policy. */
#include "js8_live.h"
#include <string.h>
static const mini_api_t *base_api;
static mini_api_t test_api;
static mini_audio_api_t test_audio;
static mini_audio_rx_api_t test_rx;
static mini_time_location_api_t test_time;
static uint64_t input_frames;
static mini_result_t timed_read(mini_audio_stream_t stream, void *frames, uint32_t cap,
                                uint32_t *got, uint32_t timeout)
{
    mini_result_t result=base_api->audio->rx->read(stream,frames,cap,got,timeout);
    if (!result) {
        input_frames+=*got;
        base_api->time_location->sleep_ms(1);
    }
    return result;
}
static mini_result_t timed_utc(mini_utc_time_t *utc)
{
    uint64_t samples=input_frames/2;
    utc->unix_seconds=15000+(int64_t)(samples/6000);
    utc->nanoseconds=(uint32_t)((samples%6000)*1000000000/6000+1);
    return MINI_OK;
}
static const mini_api_t *probe_api_get(void)
{
    base_api=mini_api_get();test_api=*base_api;
    test_audio=*base_api->audio;test_rx=*base_api->audio->rx;test_time=*base_api->time_location;
    test_rx.read=timed_read;test_audio.rx=&test_rx;test_api.audio=&test_audio;
    test_time.utc_get=timed_utc;test_api.time_location=&test_time;
    /* Console script input belongs to the shell, not the test app's quit key. */
    test_api.input=NULL;input_frames=0;
    return &test_api;
}
#define mini_api_get probe_api_get
#include "../platform/linux/linux_js8chat.c"
#undef mini_api_get
