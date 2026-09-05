#include <stddef.h>

#include "minishell/api.h"

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

static void say(const mini_system_api_t *system, const char *text)
{
    system->write(text);
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->abi_version != MINISHELL_ABI_VERSION ||
        api->struct_size < FIELD_END(mini_api_t, fs) ||
        api->system == NULL || api->fs == NULL) {
        return 2;
    }

    const mini_system_api_t *system = api->system;
    const mini_fs_api_t *fs = api->fs;
    if (system->struct_size < FIELD_END(mini_system_api_t, write) ||
        system->write == NULL) {
        return 2;
    }
    if (fs->struct_size < FIELD_END(mini_fs_api_t, remove_file) ||
        fs->remove_file == NULL) {
        say(system, "rm: file removal is unavailable\n");
        return 2;
    }

    if (argc != 2) {
        say(system, "usage: rm <file>\n");
        return 1;
    }

    mini_result_t result = fs->remove_file(argv[1]);
    if (result == MINI_OK) return 0;
    if (result == MINI_ERR_NOT_FOUND) {
        say(system, "rm: file not found\n");
        return 3;
    }
    if (result == MINI_ERR_IS_DIR) {
        say(system, "rm: path is a directory; use rmdir for an empty directory\n");
        return 4;
    }
    if (result == MINI_ERR_ACCESS) {
        say(system, "rm: access denied\n");
        return 5;
    }
    if (result == MINI_ERR_UNSUPPORTED) {
        say(system, "rm: file removal is unsupported\n");
        return 6;
    }

    say(system, "rm: remove failed\n");
    return 7;
}
