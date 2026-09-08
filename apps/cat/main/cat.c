#include <stddef.h>
#include <stdint.h>

#include "minishell/api.h"

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

#define CAT_BUFFER_SIZE 256u

static void say(const mini_system_api_t *system, const char *text)
{
    system->write(text);
}

static int cat_one(const mini_system_api_t *system,
                   const mini_fs_api_t *fs,
                   const char *path)
{
    mini_file_t file = MINI_FILE_INVALID;
    mini_result_t result = fs->open(path, MINI_FS_READ, &file);
    if (result != MINI_OK) {
        say(system, "cat: cannot open file\n");
        return 3;
    }

    char buffer[CAT_BUFFER_SIZE + 1u];
    int rc = 0;

    for (;;) {
        uint32_t count = 0u;
        result = fs->read(file, buffer, CAT_BUFFER_SIZE, &count);
        if (result != MINI_OK) {
            say(system, "cat: read error\n");
            rc = 4;
            break;
        }
        if (count == 0u) break;

        for (uint32_t i = 0u; i < count; ++i) {
            if (buffer[i] == '\0') {
                say(system, "cat: binary/NUL data is not supported by the current text output API\n");
                rc = 5;
                goto done;
            }
        }

        buffer[count] = '\0';
        system->write(buffer);
    }

done:
    result = fs->close(file);
    if (result != MINI_OK && rc == 0) {
        say(system, "cat: close error\n");
        rc = 6;
    }
    return rc;
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
        system->write == NULL ||
        fs->struct_size < FIELD_END(mini_fs_api_t, read) ||
        fs->open == NULL || fs->close == NULL || fs->read == NULL) {
        return 2;
    }

    if (argc != 2) {
        say(system, "usage: cat <path>\n");
        return 1;
    }

    return cat_one(system, fs, argv[1]);
}
