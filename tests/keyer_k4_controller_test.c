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
    (void)path;
    (void)flags;
    if (out_file != NULL) *out_file = MINI_FILE_INVALID;
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
    if (!s_q_sent && s_now_us >= 300000u) {
        out_event->struct_size = sizeof(*out_event);
        out_event->type = MINI_KEY_EVENT_CHAR;
        out_event->codepoint = (uint32_t)'q';
        out_event->key = 0u;
        out_event->modifiers = 0u;
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
        *out_level = s_now_us < 10000u ? 0u : 1u;
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

static const mini_fs_api_t FS = {
    .struct_size = sizeof(mini_fs_api_t),
    .open = fake_fs_open,
    .close = fake_fs_close,
    .read = fake_fs_read,
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

static const mini_api_t API = {
    .api_version = MINISHELL_API_VERSION,
    .struct_size = sizeof(mini_api_t),
    .console = &CONSOLE,
    .fs = &FS,
    .time_location = &TIME_LOCATION,
    .input = &INPUT,
    .digital_io = &DIGITAL_IO,
};

int main(void)
{
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
    CHECK(s_level[3] == 1u && s_level[6] == 1u);
    CHECK(strstr(s_console, "E") != NULL);

    app_controller_shutdown();
    CHECK(!s_opened[13] && !s_opened[15]);
    CHECK(!s_opened[3] && !s_opened[6]);
    CHECK(s_level[3] == 1u && s_level[6] == 1u);

    puts("keyer_k4_controller_test: PASS");
    return 0;
}
