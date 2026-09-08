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
    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
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
    if (fs->struct_size < FIELD_END(mini_fs_api_t, rename) ||
        fs->stat == NULL || fs->rename == NULL) {
        say(system, "mv: filesystem rename is unavailable\n");
        return 2;
    }

    if (argc != 3) {
        say(system, "usage: mv <source> <destination>\n");
        return 1;
    }

    mini_fs_stat_t st = {.struct_size = sizeof(st)};
    mini_result_t result = fs->stat(argv[1], &st);
    if (result != MINI_OK) {
        say(system, "mv: source not found or inaccessible\n");
        return 3;
    }
    if (st.type != MINI_FS_TYPE_FILE) {
        say(system, "mv: source is not a regular file\n");
        return 4;
    }

    result = fs->rename(argv[1], argv[2]);
    if (result == MINI_OK) return 0;
    if (result == MINI_ERR_EXISTS) {
        say(system, "mv: destination already exists\n");
        return 5;
    }
    if (result == MINI_ERR_NOT_FOUND) {
        say(system, "mv: source or destination parent not found\n");
        return 6;
    }
    if (result == MINI_ERR_NOT_DIR) {
        say(system, "mv: destination parent is not a directory\n");
        return 7;
    }
    if (result == MINI_ERR_ACCESS) {
        say(system, "mv: access denied\n");
        return 8;
    }
    if (result == MINI_ERR_UNSUPPORTED) {
        say(system, "mv: rename is unsupported\n");
        return 9;
    }

    say(system, "mv: rename failed\n");
    return 10;
}
