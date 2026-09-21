/* All MiniShell ownership is private to this adapter. No board SDK calls. */
#include "minicw_port.h"
#include "minicw_run.h"
#include "minicw_input.h"
#include "app_core.h"
#include <stddef.h>
#include <string.h>

static const mini_api_t *s_api;
static mini_digital_io_t s_lines[4];
static const uint32_t line_ids[4] = {13, 15, 3, 6};
static mini_result_t s_error;
static bool s_exit;
static char s_presented[7][21];
static bool s_have_frame;

static void record(mini_result_t result)
{
    if (s_error == MINI_OK && result != MINI_OK) s_error = result;
}
/* Optional tail discovery must not read past a pre-T043 Audio object. */
static const mini_audio_tone_api_t *s_tone_api;
static mini_audio_tone_t s_tone;
bool minicw_port_tone_open(uint16_t hz, uint8_t volume)
{
    s_tone_api = NULL; s_tone = MINI_AUDIO_TONE_INVALID;
    const mini_audio_api_t *audio = s_api->audio;
    if (!audio || audio->struct_size < offsetof(mini_audio_api_t, tone) + sizeof(audio->tone) ||
        !(audio->capabilities & MINI_AUDIO_CAP_TONE)) return false;
    const mini_audio_tone_api_t *tone = audio->tone;
    if (!tone || tone->struct_size < sizeof(*tone) || !tone->open || !tone->configure ||
        !tone->enqueue || !tone->hold || !tone->stop || !tone->busy || !tone->close) return false;
    s_tone_api = tone;
    mini_audio_tone_config_t config = {sizeof(config), hz, volume};
    record(tone->open(&config, &s_tone));
    return true;
}
void minicw_port_tone_configure(uint16_t hz, uint8_t volume)
{
    mini_audio_tone_config_t config = {sizeof(config), hz, volume};
    if (s_tone) record(s_tone_api->configure(s_tone, &config));
}
void minicw_port_tone_enqueue(uint32_t ms) { if (s_tone) record(s_tone_api->enqueue(s_tone, ms)); }
void minicw_port_tone_hold(bool active) { if (s_tone) record(s_tone_api->hold(s_tone, active)); }
void minicw_port_tone_stop(void) { if (s_tone) record(s_tone_api->stop(s_tone)); }
bool minicw_port_tone_busy(void)
{
    uint32_t busy = 0;
    if (s_tone) record(s_tone_api->busy(s_tone, &busy));
    return busy != 0;
}
static void minicw_port_tone_close(void)
{
    if (s_tone) record(s_tone_api->close(s_tone));
    s_tone = MINI_AUDIO_TONE_INVALID; s_tone_api = NULL;
}
/* Storage remains optional and never poisons local Keyer operation. */
static const mini_fs_api_t *filesystem(void)
{
    const mini_fs_api_t *fs = s_api ? s_api->fs : NULL;
    if (!fs || fs->struct_size < offsetof(mini_fs_api_t, mkdir) + sizeof(fs->mkdir) ||
        !fs->open || !fs->close || !fs->read || !fs->write || !fs->sync ||
        !fs->mkdir || !fs->remove_file || !fs->rename) return NULL;
    return fs;
}
minicw_file_result_t minicw_port_file_read(const char *path, char *out, uint32_t capacity)
{
    const mini_fs_api_t *fs = filesystem();
    if (!fs || !out || capacity < 2) return MINICW_FILE_ERROR;
    mini_file_t file = MINI_FILE_INVALID;
    mini_result_t r = fs->open(path, MINI_FS_READ, &file);
    if (r == MINI_ERR_NOT_FOUND) return MINICW_FILE_MISSING;
    if (r != MINI_OK) return MINICW_FILE_ERROR;
    uint32_t used = 0;
    minicw_file_result_t result = MINICW_FILE_OK;
    for (;;) {
        char chunk[128]; uint32_t got = 0;
        r = fs->read(file, chunk, sizeof(chunk), &got);
        if (r != MINI_OK || got > sizeof(chunk)) { result = MINICW_FILE_ERROR; break; }
        if (!got) break;
        if (got >= capacity - used) { result = MINICW_FILE_INVALID; break; }
        for (uint32_t i = 0; i < got; ++i) if (!chunk[i]) result = MINICW_FILE_INVALID;
        if (result != MINICW_FILE_OK) break;
        memcpy(out + used, chunk, got); used += got;
    }
    out[used] = 0;
    if (fs->close(file) != MINI_OK) result = MINICW_FILE_ERROR;
    return result;
}
bool minicw_port_file_replace(const char *directory, const char *temporary, const char *destination,
                              const char *text, uint32_t size)
{
    const mini_fs_api_t *fs = filesystem();
    if (!fs) return false;
    mini_result_t r = fs->mkdir(directory);
    if (r != MINI_OK && r != MINI_ERR_EXISTS) return false;
    (void)fs->remove_file(temporary);
    mini_file_t file = MINI_FILE_INVALID;
    r = fs->open(temporary, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC, &file);
    if (r != MINI_OK) { (void)fs->remove_file(temporary); return false; }
    uint32_t offset = 0;
    while (r == MINI_OK && offset < size) {
        uint32_t written = 0;
        r = fs->write(file, text + offset, size - offset, &written);
        if (!written || written > size - offset) r = MINI_ERR_IO;
        if (r == MINI_OK) offset += written;
    }
    if (r == MINI_OK) r = fs->sync(file);
    mini_result_t closed = fs->close(file);
    if (r == MINI_OK) r = closed;
    /* The destination is untouched until this single commit operation. */
    if (r == MINI_OK) r = fs->rename(temporary, destination);
    if (r != MINI_OK) (void)fs->remove_file(temporary);
    return r == MINI_OK;
}
bool minicw_port_utc_hm(uint8_t *hour, uint8_t *minute)
{
    const mini_time_location_api_t *time = s_api->time_location;
    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    if (!(time->capabilities & MINI_TIMELOC_CAP_UTC) || !time->utc_get ||
        time->utc_get(&utc) != MINI_OK) return false;
    int64_t day = utc.unix_seconds % 86400;
    if (day < 0) day += 86400;
    *hour = (uint8_t)(day / 3600);
    *minute = (uint8_t)((day % 3600) / 60);
    return true;
}
uint32_t minicw_port_now_ms(void)
{
    return (uint32_t)(s_api->time_location->monotonic_us() / 1000U);
}
uint32_t minicw_port_ticks(void)
{
    return (uint32_t)(s_api->time_location->monotonic_us() / 10000U);
}
bool minicw_port_inputs_ready(void) { return s_lines[0] && s_lines[1]; }
bool minicw_port_outputs_ready(void) { return s_lines[2] && s_lines[3]; }
uint32_t minicw_port_read(uint32_t line)
{
    uint32_t level = 1;
    for (unsigned i = 0; i < 2; ++i) {
        if (line_ids[i] == line && s_lines[i]) {
            record(s_api->digital_io->read(s_lines[i], &level));
            break;
        }
    }
    return level;
}
void minicw_port_write(uint32_t line, uint32_t level)
{
    for (unsigned i = 2; i < 4; ++i)
        if (line_ids[i] == line && s_lines[i]) record(s_api->digital_io->write(s_lines[i], level));
}
void minicw_port_present(const char rows[7][21])
{
    bool changed = false;
    for (unsigned row = 0; row < 7; ++row) {
        if (!s_have_frame || memcmp(rows[row], s_presented[row], 20)) {
            record(s_api->display->text->write_at(row, 0, rows[row], 20));
            changed = true;
        }
    }
    if (changed) {
        record(s_api->display->present());
        memcpy(s_presented, rows, sizeof(s_presented));
        s_have_frame = true;
    }
}
bool minicw_input_poll_input(minicw_input_event_t *out)
{
    mini_key_event_t key = {.struct_size = sizeof(key)};
    memset(out, 0, sizeof(*out));
    mini_result_t r = s_api->input->key->read(&key, MINI_WAIT_NONE);
    if (r == MINI_ERR_NOT_READY || r == MINI_ERR_TIMEOUT) return false;
    record(r);
    if (r != MINI_OK) return false;
    out->fn = (key.modifiers & MINI_MOD_FN) != 0;
    out->ctrl = (key.modifiers & MINI_MOD_CTRL) != 0;
    out->alt = (key.modifiers & MINI_MOD_ALT) != 0;
    out->opt = (key.modifiers & MINI_MOD_OPT) != 0;
    out->shift = (key.modifiers & MINI_MOD_SHIFT) != 0;
    if (key.type == MINI_KEY_EVENT_CHAR) {
        if (key.codepoint == 3 || (out->ctrl && (key.codepoint == 'c' || key.codepoint == 'C'))) {
            s_exit = true;
            return false;
        }
        if (key.codepoint > 127) return false;
        out->ch = (char)key.codepoint;
    } else if (key.type == MINI_KEY_EVENT_SPECIAL) {
        switch (key.key) {
        case MINI_KEY_CTRL: out->type = MINICW_INPUT_EVENT_CTRL; return true;
        case MINI_KEY_ALT: out->type = MINICW_INPUT_EVENT_ALT; return true;
        case MINI_KEY_OPT: out->type = MINICW_INPUT_EVENT_OPT; return true;
        case MINI_KEY_FN: out->type = MINICW_INPUT_EVENT_FN; return true;
        case MINI_KEY_ENTER: out->ch = '\n'; break;
        case MINI_KEY_BACKSPACE: out->ch = '\b'; break;
        case MINI_KEY_ESCAPE: out->ch = 27; break;
        case MINI_KEY_TAB: out->ch = '\t'; break;
        case MINI_KEY_UP: out->ch = ';'; break;
        case MINI_KEY_DOWN: out->ch = '.'; break;
        case MINI_KEY_LEFT: out->ch = ','; break;
        case MINI_KEY_RIGHT: out->ch = '/'; break;
        default: return false;
        }
    } else return false;
    out->type = MINICW_INPUT_EVENT_CHAR;
    return true;
}

static bool available(const mini_api_t *api)
{
    return api && api->api_version == MINISHELL_API_VERSION && api->struct_size >= sizeof(*api) &&
        api->time_location && api->time_location->struct_size >= sizeof(*api->time_location) &&
        api->time_location->monotonic_us && api->time_location->sleep_ms &&
        api->digital_io && api->digital_io->struct_size >= sizeof(*api->digital_io) &&
        (api->digital_io->capabilities & (MINI_DIGITAL_IO_CAP_INPUT_PULLUP | MINI_DIGITAL_IO_CAP_OUTPUT_OPEN_DRAIN)) ==
        (MINI_DIGITAL_IO_CAP_INPUT_PULLUP | MINI_DIGITAL_IO_CAP_OUTPUT_OPEN_DRAIN) &&
        api->digital_io->open && api->digital_io->read && api->digital_io->write && api->digital_io->close &&
        api->input && api->input->struct_size >= sizeof(*api->input) &&
        (api->input->capabilities & MINI_INPUT_CAP_KEY) && api->input->key &&
        api->input->key->struct_size >= sizeof(*api->input->key) && api->input->key->read &&
        api->display && api->display->struct_size >= sizeof(*api->display) &&
        (api->display->capabilities & MINI_DISPLAY_CAP_TEXT) && api->display->present &&
        api->display->text && api->display->text->struct_size >= sizeof(*api->display->text) &&
        api->display->text->get_info && api->display->text->write_at && api->display->text->clear;
}
int minicw_run(const mini_api_t *api)
{
    mini_text_display_info_t info = {.struct_size = sizeof(info)};
    if (!available(api)) return 1;
    s_api = api;
    s_error = MINI_OK;
    s_exit = s_have_frame = false;
    memset(s_lines, 0, sizeof(s_lines));
    record(api->display->text->get_info(&info));
    if (s_error != MINI_OK || info.columns < 20 || info.rows < 7) return 1;
    for (unsigned i = 0; i < 4 && s_error == MINI_OK; ++i) {
        mini_digital_io_config_t config = {
            .struct_size = sizeof(config), .line_id = line_ids[i],
            .mode = i < 2 ? MINI_DIGITAL_IO_MODE_INPUT_PULLUP : MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN,
            .initial_level = 1
        };
        record(api->digital_io->open(&config, &s_lines[i]));
    }
    if (s_error == MINI_OK) {
        record(api->display->text->clear());
        app_core_init();
        while (!s_exit && s_error == MINI_OK) {
            app_core_step();
            if (!s_exit && s_error == MINI_OK) record(api->time_location->sleep_ms(10));
        }
        app_core_shutdown();
        minicw_port_tone_close();
        if (s_error == MINI_OK) app_core_save_on_exit();
    }
    /* Release both wires, including SK-M's normally asserted ring, even on partial init/error. */
    for (unsigned i = 2; i < 4; ++i)
        if (s_lines[i]) record(api->digital_io->write(s_lines[i], 1));
    for (unsigned i = 0; i < 4; ++i) {
        if (s_lines[i]) record(api->digital_io->close(s_lines[i]));
        s_lines[i] = MINI_DIGITAL_IO_INVALID;
    }
    s_api = NULL;
    return s_error == MINI_OK ? 0 : 1;
}
