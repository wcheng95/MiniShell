#include "abi_test.h"

#define STRESS_ALLOCATIONS 8u

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = NULL;
    int rc = abi_test_base(&api);
    if (rc != 0) return rc;

    if (!ABI_HAS_API_FIELD(api, memory) || api->memory == NULL)
        return abi_test_fail(api, "abi_stress", "memory unavailable", 60);
    if (!ABI_HAS_API_FIELD(api, fs) || api->fs == NULL)
        return abi_test_fail(api, "abi_stress", "filesystem unavailable", 61);
    if (!ABI_HAS_API_FIELD(api, time_location) || api->time_location == NULL)
        return abi_test_fail(api, "abi_stress", "time/location unavailable", 62);
    if (!ABI_HAS_API_FIELD(api, display) || api->display == NULL)
        return abi_test_fail(api, "abi_stress", "display unavailable", 63);
    if (!ABI_HAS_API_FIELD(api, input) || api->input == NULL)
        return abi_test_fail(api, "abi_stress", "input unavailable", 64);

    const mini_memory_api_t *memory = api->memory;
    const mini_fs_api_t *fs = api->fs;
    const mini_time_location_api_t *time_location = api->time_location;
    const mini_display_api_t *display = api->display;
    const mini_input_api_t *input = api->input;

    if (memory->struct_size < ABI_FIELD_END(mini_memory_api_t, get_info) ||
        memory->alloc == NULL || memory->get_info == NULL)
        return abi_test_fail(api, "abi_stress", "memory table incomplete", 65);

    mini_memory_info_t info = {0};
    info.struct_size = sizeof(info);
    if (memory->get_info(&info) != MINI_OK ||
        (info.valid_fields & MINI_MEM_INFO_APP_USAGE) == 0u)
        return abi_test_fail(api, "abi_stress", "memory info unavailable", 66);

    if (info.app_allocation_count != 0u || info.app_allocated_bytes != 0u)
        return abi_test_fail(api, "abi_stress", "stale app allocations", 67);

    for (uint32_t i = 0u; i < STRESS_ALLOCATIONS; ++i) {
        void *ptr = NULL;
        if (memory->alloc(64u + i, &ptr) != MINI_OK || ptr == NULL)
            return abi_test_fail(api, "abi_stress", "allocation failed", 68);
    }

    info.struct_size = sizeof(info);
    if (memory->get_info(&info) != MINI_OK ||
        info.app_allocation_count != STRESS_ALLOCATIONS)
        return abi_test_fail(api, "abi_stress", "allocation tracking mismatch", 69);

    if (fs->struct_size < ABI_FIELD_END(mini_fs_api_t, stat) ||
        fs->open == NULL || fs->write == NULL || fs->sync == NULL)
        return abi_test_fail(api, "abi_stress", "filesystem table incomplete", 70);

    static const char path[] = "/sd/abistrs.tmp";
    static const uint8_t byte = 0x5au;
    mini_file_t first = MINI_FILE_INVALID;
    mini_file_t second = MINI_FILE_INVALID;

    if (fs->open(path, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC, &first) != MINI_OK)
        return abi_test_fail(api, "abi_stress", "first file open failed", 71);

    uint32_t written = 0u;
    if (fs->write(first, &byte, 1u, &written) != MINI_OK || written != 1u ||
        fs->sync(first) != MINI_OK)
        return abi_test_fail(api, "abi_stress", "file write failed", 72);

    if (fs->open(path, MINI_FS_READ, &second) != MINI_OK)
        return abi_test_fail(api, "abi_stress", "second file open failed", 73);

    if (time_location->struct_size < ABI_FIELD_END(mini_time_location_api_t, snapshot_get) ||
        time_location->monotonic_us == NULL || time_location->sleep_ms == NULL)
        return abi_test_fail(api, "abi_stress", "time table incomplete", 74);

    uint64_t before = time_location->monotonic_us();
    if (time_location->sleep_ms(1u) != MINI_OK)
        return abi_test_fail(api, "abi_stress", "sleep failed", 75);
    uint64_t after = time_location->monotonic_us();
    if (after < before)
        return abi_test_fail(api, "abi_stress", "monotonic moved backwards", 76);

    if (display->struct_size < ABI_FIELD_END(mini_display_api_t, present) ||
        display->present == NULL ||
        (display->capabilities & MINI_DISPLAY_CAP_TEXT) == 0u ||
        display->text == NULL ||
        display->text->struct_size < ABI_FIELD_END(mini_text_display_api_t, write_at) ||
        display->text->get_info == NULL)
        return abi_test_fail(api, "abi_stress", "display table incomplete", 77);

    mini_text_display_info_t display_info = {0};
    display_info.struct_size = sizeof(display_info);
    if (display->text->get_info(&display_info) != MINI_OK ||
        display_info.columns == 0u || display_info.rows == 0u ||
        display->present() != MINI_OK)
        return abi_test_fail(api, "abi_stress", "display operation failed", 78);

    if (input->struct_size < ABI_FIELD_END(mini_input_api_t, key) ||
        (input->capabilities & MINI_INPUT_CAP_KEY) == 0u ||
        input->key == NULL ||
        input->key->struct_size < ABI_FIELD_END(mini_key_input_api_t, read) ||
        input->key->read == NULL)
        return abi_test_fail(api, "abi_stress", "input table incomplete", 79);

    mini_key_event_t event = {0};
    event.struct_size = sizeof(event);
    mini_result_t input_result = input->key->read(&event, MINI_WAIT_NONE);
    if (input_result != MINI_OK && input_result != MINI_ERR_NOT_READY)
        return abi_test_fail(api, "abi_stress", "input poll failed", 80);

    /*
     * Intentional: do not free the eight allocations and do not close either
     * file handle. MiniShell app teardown must reclaim them after main returns.
     * The next invocation verifies memory cleanup immediately, while repeated
     * invocations will also expose leaked file handles.
     */
    abi_test_line(api, "abi_stress", "PASS");
    return 0;
}
