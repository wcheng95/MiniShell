#include <stdint.h>
#include <string.h>

#include "minishell/api.h"

static mini_result_t row_write(const mini_text_display_api_t *text,
                               uint32_t row, uint32_t columns, const char *line)
{
    mini_result_t result = text->clear_at(row, 0u, 1u, columns);
    if (result != MINI_OK) return result;
    uint32_t count = (uint32_t)strlen(line);
    if (count > columns) count = columns;
    return text->write_at(row, 0u, line, count);
}

static int fail(const mini_api_t *api, const char *message, int code)
{
    if (api != NULL && api->system != NULL && api->system->write != NULL) {
        api->system->write("a3_probe: FAIL: ");
        api->system->write(message);
        api->system->write("\n");
    }
    return code;
}

static int test_flash(const mini_api_t *api)
{
    mini_fs_stat_t stat_info = {.struct_size = sizeof(stat_info)};
    if (api->fs->stat("/flash", &stat_info) != MINI_OK ||
        stat_info.type != MINI_FS_TYPE_DIRECTORY) {
        return 10;
    }

    const char *dir = "/flash/a3probe";
    const char *a = "/flash/a3probe/a.txt";
    const char *b = "/flash/a3probe/b.txt";
    const char payload[] = "MiniShell A3 LittleFS";

    (void)api->fs->remove_file(a);
    (void)api->fs->remove_file(b);
    mini_result_t result = api->fs->mkdir(dir);
    if (result != MINI_OK && result != MINI_ERR_EXISTS) return 11;

    mini_file_t file = MINI_FILE_INVALID;
    if (api->fs->open(a, MINI_FS_READ | MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC,
                      &file) != MINI_OK) {
        return 12;
    }
    uint32_t written = 0u;
    if (api->fs->write(file, payload, (uint32_t)(sizeof(payload) - 1u), &written) != MINI_OK ||
        written != sizeof(payload) - 1u || api->fs->sync(file) != MINI_OK ||
        api->fs->close(file) != MINI_OK) {
        return 13;
    }

    file = MINI_FILE_INVALID;
    if (api->fs->open(a, MINI_FS_READ, &file) != MINI_OK) return 14;
    char buffer[64] = {0};
    uint32_t read_count = 0u;
    if (api->fs->read(file, buffer, sizeof(buffer), &read_count) != MINI_OK ||
        api->fs->close(file) != MINI_OK || read_count != sizeof(payload) - 1u ||
        memcmp(buffer, payload, sizeof(payload) - 1u) != 0) {
        return 15;
    }

    if (api->fs->rename(a, b) != MINI_OK) return 16;

    mini_dir_t directory = MINI_DIR_INVALID;
    if (api->fs->dir_open(dir, &directory) != MINI_OK) return 17;
    int found = 0;
    for (;;) {
        mini_fs_dir_entry_t entry = {.struct_size = sizeof(entry)};
        uint32_t has_entry = 0u;
        if (api->fs->dir_read(directory, &entry, &has_entry) != MINI_OK) {
            (void)api->fs->dir_close(directory);
            return 18;
        }
        if (has_entry == 0u) break;
        if (entry.type == MINI_FS_TYPE_FILE && strcmp(entry.name, "b.txt") == 0) found = 1;
    }
    if (api->fs->dir_close(directory) != MINI_OK || !found) return 19;

    if (api->fs->remove_file(b) != MINI_OK || api->fs->rmdir(dir) != MINI_OK) return 20;
    return 0;
}

static int test_time_location(const mini_api_t *api, int *out_utc_ready)
{
    uint64_t before = api->time_location->monotonic_us();
    if (api->time_location->sleep_ms(5u) != MINI_OK) return 30;
    if (api->time_location->monotonic_us() <= before) return 31;

    uint64_t required = MINI_TIMELOC_CAP_UTC |
                        MINI_TIMELOC_CAP_SET_UTC |
                        MINI_TIMELOC_CAP_DEFAULT_LOCATION |
                        MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION;
    if ((api->time_location->capabilities & required) != required) return 32;

    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    *out_utc_ready = api->time_location->utc_get(&utc) == MINI_OK;

    mini_geo_point_t saved = {.struct_size = sizeof(saved)};
    mini_result_t saved_result = api->time_location->location_default_get(&saved);
    int had_saved = saved_result == MINI_OK;
    if (!had_saved && saved_result != MINI_ERR_NOT_READY) return 33;

    mini_geo_point_t test = {
        .struct_size = sizeof(test),
        .latitude_e7 = 374221234,
        .longitude_e7 = -1220845678,
    };
    if (api->time_location->location_default_set(&test) != MINI_OK) return 34;

    mini_geo_point_t readback = {.struct_size = sizeof(readback)};
    if (api->time_location->location_default_get(&readback) != MINI_OK ||
        readback.latitude_e7 != test.latitude_e7 ||
        readback.longitude_e7 != test.longitude_e7) {
        return 35;
    }

    if (had_saved) {
        if (api->time_location->location_default_set(&saved) != MINI_OK) return 36;
    } else if (api->time_location->location_default_clear() != MINI_OK) {
        return 37;
    }
    return 0;
}

int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return 2;

    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->system == NULL || api->system->write == NULL || api->fs == NULL ||
        api->time_location == NULL || api->display == NULL || api->display->text == NULL ||
        api->input == NULL || api->input->key == NULL) {
        return fail(api, "missing A3a service", 3);
    }

    int result = test_flash(api);
    if (result != 0) return fail(api, "LittleFS", result);

    int utc_ready = 0;
    result = test_time_location(api, &utc_ready);
    if (result != 0) return fail(api, "Time/Location", result);

    const mini_text_display_api_t *text = api->display->text;
    mini_text_display_info_t info = {.struct_size = sizeof(info)};
    if (text->get_info(&info) != MINI_OK || info.rows < 6u) {
        return fail(api, "display", 40);
    }

    if (text->clear() != MINI_OK ||
        row_write(text, 0u, info.columns, "MiniShell A3a probe") != MINI_OK ||
        row_write(text, 1u, info.columns, "LittleFS PASS") != MINI_OK ||
        row_write(text, 2u, info.columns, "File/Dir PASS") != MINI_OK ||
        row_write(text, 3u, info.columns, "TimeLoc PASS") != MINI_OK ||
        row_write(text, 4u, info.columns, utc_ready ? "UTC loaded" : "UTC not set") != MINI_OK ||
        row_write(text, 5u, info.columns, "q/Enter exits") != MINI_OK ||
        api->display->present() != MINI_OK) {
        return fail(api, "display result", 41);
    }

    for (;;) {
        mini_key_event_t event = {.struct_size = sizeof(event)};
        if (api->input->key->read(&event, MINI_WAIT_FOREVER) != MINI_OK) {
            return fail(api, "input", 42);
        }
        if (event.type == MINI_KEY_EVENT_SPECIAL &&
            (event.key == MINI_KEY_ENTER || event.key == MINI_KEY_ESCAPE)) break;
        if (event.type == MINI_KEY_EVENT_CHAR &&
            (event.codepoint == (uint32_t)'q' || event.codepoint == (uint32_t)'Q')) break;
    }

    api->system->write("a3_probe: PASS\n");
    return 0;
}
