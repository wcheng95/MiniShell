#include <stdio.h>
#include <string.h>

#include "minishell/api.h"

static int fail(const mini_api_t *api, const char *message)
{
    if (api != NULL && api->system != NULL && api->system->write != NULL) {
        api->system->write("dir_probe: FAIL: ");
        api->system->write(message);
        api->system->write("\n");
    }
    return 1;
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->system == NULL || api->system->write == NULL || api->fs == NULL ||
        api->fs->struct_size < sizeof(mini_fs_api_t) ||
        api->fs->dir_open == NULL || api->fs->dir_read == NULL ||
        api->fs->dir_close == NULL) {
        return fail(api, "directory API unavailable");
    }

    const char *path = argc > 1 ? argv[1] : "/sd";
    mini_dir_t dir = MINI_DIR_INVALID;
    if (api->fs->dir_open(path, &dir) != MINI_OK || dir == MINI_DIR_INVALID) {
        return fail(api, "dir_open");
    }

    for (;;) {
        mini_fs_dir_entry_t entry = {.struct_size = sizeof(entry)};
        uint32_t has_entry = 0u;
        mini_result_t result = api->fs->dir_read(dir, &entry, &has_entry);
        if (result != MINI_OK) {
            (void)api->fs->dir_close(dir);
            return fail(api, "dir_read");
        }
        if (has_entry == 0u) break;

        char line[MINI_FS_NAME_MAX + 8u];
        const char prefix = entry.type == MINI_FS_TYPE_DIRECTORY ? 'D' : 'F';
        int count = snprintf(line, sizeof(line), "%c %s\n", prefix, entry.name);
        if (count < 0 || (size_t)count >= sizeof(line)) {
            (void)api->fs->dir_close(dir);
            return fail(api, "entry formatting");
        }
        api->system->write(line);
    }

    if (api->fs->dir_close(dir) != MINI_OK) {
        return fail(api, "dir_close");
    }

    api->system->write("dir_probe: PASS\n");
    return 0;
}
