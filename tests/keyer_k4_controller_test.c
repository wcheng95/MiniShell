#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_controller.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static bool s_opened[64];
static uint32_t s_mode[64];
static uint32_t s_level[64];
static uint64_t s_now_us;
static bool s_q_sent;
static unsigned scenario, input_index;
static bool display_diagnostic, manual_render;
static unsigned render_calls, present_calls, diagnostic_read;
static bool delay_render, idle_repeat_render, catchup_render;
static uint64_t last_render;
static const char diagnostic_config[] = "m1=I E\nrepeat_s=1\n";
static const mini_key_event_t *r2_events;
static unsigned r2_count;
static uint64_t final_release;
static char saved[1024], temporary[1024];
static unsigned write_position;
static bool fail_save;
static unsigned save_count;
static bool mute_on_seen, mute_off_seen, unsupported_seen;
static bool overlay_seen;
static bool s_saw_key_down;
static char s_console[1024];
static size_t s_console_len;

static void fake_console_write(const char *text)
{
    size_t len;
    if (text == NULL) return;
    len = strlen(text);
    if (len > sizeof(s_console) - 1u - s_console_len) {
        len = sizeof(s_console) - 1u - s_console_len;
    }
    memcpy(&s_console[s_console_len], text, len);
    s_console_len += len;
    s_console[s_console_len] = '\0';
}

static mini_result_t fake_fs_open(const char *path, uint32_t flags, mini_file_t *out_file)
{
    if (out_file != NULL) *out_file = MINI_FILE_INVALID;
    if (flags & MINI_FS_WRITE) {
        CHECK(!strcmp(path, "/flash/keyer/setting.tmp"));
        *out_file = 1; write_position = 0; temporary[0] = 0; return MINI_OK;
    }
    if (display_diagnostic) { *out_file = 2; diagnostic_read = 0; return MINI_OK; }
    return MINI_ERR_NOT_FOUND;
}

static mini_result_t fake_fs_read(mini_file_t file, void *buffer, uint32_t size,
                                  uint32_t *out_read)
{
    if (display_diagnostic && file == 2) {
        unsigned remaining = (unsigned)strlen(diagnostic_config) - diagnostic_read;
        if (size > remaining) size = remaining;
        memcpy(buffer, diagnostic_config + diagnostic_read, size);
        diagnostic_read += size; *out_read = size; return MINI_OK;
    }
    (void)file;
    (void)buffer;
    (void)size;
    if (out_read != NULL) *out_read = 0u;
    return MINI_ERR_BAD_HANDLE;
}

static mini_result_t fake_fs_close(mini_file_t file)
{
    (void)file;
    return MINI_OK;
}

static uint64_t fake_monotonic_us(void)
{
    return s_now_us;
}

static mini_result_t fake_sleep_ms(uint32_t milliseconds)
{
    s_now_us += (uint64_t)milliseconds * 1000u;
    return MINI_OK;
}

static mini_result_t fake_key_read(mini_key_event_t *out_event, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (out_event == NULL) return MINI_ERR_INVALID;
    if (display_diagnostic) {
        if (!input_index && s_now_us >= 2000) {
            ++input_index; out_event->type = MINI_KEY_EVENT_CHAR;
            out_event->codepoint = '1'; out_event->modifiers = MINI_MOD_ALT; return MINI_OK;
        }
        if (s_now_us >= 2100000) {
            out_event->type = MINI_KEY_EVENT_CHAR; out_event->codepoint = 'c';
            out_event->modifiers = MINI_MOD_CTRL; return MINI_OK;
        }
        return MINI_ERR_NOT_READY;
    }
    if (scenario >= 8) {
        if (input_index < r2_count && s_now_us >= (uint64_t)input_index * (scenario >= 10 ? 100000u : 10000u)) {
            *out_event = r2_events[input_index++]; return MINI_OK;
        }
        if (s_now_us < (scenario == 11 ? 450000u : 300000u)) return MINI_ERR_NOT_READY;
        out_event->type = MINI_KEY_EVENT_CHAR; out_event->codepoint = 'c';
        out_event->modifiers = MINI_MOD_CTRL; return MINI_OK;
    }
    if (scenario >= 5) {
        static const mini_key_event_t choice_events[] = {
            {.type = MINI_KEY_EVENT_SPECIAL, .key = MINI_KEY_OPT},
            {.type = MINI_KEY_EVENT_CHAR, .codepoint = '6'},
            {.type = MINI_KEY_EVENT_SPECIAL, .key = MINI_KEY_RIGHT, .modifiers = MINI_MOD_FN},
            {.type = MINI_KEY_EVENT_SPECIAL, .key = MINI_KEY_ENTER},
            {.type = MINI_KEY_EVENT_SPECIAL, .key = MINI_KEY_OPT},
        };
        unsigned count = scenario == 7 ? 5 : scenario == 6 ? 2 : 1;
        if (input_index < count) {
            if (scenario == 7) *out_event = choice_events[input_index];
            else {
                if (input_index == 1) CHECK(strstr(saved, "mute=On\n") != NULL && save_count == 1);
                out_event->type = MINI_KEY_EVENT_CHAR; out_event->codepoint = '\\';
            }
            ++input_index; return MINI_OK;
        }
        if (s_now_us < 300000) return MINI_ERR_NOT_READY;
        out_event->type = MINI_KEY_EVENT_CHAR; out_event->codepoint = 'c';
        out_event->modifiers = MINI_MOD_CTRL; return MINI_OK;
    }
    if (scenario && input_index == 0) {
        ++input_index;
        out_event->type = (scenario == 1 || scenario == 3) ? MINI_KEY_EVENT_SPECIAL : MINI_KEY_EVENT_CHAR;
        out_event->key = MINI_KEY_TAB; out_event->codepoint = scenario == 4 ? 'T' : ']'; out_event->modifiers = 0;
        return MINI_OK;
    }
    if (scenario == 4 && input_index == 1 && s_now_us >= 1000) {
        ++input_index; out_event->type = MINI_KEY_EVENT_SPECIAL;
        out_event->key = MINI_KEY_ENTER; out_event->modifiers = 0; return MINI_OK;
    }
    if (scenario && input_index == (scenario == 4 ? 2u : 1u) && s_now_us >= 180000) {
        ++input_index;
        out_event->type = MINI_KEY_EVENT_CHAR; out_event->codepoint = 'q'; out_event->modifiers = 0;
        return MINI_OK;
    }
    if (!s_q_sent && s_now_us >= (scenario == 1 ? 400000u : 300000u)) {
        out_event->struct_size = sizeof(*out_event);
        out_event->type = MINI_KEY_EVENT_CHAR;
        out_event->codepoint = (uint32_t)'c';
        out_event->key = 0u;
        out_event->modifiers = MINI_MOD_CTRL;
        s_q_sent = true;
        return MINI_OK;
    }
    return MINI_ERR_NOT_READY;
}

static mini_result_t fake_dio_open(const mini_digital_io_config_t *config,
                                   mini_digital_io_t *out_line)
{
    if (config == NULL || out_line == NULL || config->line_id >= 64u ||
        config->initial_level > 1u) return MINI_ERR_INVALID;
    if (s_opened[config->line_id]) return MINI_ERR_EXISTS;

    s_opened[config->line_id] = true;
    s_mode[config->line_id] = config->mode;
    s_level[config->line_id] = config->initial_level;
    *out_line = config->line_id + 1u;
    return MINI_OK;
}

static mini_result_t fake_dio_read(mini_digital_io_t line, uint32_t *out_level)
{
    uint32_t id;
    if (line == MINI_DIGITAL_IO_INVALID || out_level == NULL) return MINI_ERR_INVALID;
    id = line - 1u;
    if (id >= 64u || !s_opened[id]) return MINI_ERR_BAD_HANDLE;

    if (display_diagnostic) { *out_level = 1; return MINI_OK; }
    if (id == 13u) {
        if (scenario == 11) *out_level = s_now_us >= 150000 && s_now_us < 160000 ? 0u : 1u;
        else if (scenario == 3 || scenario == 9) *out_level = 1;
        else if (scenario == 4) *out_level = s_now_us >= 10000 && s_now_us < 20000 ? 0u : 1u;
        else *out_level = scenario == 1 ? (s_now_us >= 100000 && s_now_us < 110000 ? 0u : 1u) : (s_now_us < 10000u ? 0u : 1u);
    } else if (id == 15u) {
        *out_level = 1u;
    } else {
        *out_level = s_level[id];
    }
    return MINI_OK;
}

static mini_result_t fake_dio_write(mini_digital_io_t line, uint32_t level)
{
    uint32_t id;
    if (line == MINI_DIGITAL_IO_INVALID || level > 1u) return MINI_ERR_INVALID;
    id = line - 1u;
    if (id >= 64u || !s_opened[id]) return MINI_ERR_BAD_HANDLE;
    if (id == 3 && s_level[id] == 0 && level == 1) final_release = s_now_us;
    s_level[id] = level;
    if ((id == 3u || id == 6u) && level == 0u) s_saw_key_down = true;
    return MINI_OK;
}

static mini_result_t fake_dio_close(mini_digital_io_t line)
{
    uint32_t id;
    if (line == MINI_DIGITAL_IO_INVALID) return MINI_ERR_INVALID;
    id = line - 1u;
    if (id >= 64u || !s_opened[id]) return MINI_ERR_BAD_HANDLE;
    s_opened[id] = false;
    return MINI_OK;
}

static const mini_console_api_t CONSOLE = {
    .struct_size = sizeof(mini_console_api_t),
    .write = fake_console_write,
};

static mini_result_t fake_fs_write(mini_file_t f, const void *b, uint32_t n, uint32_t *w)
{
    CHECK(f == 1 && write_position + n < sizeof(temporary));
    memcpy(temporary + write_position, b, n); write_position += n;
    temporary[write_position] = 0; *w = n; return MINI_OK;
}
static mini_result_t fake_fs_sync(mini_file_t f) { CHECK(f == 1); return MINI_OK; }
static mini_result_t fake_fs_mkdir(const char *p) { (void)p; return MINI_ERR_EXISTS; }
static mini_result_t fake_fs_remove(const char *p) { (void)p; return MINI_OK; }
static mini_result_t fake_fs_rename(const char *a, const char *b)
{
    (void)a; CHECK(!strcmp(b, "/flash/keyer/setting.txt"));
    if (fail_save) return MINI_ERR_IO;
    strcpy(saved, temporary); ++save_count; return MINI_OK;
}

static const mini_fs_api_t FS = {
    .struct_size = sizeof(mini_fs_api_t),
    .open = fake_fs_open,
    .close = fake_fs_close,
    .read = fake_fs_read,
    .write = fake_fs_write, .sync = fake_fs_sync, .mkdir = fake_fs_mkdir,
    .rename = fake_fs_rename, .remove_file = fake_fs_remove,
};

/* At 20 WPM, M1="I E" selected at 2 ms starts after TxDelay at 1002 ms:
 * element 1002-1062, element-gap 1062-1122, element 1122-1182,
 * word-gap 1182-1602, element 1602-1662, char-gap 1662-1842 ms.
 * Repeat wait is idle from 1842 ms until 2662 ms. */
static void check_display_window(void)
{
    CHECK(s_now_us < 1002000 || s_now_us >= 1842000);
}
static mini_result_t fake_utc_get(mini_utc_time_t *utc)
{
    if (!display_diagnostic) {
        if (scenario == 0 && s_now_us == 0 && s_level[3] == 0) manual_render = true;
        return MINI_ERR_NOT_READY;
    }
    check_display_window();
    ++render_calls; last_render = s_now_us;
    if (s_now_us >= 2000 && s_now_us < 1002000) delay_render = true;
    if (s_now_us == 1842000) catchup_render = true;
    if (s_now_us > 1842000) idle_repeat_render = true;
    /* Change the header each render so the adapter must also present. */
    utc->unix_seconds = (int64_t)(s_now_us / 1000 % 1440) * 60;
    return MINI_OK;
}

static const mini_time_location_api_t TIME_LOCATION = {
    .struct_size = sizeof(mini_time_location_api_t),
    .monotonic_us = fake_monotonic_us,
    .utc_get = fake_utc_get,
    .sleep_ms = fake_sleep_ms,
};

static const mini_key_input_api_t KEY_INPUT = {
    .struct_size = sizeof(mini_key_input_api_t),
    .read = fake_key_read,
};

static const mini_input_api_t INPUT = {
    .struct_size = sizeof(mini_input_api_t),
    .capabilities = MINI_INPUT_CAP_KEY,
    .key = &KEY_INPUT,
};

static const mini_digital_io_api_t DIGITAL_IO = {
    .struct_size = sizeof(mini_digital_io_api_t),
    .capabilities = MINI_DIGITAL_IO_CAP_INPUT_PULLUP |
                    MINI_DIGITAL_IO_CAP_OUTPUT_OPEN_DRAIN,
    .open = fake_dio_open,
    .read = fake_dio_read,
    .write = fake_dio_write,
    .close = fake_dio_close,
};

static char screen[7][21];
static bool decoded_seen, save_failed_seen, saved_seen;
static mini_result_t display_info(mini_text_display_info_t *i)
{ i->columns = 20; i->rows = 7; return MINI_OK; }
static mini_result_t display_clear(void) { return MINI_OK; }
static mini_result_t display_write(uint32_t r, uint32_t c, const char *s, uint32_t n, uint32_t attr)
{
    if (display_diagnostic) check_display_window();
    (void)attr; CHECK(r < 7 && c == 0 && n == 20);
    memcpy(screen[r], s, n); screen[r][20] = 0;
    if (strstr(screen[r], "Save failed")) save_failed_seen = true;
    if (r == 1 && !strncmp(screen[r], "M1:", 3)) overlay_seen = true;
    if (strstr(screen[r], "Saved")) saved_seen = true;
    if (strstr(screen[r], "Mute:ON")) mute_on_seen = true;
    if (strstr(screen[r], "Mute:OFF")) mute_off_seen = true;
    if (strstr(screen[r], "Unsupported char")) unsupported_seen = true;
    if (r > 0 && r < 6 && strchr(screen[r], 'E')) decoded_seen = true;
    return MINI_OK;
}
static const mini_text_display_api_t TEXT = {
    .get_info = display_info, .clear = display_clear, .write_at_attr = display_write,
};
static mini_result_t display_present(void)
{
    if (display_diagnostic) { check_display_window(); ++present_calls; }
    return MINI_OK;
}
static const mini_display_api_t DISPLAY = {.text = &TEXT, .present = display_present};
static const mini_api_t API = {
    .api_version = MINISHELL_API_VERSION,
    .struct_size = sizeof(mini_api_t),
    .console = &CONSOLE,
    .display = &DISPLAY,
    .fs = &FS,
    .time_location = &TIME_LOCATION,
    .input = &INPUT,
    .digital_io = &DIGITAL_IO,
};

static void run_scenario(unsigned which)
{
    manual_render = false;
    scenario = which; input_index = 0; final_release = 0; decoded_seen = false;
    save_failed_seen = saved_seen = overlay_seen = false;
    save_count = 0; mute_on_seen = mute_off_seen = unsupported_seen = false;
    memset(s_opened, 0, sizeof(s_opened));
    memset(s_mode, 0, sizeof(s_mode));
    for (size_t i = 0u; i < 64u; ++i) s_level[i] = 1u;
    s_now_us = 0u;
    s_q_sent = false;
    s_saw_key_down = false;
    s_console_len = 0u;
    s_console[0] = '\0';

    CHECK(app_controller_init(&API) == MINI_OK);
    CHECK(s_mode[13] == MINI_DIGITAL_IO_MODE_INPUT_PULLUP);
    CHECK(s_mode[15] == MINI_DIGITAL_IO_MODE_INPUT_PULLUP);
    CHECK(s_mode[3] == MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN);
    CHECK(s_mode[6] == MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN);

    CHECK(app_controller_run() == 0);
    CHECK(s_saw_key_down);
    if (scenario == 0) CHECK(manual_render);
    CHECK(s_level[3] == (scenario == 3 ? 0u : 1u) && s_level[6] == s_level[3]);
    CHECK(decoded_seen == (scenario != 3 && scenario != 9 && scenario != 12));
    if (scenario == 4) CHECK(final_release == 70000);
    CHECK(strcmp(screen[0], scenario == 2 ? "--:-- PdL SKS 21 V80" : "--:-- PdL SKS 20 V80") == 0);
    if (scenario == 1) {
        CHECK(final_release == 160000); /* Tune preempted by physical dit. */
        CHECK(s_now_us == 400000); /* Bare q did not exit. */
        CHECK(screen[6][0] == 'Q');
    }
    if (scenario == 2) {
        CHECK(save_failed_seen == fail_save && saved_seen != fail_save);
        if (!fail_save) CHECK(strstr(saved, "wpm=21\n") != NULL);
    }
    if (scenario == 5 || scenario == 6) {
        CHECK(!unsupported_seen && !saved_seen);
        if (fail_save) CHECK(save_failed_seen && !mute_on_seen && save_count == 0);
        else {
            CHECK(mute_on_seen && mute_off_seen == (scenario == 6));
            CHECK(save_count == (scenario == 6 ? 2u : 1u));
            CHECK(strstr(saved, scenario == 6 ? "mute=Off\n" : "mute=On\n") != NULL);
            CHECK(!strncmp(screen[6], scenario == 6 ? "Mute:OFF" : "Mute:ON", scenario == 6 ? 8 : 7));
        }
    }
    if (scenario == 7) {
        CHECK(saved_seen && save_count == 1);
        CHECK(strstr(saved, "paddle=IambicB\n") != NULL);
    }
    if (scenario >= 8) {
        CHECK(save_count == 0 && !saved_seen && !save_failed_seen);
        if (scenario == 9) {
            CHECK(final_release == 20000); /* Active dah cancelled at backtick. */
            CHECK(!strcmp(screen[6], "                    "));
        }
    }
    if (scenario >= 10) {
        CHECK(overlay_seen);
        CHECK(!unsupported_seen);
        if (scenario == 10) CHECK(!strncmp(screen[6], "CQ POTA", 7));
        if (scenario == 11) {
            CHECK(final_release == 210000);
            CHECK(!strcmp(screen[6], "                    "));
        }
        if (scenario == 12) CHECK(!strncmp(screen[1], "M1:CQ POTA", 10));
        else CHECK(screen[1][0] == 'E');
    }
    CHECK(strstr(s_console, "<BS>") == NULL);

    app_controller_shutdown();
    CHECK(!s_opened[13] && !s_opened[15]);
    CHECK(!s_opened[3] && !s_opened[6]);
    CHECK(s_level[3] == 1u && s_level[6] == 1u);

}
static void operation_backtick_r2(void)
{
    const mini_key_event_t edit_cancel[] = {
        {.type = MINI_KEY_EVENT_SPECIAL, .key = MINI_KEY_OPT},
        {.type = MINI_KEY_EVENT_CHAR, .codepoint = '3'},
        {.type = MINI_KEY_EVENT_SPECIAL, .key = MINI_KEY_RIGHT, .modifiers = MINI_MOD_FN},
        {.type = MINI_KEY_EVENT_CHAR, .codepoint = '`'},
        {.type = MINI_KEY_EVENT_CHAR, .codepoint = '`'},
    };
    mini_key_event_t events[6];
    memcpy(events, edit_cancel, sizeof(edit_cancel));
    r2_events = events; r2_count = 5;
    char old[1024]; strcpy(old, saved);
    run_scenario(8); CHECK(!strcmp(old, saved)); /* numeric */
    events[1].codepoint = '6';
    run_scenario(8); CHECK(!strcmp(old, saved)); /* choice */
    events[1] = (mini_key_event_t){.type = MINI_KEY_EVENT_SPECIAL, .key = MINI_KEY_DOWN, .modifiers = MINI_MOD_FN};
    events[2] = (mini_key_event_t){.type = MINI_KEY_EVENT_CHAR, .codepoint = '1'};
    events[3] = (mini_key_event_t){.type = MINI_KEY_EVENT_CHAR, .codepoint = 'X'};
    events[4] = events[5] = (mini_key_event_t){.type = MINI_KEY_EVENT_CHAR, .codepoint = '`'};
    r2_count = 6;
    run_scenario(8); CHECK(!strcmp(old, saved)); /* message */
    events[0] = (mini_key_event_t){.type = MINI_KEY_EVENT_CHAR, .codepoint = 'T'};
    events[1] = (mini_key_event_t){.type = MINI_KEY_EVENT_SPECIAL, .key = MINI_KEY_ENTER};
    events[2] = (mini_key_event_t){.type = MINI_KEY_EVENT_CHAR, .codepoint = '`'};
    r2_count = 3;
    run_scenario(9); CHECK(!strcmp(old, saved));
}
static void memory_overlay_r3(void)
{
    mini_key_event_t events[] = {
        {.type = MINI_KEY_EVENT_SPECIAL, .key = MINI_KEY_ALT, .modifiers = MINI_MOD_ALT},
        {.type = MINI_KEY_EVENT_CHAR, .codepoint = '1'},
        {.type = MINI_KEY_EVENT_SPECIAL, .key = MINI_KEY_ALT, .modifiers = MINI_MOD_ALT},
    };
    r2_events = events; r2_count = 3;
    run_scenario(10); /* Alt overlay + plain 1, then restore accumulated decode. */
    events[1].modifiers = MINI_MOD_ALT;
    run_scenario(10); /* Direct Alt+1 remains the same memory action. */
    events[1].modifiers = 0;
    run_scenario(11); /* Late physical press cancels pending memory, still decodes. */
    r2_count = 1;
    run_scenario(12); /* Ctrl+C while overlay visible still releases resources. */
}
static void display_starvation_r4(void)
{
    display_diagnostic = true;
    render_calls = present_calls = diagnostic_read = 0;
    delay_render = idle_repeat_render = catchup_render = false;
    input_index = 0; s_now_us = 0; s_saw_key_down = false;
    s_console_len = 0;
    CHECK(app_controller_init(&API) == MINI_OK);
    present_calls = 0;
    CHECK(app_controller_run() == 0);
    CHECK(s_saw_key_down && s_level[3] == 1 && s_level[6] == 1);
    CHECK(delay_render && catchup_render && idle_repeat_render);
    CHECK(render_calls > 2 && present_calls == render_calls);
    CHECK(last_render > 1842000 && s_now_us == 2100000);
    CHECK(!strcmp(screen[6], "                    "));
    app_controller_shutdown();
    CHECK(!s_opened[3] && !s_opened[6] && !s_opened[13] && !s_opened[15]);
    display_diagnostic = false;
}
int main(void)
{
    run_scenario(0); run_scenario(1); run_scenario(2); run_scenario(3); run_scenario(4);
    run_scenario(5); run_scenario(6); run_scenario(7);
    operation_backtick_r2();
    memory_overlay_r3();
    display_starvation_r4();
    char old[1024]; strcpy(old, saved); fail_save = true; run_scenario(2);
    run_scenario(5);
    CHECK(!strcmp(old, saved));
    puts("keyer_k4_controller_test: PASS");
    return 0;
}
