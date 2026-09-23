/* Host-only arithmetic/read-phase checks and instrumented utility invocation. */
#include "js8_monitor.h"
#include "js8_channel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned workspace_allocations, dictionary_opens, stream_resets;
static void *tracked_alloc(size_t alignment, size_t bytes)
{
    ++workspace_allocations;
    return aligned_alloc(alignment, bytes);
}
static FILE *tracked_open(const char *path, const char *mode)
{
    const char *override = getenv("JS8_JSC_DICT");
    if (!strcmp(path, override ? override : JS8_JSC_DEFAULT_DICT)) ++dictionary_opens;
    return fopen(path, mode);
}
static void tracked_reset(Js8Monitor *monitor)
{
    ++stream_resets;
    js8_monitor_reset_stream(monitor);
}
#define main js8_decode_entry
#define aligned_alloc tracked_alloc
#define fopen tracked_open
#define js8_monitor_reset_stream tracked_reset
#include "../apps/js8chat/tools/js8_decode.c"
#undef js8_monitor_reset_stream
#undef fopen
#undef aligned_alloc
#undef main

static int sample_at(uint32_t index)
{
    return (int)(index % 32749) - 16374;
}
int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--tones")) {
        uint8_t bits[75], tones[79];
        assert(strlen(argv[2]) == 75);
        for (unsigned i = 0; i < 75; ++i) {
            assert(argv[2][i] == '0' || argv[2][i] == '1');
            bits[i] = (uint8_t)(argv[2][i]-'0');
        }
        assert(js8_channel_encode(bits, tones) == 0);
        for (unsigned i = 0; i < 79; ++i) printf("%u", tones[i]);
        putchar('\n');
        return 0;
    }
    if (argc > 1) {
        int result = js8_decode_entry(argc, argv);
        fprintf(stderr, "probe workspace_allocations=%u dictionary_opens=%u stream_resets=%u\n",
                workspace_allocations, dictionary_opens, stream_resets);
        return result;
    }
    Js8MonitorRequirements req = {0};
    Js8MonitorConfig cfg = js8_monitor_baseline_config();
    Js8Candidate candidate = {0};
    req.min_bin = 32; candidate.freq_offset = 79; candidate.freq_sub = 1;
    assert(candidate_millihz(&req, &cfg, &candidate) == 696875);
    candidate.freq_offset = -1;
    assert(candidate_millihz(&req, &cfg, &candidate) == 196875);
    req.min_bin = 0;
    assert(candidate_millihz(&req, &cfg, &candidate) == -3125);
    assert(full_slots(179999) == 0 && full_slots(180000) == 1);
    assert(full_slots(720000) == 4 && full_slots(1440000) == 8);
    assert(full_slots(1441234) == 8 && 1441234 % HOST_SLOT_INPUT_SAMPLES == 1234);
    assert(HOST_WINDOW_INPUT_SAMPLES == 178560 && HOST_SLOT_INPUT_SAMPLES == 180000);
    FILE *file = tmpfile(); assert(file);
    uint8_t prefix[37] = {0}; assert(fwrite(prefix, 1, sizeof(prefix), file) == sizeof(prefix));
    /* Deterministic sample-index values, no tones or FFT; exercise actual
     * seek_slot/read_block against the globally continuous phase-0 sequence.
     */
    for (uint32_t i = 0; i < 1440000; i += 1000) {
        uint8_t bytes[2000];
        for (unsigned n = 0; n < 1000; ++n) {
            uint16_t sample = (uint16_t)sample_at(i+n);
            bytes[2*n] = (uint8_t)sample; bytes[2*n+1] = (uint8_t)(sample >> 8);
        }
        assert(fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    }
    HostWav wav = {file, 1440000, 1440000, 37};
    for (uint32_t slot = 0; slot < 8; ++slot) {
        assert(slot_input_start(slot) == (uint64_t)slot*180000);
        assert(seek_slot(&wav, slot) == 0);
        assert(ftell(file) == (long)(37 + slot*360000));
        for (unsigned block = 0; block < 93; ++block) {
            float samples[960];
            assert(read_block(&wav, samples) == 0);
            for (unsigned n = 0; n < 960; ++n) {
                uint32_t continuous_kept = 2 * (slot*90000 + block*960 + n);
                assert(samples[n] == (float)sample_at(continuous_kept)/32768.0f);
            }
        }
        assert(ftell(file) == (long)(37 + slot*360000 + 357120));
        assert(wav.samples == 1440000 - slot*180000 - 178560);
    }
    assert(seek_slot(&wav, 8) == -1);
    fclose(file);
    puts("js8_multislot_host_test: 4/8 slots, exact starts and continuous sample phase: PASS");
    return 0;
}
