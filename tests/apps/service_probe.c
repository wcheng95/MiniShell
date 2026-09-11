#include <stdint.h>
#include <string.h>

#include "minishell/api.h"

static int fail(const mini_api_t *api, const char *message, int code)
{
    if (api != NULL && api->system != NULL && api->system->write != NULL) {
        api->system->write("service_probe: FAIL: ");
        api->system->write(message);
        api->system->write("\n");
    }
    return code;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->system == NULL || api->system->write == NULL) {
        return 2;
    }

    if (api->memory == NULL || api->fs == NULL || api->time_location == NULL ||
        api->display == NULL || api->input == NULL || api->digital_io == NULL) {
        return fail(api, "missing service table", 3);
    }

    void *memory = NULL;
    if (api->memory->alloc(32u, &memory) != MINI_OK || memory == NULL) {
        return fail(api, "memory alloc", 4);
    }
    memset(memory, 0x5a, 32u);

    void *larger = NULL;
    if (api->memory->realloc(memory, 64u, &larger) != MINI_OK || larger == NULL) {
        return fail(api, "memory realloc", 5);
    }

    mini_memory_info_t memory_info = {.struct_size = sizeof(memory_info)};
    if (api->memory->get_info(&memory_info) != MINI_OK ||
        (memory_info.valid_fields & MINI_MEM_INFO_APP_USAGE) == 0u ||
        memory_info.app_allocation_count != 1u ||
        memory_info.app_allocated_bytes != 64u) {
        return fail(api, "memory accounting", 6);
    }

    if (api->memory->free(larger) != MINI_OK) {
        return fail(api, "memory free", 7);
    }

    const char *directory = "/sd/service_probe";
    const char *path_a = "/sd/service_probe/a.bin";
    const char *path_b = "/sd/service_probe/b.bin";
    const char payload[] = "MiniShell Linux filesystem";

    if (api->fs->mkdir(directory) != MINI_OK) {
        return fail(api, "filesystem mkdir", 8);
    }

    mini_file_t file = MINI_FILE_INVALID;
    uint32_t flags = MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC;
    if (api->fs->open(path_a, flags, &file) != MINI_OK) {
        return fail(api, "filesystem create", 9);
    }

    uint32_t written = 0u;
    if (api->fs->write(file, payload, (uint32_t)(sizeof(payload) - 1u), &written) != MINI_OK ||
        written != sizeof(payload) - 1u || api->fs->sync(file) != MINI_OK ||
        api->fs->close(file) != MINI_OK) {
        return fail(api, "filesystem write", 10);
    }

    file = MINI_FILE_INVALID;
    if (api->fs->open(path_a, MINI_FS_READ, &file) != MINI_OK) {
        return fail(api, "filesystem reopen", 11);
    }

    char buffer[64] = {0};
    uint32_t read_count = 0u;
    if (api->fs->read(file, buffer, sizeof(buffer), &read_count) != MINI_OK ||
        api->fs->close(file) != MINI_OK ||
        read_count != sizeof(payload) - 1u ||
        memcmp(buffer, payload, sizeof(payload) - 1u) != 0) {
        return fail(api, "filesystem read", 12);
    }

    mini_fs_stat_t stat_info = {.struct_size = sizeof(stat_info)};
    if (api->fs->stat(path_a, &stat_info) != MINI_OK ||
        stat_info.type != MINI_FS_TYPE_FILE ||
        stat_info.size != sizeof(payload) - 1u) {
        return fail(api, "filesystem stat", 13);
    }

    if (api->fs->rename(path_a, path_b) != MINI_OK ||
        api->fs->remove_file(path_b) != MINI_OK ||
        api->fs->rmdir(directory) != MINI_OK) {
        return fail(api, "filesystem namespace", 14);
    }

    uint64_t before = api->time_location->monotonic_us();
    if (api->time_location->sleep_ms(2u) != MINI_OK) {
        return fail(api, "time sleep", 15);
    }
    uint64_t after = api->time_location->monotonic_us();
    if (after <= before) {
        return fail(api, "monotonic clock", 16);
    }

    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    if (api->time_location->utc_get(&utc) != MINI_OK || utc.nanoseconds >= 1000000000u) {
        return fail(api, "UTC", 17);
    }

    mini_geo_point_t location = {
        .struct_size = sizeof(location),
        .latitude_e7 = 374221234,
        .longitude_e7 = -1220845678,
    };
    if (api->time_location->location_default_set(&location) != MINI_OK) {
        return fail(api, "default location set", 18);
    }

    mini_geo_point_t location_read = {.struct_size = sizeof(location_read)};
    if (api->time_location->location_default_get(&location_read) != MINI_OK ||
        location_read.latitude_e7 != location.latitude_e7 ||
        location_read.longitude_e7 != location.longitude_e7) {
        return fail(api, "default location get", 19);
    }
    if (api->time_location->location_default_clear() != MINI_OK) {
        return fail(api, "default location clear", 20);
    }

    if ((api->display->capabilities & MINI_DISPLAY_CAP_TEXT) == 0u ||
        api->display->text == NULL || api->display->present == NULL) {
        return fail(api, "display capability", 21);
    }
    mini_text_display_info_t display_info = {.struct_size = sizeof(display_info)};
    const char marker[] = "D";
    if (api->display->text->get_info(&display_info) != MINI_OK ||
        display_info.columns == 0u || display_info.rows == 0u ||
        api->display->text->write_at(0u, 0u, marker, 1u) != MINI_OK ||
        api->display->text->clear_at(0u, 0u, 1u, 1u) != MINI_OK ||
        api->display->present() != MINI_OK) {
        return fail(api, "display backend", 22);
    }

    if ((api->input->capabilities & MINI_INPUT_CAP_KEY) == 0u ||
        api->input->key == NULL || api->input->key->read == NULL) {
        return fail(api, "input capability", 23);
    }

    const uint64_t digital_required = MINI_DIGITAL_IO_CAP_INPUT |
                                      MINI_DIGITAL_IO_CAP_INPUT_PULLUP |
                                      MINI_DIGITAL_IO_CAP_OUTPUT |
                                      MINI_DIGITAL_IO_CAP_OUTPUT_OPEN_DRAIN;
    if ((api->digital_io->capabilities & digital_required) != digital_required ||
        api->digital_io->open == NULL || api->digital_io->read == NULL ||
        api->digital_io->write == NULL || api->digital_io->close == NULL) {
        return fail(api, "digital io capability", 24);
    }

    mini_digital_io_t input = MINI_DIGITAL_IO_INVALID;
    mini_digital_io_config_t input_cfg = {
        .struct_size = sizeof(input_cfg),
        .line_id = 100u,
        .mode = MINI_DIGITAL_IO_MODE_INPUT_PULLUP,
        .initial_level = 0u,
    };
    uint32_t level = 0u;
    if (api->digital_io->open(&input_cfg, &input) != MINI_OK ||
        input == MINI_DIGITAL_IO_INVALID ||
        api->digital_io->read(input, &level) != MINI_OK || level != 1u ||
        api->digital_io->write(input, 0u) != MINI_ERR_ACCESS) {
        return fail(api, "digital input", 25);
    }

    mini_digital_io_t duplicate = MINI_DIGITAL_IO_INVALID;
    if (api->digital_io->open(&input_cfg, &duplicate) != MINI_ERR_EXISTS ||
        duplicate != MINI_DIGITAL_IO_INVALID ||
        api->digital_io->close(input) != MINI_OK ||
        api->digital_io->close(input) != MINI_ERR_BAD_HANDLE) {
        return fail(api, "digital ownership", 26);
    }

    mini_digital_io_t output = MINI_DIGITAL_IO_INVALID;
    mini_digital_io_config_t output_cfg = {
        .struct_size = sizeof(output_cfg),
        .line_id = 101u,
        .mode = MINI_DIGITAL_IO_MODE_OUTPUT,
        .initial_level = 1u,
    };
    if (api->digital_io->open(&output_cfg, &output) != MINI_OK ||
        api->digital_io->read(output, &level) != MINI_OK || level != 1u ||
        api->digital_io->write(output, 0u) != MINI_OK ||
        api->digital_io->read(output, &level) != MINI_OK || level != 0u ||
        api->digital_io->close(output) != MINI_OK) {
        return fail(api, "digital output", 27);
    }

    mini_digital_io_t open_drain = MINI_DIGITAL_IO_INVALID;
    mini_digital_io_config_t od_cfg = {
        .struct_size = sizeof(od_cfg),
        .line_id = 102u,
        .mode = MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN,
        .initial_level = 1u,
    };
    if (api->digital_io->open(&od_cfg, &open_drain) != MINI_OK ||
        api->digital_io->write(open_drain, 0u) != MINI_OK) {
        return fail(api, "digital open drain", 28);
    }
    /* Intentionally leave open_drain open. app_end() must reclaim it, and the
     * second service_probe run in linux_services.py proves that cleanup. */

    api->system->write("service_probe: PASS\n");
    return 0;
}
