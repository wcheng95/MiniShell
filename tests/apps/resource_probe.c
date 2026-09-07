#include <stdint.h>
#include <string.h>

#include "minishell/api.h"

static int fail(const mini_api_t *api, const char *message)
{
    if (api != NULL && api->system != NULL && api->system->write != NULL) {
        api->system->write("resource_probe: FAIL: ");
        api->system->write(message);
        api->system->write("\n");
    }
    return 1;
}

static int write_file(const mini_api_t *api, const char *path,
                      const uint8_t *buffer, uint32_t size)
{
    mini_file_t file = MINI_FILE_INVALID;
    if (api->fs->open(path, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC,
                      &file) != MINI_OK) {
        return 1;
    }

    uint32_t total = 0u;
    while (total < size) {
        uint32_t written = 0u;
        mini_result_t result = api->fs->write(file, buffer + total, size - total, &written);
        if (result != MINI_OK || written == 0u) {
            (void)api->fs->close(file);
            return 1;
        }
        total += written;
    }
    return api->fs->close(file) == MINI_OK ? 0 : 1;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->memory == NULL || api->fs == NULL ||
        api->memory->get_info == NULL || api->fs->space == NULL) {
        return fail(api, "services unavailable");
    }

    mini_memory_info_t memory = {.struct_size = sizeof(memory)};
    if (api->memory->get_info(&memory) != MINI_OK ||
        (memory.valid_fields & MINI_MEM_INFO_FREE_BYTES) == 0u ||
        memory.free_bytes != 4096u) {
        return fail(api, "initial memory budget");
    }

    void *block = NULL;
    if (api->memory->alloc(3072u, &block) != MINI_OK || block == NULL) {
        return fail(api, "memory allocation within budget");
    }
    void *extra = NULL;
    if (api->memory->alloc(2048u, &extra) != MINI_ERR_NO_MEMORY || extra != NULL) {
        return fail(api, "memory budget enforcement");
    }
    memory.struct_size = sizeof(memory);
    if (api->memory->get_info(&memory) != MINI_OK || memory.free_bytes != 1024u) {
        return fail(api, "memory free accounting");
    }
    if (api->memory->free(block) != MINI_OK) {
        return fail(api, "memory free");
    }

    mini_fs_space_t space = {.struct_size = sizeof(space)};
    if (api->fs->space("/", &space) != MINI_OK ||
        space.total_bytes != 4096u || space.used_bytes != 0u || space.free_bytes != 4096u) {
        return fail(api, "initial storage budget");
    }

    mini_file_t file = MINI_FILE_INVALID;
    if (api->fs->open("/sd/quota.bin", MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC,
                      &file) != MINI_OK) {
        return fail(api, "storage create");
    }

    uint8_t buffer[3072];
    memset(buffer, 0x5a, sizeof(buffer));
    uint32_t written = 0u;
    if (api->fs->write(file, buffer, sizeof(buffer), &written) != MINI_OK ||
        written != sizeof(buffer)) {
        return fail(api, "storage write within budget");
    }
    written = 0u;
    if (api->fs->write(file, buffer, 2048u, &written) != MINI_ERR_NO_SPACE || written != 0u) {
        return fail(api, "storage budget enforcement");
    }
    written = 0u;
    if (api->fs->write(file, buffer, 1024u, &written) != MINI_OK || written != 1024u) {
        return fail(api, "storage fill to budget");
    }
    if (api->fs->close(file) != MINI_OK) {
        return fail(api, "storage close");
    }

    space.struct_size = sizeof(space);
    if (api->fs->space("/sd", &space) != MINI_OK ||
        space.used_bytes != 4096u || space.free_bytes != 0u) {
        return fail(api, "storage accounting");
    }
    if (api->fs->remove_file("/sd/quota.bin") != MINI_OK) {
        return fail(api, "storage remove");
    }

    if (write_file(api, "/sd/source.bin", buffer, 1024u) != 0 ||
        write_file(api, "/sd/destination.bin", buffer, 512u) != 0) {
        return fail(api, "rename accounting setup");
    }
    space.struct_size = sizeof(space);
    if (api->fs->space("/sd", &space) != MINI_OK || space.used_bytes != 1536u) {
        return fail(api, "rename accounting initial usage");
    }
    if (api->fs->rename("/sd/source.bin", "/sd/destination.bin") != MINI_OK) {
        return fail(api, "rename replacement");
    }

    mini_fs_stat_t st = {.struct_size = sizeof(st)};
    if (api->fs->stat("/sd/source.bin", &st) != MINI_ERR_NOT_FOUND) {
        return fail(api, "rename source removal");
    }
    st.struct_size = sizeof(st);
    if (api->fs->stat("/sd/destination.bin", &st) != MINI_OK || st.size != 1024u) {
        return fail(api, "rename destination replacement");
    }
    space.struct_size = sizeof(space);
    if (api->fs->space("/sd", &space) != MINI_OK ||
        space.used_bytes != 1024u || space.free_bytes != 3072u) {
        return fail(api, "rename replacement accounting");
    }
    if (api->fs->remove_file("/sd/destination.bin") != MINI_OK) {
        return fail(api, "rename replacement cleanup");
    }

    api->system->write("resource_probe: PASS\n");
    return 0;
}
