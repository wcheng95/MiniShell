#include "minicw_run.h"
#include "keyer_service.h"
#include "ui_service.h"
#include "tone_sim.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint64_t now_us;
static unsigned reads, sleeps, presents, opens, closes, launch, fail_open;
static unsigned levels[4], input_tip, input_ring;
static bool live[4], fail_read, fail_write, fail_present, fail_sleep;
static char frame[7][21];
static uint64_t clock_now(void) { return now_us; }
static mini_result_t sleep_ms(uint32_t ms)
{
    assert(ms == 10); ++sleeps; now_us += ms * 1000;
    return fail_sleep ? MINI_ERR_IO : MINI_OK;
}
static mini_result_t io_open(const mini_digital_io_config_t *c, mini_digital_io_t *handle)
{
    const unsigned ids[] = {13, 15, 3, 6};
    unsigned i = opens++;
    assert(i < 4 && c->line_id == ids[i] && c->initial_level == 1);
    assert(c->mode == (i < 2 ? MINI_DIGITAL_IO_MODE_INPUT_PULLUP : MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN));
    if (opens == fail_open) return MINI_ERR_ACCESS;
    live[i] = true; levels[i] = 1; *handle = i + 1; return MINI_OK;
}
static mini_result_t io_read(mini_digital_io_t h, uint32_t *level)
{
    assert(h >= 1 && h <= 2 && live[h - 1]);
    *level = h == 1 ? input_tip : input_ring;
    return fail_read ? MINI_ERR_IO : MINI_OK;
}
static mini_result_t io_write(mini_digital_io_t h, uint32_t level)
{
    assert(h >= 3 && h <= 4 && live[h - 1]); levels[h - 1] = level;
    return fail_write ? MINI_ERR_IO : MINI_OK;
}
static mini_result_t io_close(mini_digital_io_t h)
{
    assert(h && h <= 4 && live[h - 1]);
    if (h >= 3) assert(levels[h - 1] == 1);
    live[h - 1] = false; ++closes; return MINI_OK;
}
static mini_result_t info(mini_text_display_info_t *i) { i->columns = 20; i->rows = 7; return MINI_OK; }
static mini_result_t clear(void) { memset(frame, 0, sizeof(frame)); return MINI_OK; }
static mini_result_t write_at(uint32_t row, uint32_t column, const char *text, uint32_t size)
{
    assert(row < 7 && column == 0 && size == 20);
    memcpy(frame[row], text, size); frame[row][20] = 0; return MINI_OK;
}
static mini_result_t present(void) { ++presents; return fail_present ? MINI_ERR_IO : MINI_OK; }
static mini_result_t ch(mini_key_event_t *key, char c)
{
    key->type = MINI_KEY_EVENT_CHAR; key->codepoint = (unsigned char)c; return MINI_OK;
}
static mini_result_t special(mini_key_event_t *key, uint32_t code)
{
    key->type = MINI_KEY_EVENT_SPECIAL; key->key = code; return MINI_OK;
}
static mini_result_t input(mini_key_event_t *key, uint32_t timeout)
{
    assert(timeout == MINI_WAIT_NONE && reads < 200);
    unsigned step = reads++;
    if (step == 0) {
        assert(strcmp(frame[0], "--:-- PDN SKN 19 V80") == 0);
        assert(frame[1][0] == ' ' && frame[6][0] == ' ');
        assert(!keyer_service_get_mute() && !keyer_service_get_tune_active());
    }
    if (launch == 2) {
        if (step == 0) {
            keyer_service_set_message(0, "E");
            keyer_service_set_repeat_interval_s(1);
            return special(key, MINI_KEY_ALT);
        }
        if (step == 1) return ch(key, '1');
        if (step == 2) assert(keyer_service_is_tx_active());
        if (step == 30) assert(!keyer_service_is_tx_active() && !keyer_service_tx_has_text());
        if (step == 110) {
            assert(keyer_service_is_tx_active());
            input_tip = 0;
        }
        if (step == 112) {
            assert(!keyer_service_is_tx_active() && !keyer_service_tx_has_text());
            assert(levels[2] == 1 && levels[3] == 1);
            return ch(key, 3);
        }
        return MINI_ERR_NOT_READY;
    }
    if (launch != 0) {
        if (step == 0) {
            keyer_service_set_key_out_mode(KEYER_KEY_OUT_SK_M);
            keyer_service_set_tx_delay_s(2);
            return ch(key, 'E');
        }
        assert(!keyer_service_is_tx_active());
        assert(levels[2] == 1 && levels[3] == 0);
        return ch(key, 3); /* ASCII Ctrl+C also exits pending TX */
    }
    switch (step) {
    case 1: input_tip = 0; break;
    case 4: input_tip = 1; break;
    case 30:
        assert(frame[1][0] == 'E');
        return special(key, MINI_KEY_ALT);
    case 31:
        assert(strncmp(frame[1], "M1:CQ POTA", 10) == 0);
        return ch(key, '1');
    case 32:
        assert(keyer_service_is_tx_active());
        assert(strncmp(frame[6], "CQ POTA", 7) == 0);
        return special(key, MINI_KEY_ALT);
    case 33:
        assert(frame[1][0] == 'E');
        return ch(key, '`');
    case 34:
        assert(!keyer_service_tx_has_text());
        return special(key, MINI_KEY_OPT);
    case 35:
        assert(strncmp(frame[3], "3 Wpm:19", 8) == 0);
        return ch(key, '3');
    case 36: return ch(key, '2');
    case 37: return ch(key, '5');
    case 38: return special(key, MINI_KEY_ENTER);
    case 39:
        assert(keyer_service_get_key_in_wpm() == 25);
        return special(key, MINI_KEY_DOWN); /* reference menu '.' next page */
    case 40:
        assert(strncmp(frame[1], "1 M1:CQ POTA", 12) == 0);
        return ch(key, '2');
    case 41: return ch(key, 'h');
    case 42: return ch(key, 'i');
    case 43: return special(key, MINI_KEY_ENTER);
    case 44:
        assert(strcmp(keyer_service_get_message(1), "HI") == 0);
        return special(key, MINI_KEY_OPT);
    case 45: return ch(key, '\\');
    case 46:
        assert(keyer_service_get_mute());
        assert(strncmp(frame[6], "Mute:ON", 7) == 0);
        return special(key, MINI_KEY_TAB);
    case 47:
        assert(strcmp(frame[0], "--:-- PDN SKN 25 V80") == 0);
        assert(strncmp(frame[6], "Tune", 4) == 0);
        return ch(key, 'T');
    case 48:
        assert(keyer_service_get_tune_latched());
        assert(levels[2] == 0 && levels[3] == 0);
        key->modifiers = MINI_MOD_CTRL;
        return ch(key, 'c');
    default: break;
    }
    return MINI_ERR_NOT_READY;
}
static const mini_time_location_api_t time_api = {
    .struct_size = sizeof(time_api), .monotonic_us = clock_now, .sleep_ms = sleep_ms
};
static const mini_digital_io_api_t io_api = {
    .struct_size = sizeof(io_api), .capabilities = MINI_DIGITAL_IO_CAP_INPUT_PULLUP | MINI_DIGITAL_IO_CAP_OUTPUT_OPEN_DRAIN,
    .open = io_open, .read = io_read, .write = io_write, .close = io_close
};
static const mini_text_display_api_t text_api = {
    .struct_size = sizeof(text_api), .get_info = info, .clear = clear, .write_at = write_at
};
static const mini_display_api_t display_api = {
    .struct_size = sizeof(display_api), .capabilities = MINI_DISPLAY_CAP_TEXT, .text = &text_api, .present = present
};
static const mini_key_input_api_t key_api = {.struct_size = sizeof(key_api), .read = input};
static const mini_input_api_t input_api = {.struct_size = sizeof(input_api), .capabilities = MINI_INPUT_CAP_KEY, .key = &key_api};
#include "minicw_fs_fake.h"
#include "minicw_memory_fake.h"
static mini_api_t api = {
    .api_version = MINISHELL_API_VERSION, .struct_size = sizeof(api), .time_location = &time_api,
    .digital_io = &io_api, .display = &display_api, .input = &input_api, .fs = &fs_api, .memory = &memory_api
};
static void reset(void)
{
    fs_reset(); memory_reset();
    reads = sleeps = opens = closes = presents = 0; now_us = 1000000;
    input_tip = input_ring = 1;
    for (unsigned i = 0; i < 4; ++i) assert(!live[i]);
}
static uint64_t sim_clock(void *ctx) { (void)ctx; return now_us; }
int main(void)
{
    assert(minicw_run(NULL) != 0);
    for (launch = 0; launch < 3; ++launch) {
        reset(); assert(minicw_run(&api) == 0);
        assert(opens == 4 && closes == 4 && presents > 0 && sleeps > 0);
    }
    /* Repeat the accepted UI, paddle, Tune, delayed TX and M1 scenarios with
     * the real generic tone wrapper and actual renderer's committed busy state. */
    minishell_services_port_t port = {0}; port.monotonic_us = sim_clock;
    tone_sim_configure(&port);
    mini_audio_api_t audio = {.struct_size = sizeof(audio), .capabilities = MINI_AUDIO_CAP_TONE, .tone = port.audio_tone};
    api.audio = &audio;
    for (launch = 0; launch < 3; ++launch) {
        reset(); assert(minicw_run(&api) == 0); assert(closes == 4);
    }
    /* A pre-extension object must not expose the tail, even if memory beyond it
     * happens to contain a valid tone table. */
    audio.struct_size = offsetof(mini_audio_api_t, tone);
    launch = 1; reset(); assert(minicw_run(&api) == 0);
    api.audio = NULL;
    for (fail_open = 1; fail_open <= 4; ++fail_open) {
        reset(); assert(minicw_run(&api) != 0); assert(closes == fail_open - 1);
    }
    fail_open = 0;
    bool *failures[] = {&fail_read, &fail_write, &fail_present, &fail_sleep};
    for (unsigned i = 0; i < 4; ++i) {
        *failures[i] = true; reset(); assert(minicw_run(&api) != 0);
        assert(closes == 4); *failures[i] = false;
    }
    puts("minicw runtime: PASS (MiniShell timing/UI/history/menus/macros/Tune/Ctrl+C/relaunch/failure release)");
    return 0;
}
