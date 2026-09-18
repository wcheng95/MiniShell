#include <assert.h>
#include <stdio.h>
#include <string.h>
#define main probe_main
#include "main/audio_tx_probe.c"
#undef main

static char output[8192];
static uint64_t clock_us;
static unsigned writes, opens, starts, stops, aborts, closes;
static bool fatal;
static void console_write(const char *text)
{ assert(strlen(output) + strlen(text) < sizeof(output)); strcat(output, text); }
static uint64_t monotonic(void) { return clock_us; }
static mini_result_t open_tx(const char *endpoint, const mini_audio_format_t *format, mini_audio_stream_t *out)
{
    assert(strcmp(endpoint, "speaker") == 0 && format->sample_rate_hz == 48000);
    assert(format->channels == 1 && format->sample_format == MINI_AUDIO_SAMPLE_S16);
    ++opens; writes = 0; *out = 1; return MINI_OK;
}
static mini_result_t start_tx(mini_audio_stream_t stream) { assert(stream == 1); ++starts; return MINI_OK; }
static mini_result_t stop_tx(mini_audio_stream_t stream) { assert(stream == 1); ++stops; return MINI_OK; }
static mini_result_t abort_tx(mini_audio_stream_t stream) { assert(stream == 1); ++aborts; return MINI_OK; }
static mini_result_t close_tx(mini_audio_stream_t stream) { assert(stream == 1); ++closes; return MINI_OK; }
static mini_result_t write_tx(mini_audio_stream_t stream, const void *frames, uint32_t count,
                              uint32_t *accepted, uint32_t timeout)
{
    assert(stream == 1 && count == 48);
    assert(timeout == (opens == 1 ? 20u : MINI_WAIT_NONE));
    for (unsigned i = 0; i < count; ++i) assert(((const int16_t *)frames)[i] == 0);
    static const uint32_t times[] = {1000, 2001, 6000, 10001, 20001};
    static const uint32_t progress[] = {48, 17, 0, 0, 48};
    unsigned index = writes++ % 5;
    clock_us += times[index];
    *accepted = progress[index];
    if (fatal && writes > WARMUP_CALLS) { *accepted = 0; return MINI_ERR_IO; }
    return index == 2 ? MINI_ERR_TIMEOUT : MINI_OK;
}
const mini_api_t *mini_api_get(void)
{
    static const mini_console_api_t console = {.write = console_write};
    static const mini_time_location_api_t time = {.monotonic_us = monotonic};
    static const mini_audio_tx_api_t tx = {.open = open_tx, .start = start_tx, .write = write_tx,
                                          .stop = stop_tx, .abort = abort_tx, .close = close_tx};
    static const mini_audio_api_t audio = {.capabilities = MINI_AUDIO_CAP_TX, .tx = &tx};
    static const mini_api_t api = {.api_version = MINISHELL_API_VERSION, .console = &console,
                                   .time_location = &time, .audio = &audio};
    return &api;
}
int main(void)
{
    assert(probe_main(0, NULL) == 0);
    const char *expected = "calls=5000\nframes=113000\nOK=4000\nTIMEOUT=1000\nother=0\n"
        "partial=1000\nzero_OK=1000\nmin_us=1000\nmean_us=7800\nmax_us=20001\n"
        ">1000us=4000\n>2000us=4000\n>5000us=3000\n>10000us=2000\n>20000us=1000\n";
    char *first = strstr(output, expected);
    assert(first && strstr(first + strlen(expected), expected));
    assert(opens == 2 && starts == 2 && stops == 2 && closes == 2 && aborts == 0);
    fatal = true;
    opens = starts = stops = closes = aborts = 0;
    output[0] = '\0';
    assert(probe_main(0, NULL) == 1);
    assert(opens == 1 && closes == 1 && aborts == 1 && stops == 0);
    assert(strstr(output, "calls=1\nframes=0\nOK=0\nTIMEOUT=0\nother=1\n"));
    puts("probe host statistics/cleanup: PASS");
    return 0;
}
