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
static uint64_t final_release;
static char saved[1024], temporary[1024];
static unsigned write_position;
static bool fail_save;
static unsigned save_count;
static bool mute_on_seen, mute_off_seen, unsupported_seen;
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
    return MINI_ERR_NOT_FOUND;
}

static mini_result_t fake_fs_read(mini_file_t file, void *buffer, uint32_t size,
                                  uint32_t *out_read)
{
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

    if (id == 13u) {
        if (scenario == 3) *out_level = 1;
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

static const mini_time_location_api_t TIME_LOCATION = {
    .struct_size = sizeof(mini_time_location_api_t),
    .monotonic_us = fake_monotonic_us,
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
    (void)attr; CHECK(r < 7 && c == 0 && n == 20);
    memcpy(screen[r], s, n); screen[r][20] = 0;
    if (strstr(screen[r], "Save failed")) save_failed_seen = true;
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
static const mini_display_api_t DISPLAY = {.text = &TEXT, .present = display_clear};
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
    scenario = which; input_index = 0; final_release = 0; decoded_seen = false;
    save_failed_seen = saved_seen = false;
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
    CHECK(s_level[3] == (scenario == 3 ? 0u : 1u) && s_level[6] == s_level[3]);
    CHECK(decoded_seen == (scenario != 3));
    if (scenario == 4) CHECK(final_release == 70000);
    CHECK(strcmp(screen[0], scenario == 2 ? "--:-- Pdl SKS 21 V80" : "--:-- Pdl SKS 20 V80") == 0);
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
    CHECK(strstr(s_console, "<BS>") == NULL);

    app_controller_shutdown();
    CHECK(!s_opened[13] && !s_opened[15]);
    CHECK(!s_opened[3] && !s_opened[6]);
    CHECK(s_level[3] == 1u && s_level[6] == 1u);

}
int main(void)
{
    run_scenario(0); run_scenario(1); run_scenario(2); run_scenario(3); run_scenario(4);
    run_scenario(5); run_scenario(6); run_scenario(7);
    char old[1024]; strcpy(old, saved); fail_save = true; run_scenario(2);
    run_scenario(5);
    CHECK(!strcmp(old, saved));
    puts("keyer_k4_controller_test: PASS");
    return 0;
}
