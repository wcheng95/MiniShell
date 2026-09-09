#include <stddef.h>

#include "minishell/api.h"

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

static void say(const mini_console_api_t *console, const char *text)
{
    console->write(text);
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->struct_size < FIELD_END(mini_api_t, fs) ||
        api->console == NULL || api->fs == NULL) {
        return 2;
    }

    const mini_console_api_t *console = api->console;
    const mini_fs_api_t *fs = api->fs;
    if (console->struct_size < FIELD_END(mini_console_api_t, write) ||
        console->write == NULL) {
        return 2;
    }
    if (fs->struct_size < FIELD_END(mini_fs_api_t, remove_file) ||
        fs->remove_file == NULL) {
        say(console, "rm: file removal is unavailable\n");
        return 2;
    }

    if (argc != 2) {
        say(console, "usage: rm <file>\n");
        return 1;
    }

    mini_result_t result = fs->remove_file(argv[1]);
    if (result == MINI_OK) return 0;
    if (result == MINI_ERR_NOT_FOUND) {
        say(console, "rm: file not found\n");
        return 3;
    }
    if (result == MINI_ERR_IS_DIR) {
        say(console, "rm: path is a directory; use rmdir for an empty directory\n");
        return 4;
    }
    if (result == MINI_ERR_ACCESS) {
        say(console, "rm: access denied\n");
        return 5;
    }
    if (result == MINI_ERR_UNSUPPORTED) {
        say(console, "rm: file removal is unsupported\n");
        return 6;
    }

    say(console, "rm: remove failed\n");
    return 7;
}
