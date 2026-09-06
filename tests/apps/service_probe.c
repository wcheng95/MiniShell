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
    if (api == NULL || api->abi_version != MINISHELL_ABI_VERSION ||
        api->system == NULL || api->system->write == NULL) {
        return 2;
    }

    if (api->memory == NULL || api->fs == NULL || api->time_location == NULL ||
        api->display == NULL || api->input == NULL) {
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
    if (api->display->text->get_info(&display_info) != MINI_OK ||
        display_info.columns == 0u || display_info.rows == 0u ||
        api->display->present() != MINI_OK) {
        return fail(api, "display backend", 22);
    }

    if ((api->input->capabilities & MINI_INPUT_CAP_KEY) == 0u ||
        api->input->key == NULL || api->input->key->read == NULL) {
        return fail(api, "input capability", 23);
    }

    api->system->write("service_probe: PASS\n");
    return 0;
}
