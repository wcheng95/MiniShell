#include <stdint.h>
#include <stdio.h>
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

static int test_volume(const mini_api_t *api, const char *root, int code_base)
{
    mini_fs_stat_t stat_info = {.struct_size = sizeof(stat_info)};
    if (api->fs->stat(root, &stat_info) != MINI_OK ||
        stat_info.type != MINI_FS_TYPE_DIRECTORY) {
        return code_base;
    }

    char dir[40];
    char a[48];
    char b[48];
    if (snprintf(dir, sizeof(dir), "%s/a3probe", root) < 0 ||
        snprintf(a, sizeof(a), "%s/a3probe/a.txt", root) < 0 ||
        snprintf(b, sizeof(b), "%s/a3probe/b.txt", root) < 0) {
        return code_base + 1;
    }
    const char payload[] = "MiniShell A3 storage";

    (void)api->fs->remove_file(a);
    (void)api->fs->remove_file(b);
    mini_result_t result = api->fs->mkdir(dir);
    if (result != MINI_OK && result != MINI_ERR_EXISTS) return code_base + 2;

    mini_file_t file = MINI_FILE_INVALID;
    if (api->fs->open(a, MINI_FS_READ | MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC,
                      &file) != MINI_OK) {
        return code_base + 3;
    }
    uint32_t written = 0u;
    if (api->fs->write(file, payload, (uint32_t)(sizeof(payload) - 1u), &written) != MINI_OK ||
        written != sizeof(payload) - 1u || api->fs->sync(file) != MINI_OK ||
        api->fs->close(file) != MINI_OK) {
        return code_base + 4;
    }

    file = MINI_FILE_INVALID;
    if (api->fs->open(a, MINI_FS_READ, &file) != MINI_OK) return code_base + 5;
    char buffer[64] = {0};
    uint32_t read_count = 0u;
    if (api->fs->read(file, buffer, sizeof(buffer), &read_count) != MINI_OK ||
        api->fs->close(file) != MINI_OK || read_count != sizeof(payload) - 1u ||
        memcmp(buffer, payload, sizeof(payload) - 1u) != 0) {
        return code_base + 6;
    }

    if (api->fs->rename(a, b) != MINI_OK) return code_base + 7;

    mini_dir_t directory = MINI_DIR_INVALID;
    if (api->fs->dir_open(dir, &directory) != MINI_OK) return code_base + 8;
    int found = 0;
    for (;;) {
        mini_fs_dir_entry_t entry = {.struct_size = sizeof(entry)};
        uint32_t has_entry = 0u;
        if (api->fs->dir_read(directory, &entry, &has_entry) != MINI_OK) {
            (void)api->fs->dir_close(directory);
            return code_base + 9;
        }
        if (has_entry == 0u) break;
        if (entry.type == MINI_FS_TYPE_FILE && strcmp(entry.name, "b.txt") == 0) found = 1;
    }
    if (api->fs->dir_close(directory) != MINI_OK || !found) return code_base + 10;

    if (api->fs->remove_file(b) != MINI_OK || api->fs->rmdir(dir) != MINI_OK) {
        return code_base + 11;
    }
    return 0;
}

static int test_time_location(const mini_api_t *api)
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
    if (api->time_location->utc_get(&utc) != MINI_OK) return 33;

    mini_geo_point_t saved = {.struct_size = sizeof(saved)};
    mini_result_t saved_result = api->time_location->location_default_get(&saved);
    int had_saved = saved_result == MINI_OK;
    if (!had_saved && saved_result != MINI_ERR_NOT_READY) return 34;

    mini_geo_point_t test = {
        .struct_size = sizeof(test),
        .latitude_e7 = 374221234,
        .longitude_e7 = -1220845678,
    };
    if (api->time_location->location_default_set(&test) != MINI_OK) return 35;

    mini_geo_point_t readback = {.struct_size = sizeof(readback)};
    if (api->time_location->location_default_get(&readback) != MINI_OK ||
        readback.latitude_e7 != test.latitude_e7 ||
        readback.longitude_e7 != test.longitude_e7) {
        return 36;
    }

    if (had_saved) {
        if (api->time_location->location_default_set(&saved) != MINI_OK) return 37;
    } else if (api->time_location->location_default_clear() != MINI_OK) {
        return 38;
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
        return fail(api, "missing A3 service", 3);
    }

    int result = test_volume(api, "/flash", 10);
    if (result != 0) return fail(api, "LittleFS", result);

    mini_fs_stat_t sd_stat = {.struct_size = sizeof(sd_stat)};
    mini_result_t sd_result = api->fs->stat("/sd", &sd_stat);
    int sd_present = sd_result == MINI_OK && sd_stat.type == MINI_FS_TYPE_DIRECTORY;
    if (sd_result != MINI_OK && sd_result != MINI_ERR_NOT_FOUND) {
        return fail(api, "SD stat", 25);
    }
    if (sd_present) {
        result = test_volume(api, "/sd", 50);
        if (result != 0) return fail(api, "SD FATFS", result);
    }

    result = test_time_location(api);
    if (result != 0) return fail(api, "Time/Location", result);

    const mini_text_display_api_t *text = api->display->text;
    mini_text_display_info_t info = {.struct_size = sizeof(info)};
    if (text->get_info(&info) != MINI_OK || info.rows < 7u) {
        return fail(api, "display", 70);
    }

    if (text->clear() != MINI_OK ||
        row_write(text, 0u, info.columns, "MiniShell A3 probe") != MINI_OK ||
        row_write(text, 1u, info.columns, "LittleFS PASS") != MINI_OK ||
        row_write(text, 2u, info.columns, "File/Dir PASS") != MINI_OK ||
        row_write(text, 3u, info.columns, "TimeLoc PASS") != MINI_OK ||
        row_write(text, 4u, info.columns, "UTC session PASS") != MINI_OK ||
        row_write(text, 5u, info.columns, sd_present ? "SD FATFS PASS" : "SD absent OK") != MINI_OK ||
        row_write(text, 6u, info.columns, "q/Enter exits") != MINI_OK ||
        api->display->present() != MINI_OK) {
        return fail(api, "display result", 71);
    }

    for (;;) {
        mini_key_event_t event = {.struct_size = sizeof(event)};
        if (api->input->key->read(&event, MINI_WAIT_FOREVER) != MINI_OK) {
            return fail(api, "input", 72);
        }
        if (event.type == MINI_KEY_EVENT_SPECIAL &&
            (event.key == MINI_KEY_ENTER || event.key == MINI_KEY_ESCAPE)) break;
        if (event.type == MINI_KEY_EVENT_CHAR &&
            (event.codepoint == (uint32_t)'q' || event.codepoint == (uint32_t)'Q')) break;
    }

    api->system->write("a3_probe: PASS\n");
    return 0;
}
