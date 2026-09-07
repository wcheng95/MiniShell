#include <stddef.h>
#include <string.h>

#include "minishell/api.h"

static void write_error(const mini_api_t *api, const char *text)
{
    if (api != NULL && api->system != NULL && api->system->write != NULL) {
        api->system->write(text);
    }
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->abi_version != MINISHELL_ABI_VERSION ||
        api->system == NULL || api->system->write == NULL ||
        api->fs == NULL || api->fs->struct_size < sizeof(mini_fs_api_t) ||
        api->fs->dir_open == NULL || api->fs->dir_read == NULL ||
        api->fs->dir_close == NULL) {
        write_error(api, "ls: directory service unavailable\n");
        return 2;
    }

    if (argc > 2) {
        api->system->write("usage: ls [path]\n");
        return 2;
    }

    const char *path = argc == 2 ? argv[1] : "/";
    const int root_listing = strcmp(path, "/") == 0;
    mini_dir_t dir = MINI_DIR_INVALID;
    mini_result_t result = api->fs->dir_open(path, &dir);
    if (result != MINI_OK) {
        api->system->write("ls: cannot open directory\n");
        return 1;
    }

    int status = 0;
    for (;;) {
        mini_fs_dir_entry_t entry = {.struct_size = sizeof(entry)};
        uint32_t has_entry = 0u;
        result = api->fs->dir_read(dir, &entry, &has_entry);
        if (result != MINI_OK) {
            api->system->write("ls: directory read failed\n");
            status = 1;
            break;
        }
        if (has_entry == 0u) break;

        /* Match familiar ls behavior: hidden names are omitted by default. */
        if (entry.name[0] == '.') continue;

        /* Root entries are shown as complete MiniShell paths (/sd, /flash),
         * matching normal Linux mount-point naming and usable directly in
         * subsequent filesystem commands. */
        if (root_listing) api->system->write("/");
        api->system->write(entry.name);
        if (!root_listing && entry.type == MINI_FS_TYPE_DIRECTORY) {
            api->system->write("/");
        }
        api->system->write("\n");
    }

    if (api->fs->dir_close(dir) != MINI_OK && status == 0) {
        api->system->write("ls: directory close failed\n");
        status = 1;
    }
    return status;
}
