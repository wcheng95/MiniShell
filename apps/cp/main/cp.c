#include <stddef.h>
#include <stdint.h>

#include "minishell/api.h"
#include "cp_copy.h"

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

static void say(const mini_system_api_t *system, const char *text)
{
    system->write(text);
}

static void report_result(const mini_system_api_t *system, cp_copy_result_t result)
{
    switch (result) {
    case CP_COPY_OK:
        return;
    case CP_COPY_ERR_SAME_PATH:
        say(system, "cp: source and destination are the same path\n");
        return;
    case CP_COPY_ERR_SOURCE_STAT:
        say(system, "cp: cannot stat source\n");
        return;
    case CP_COPY_ERR_SOURCE_IS_DIR:
        say(system, "cp: source is not a regular file\n");
        return;
    case CP_COPY_ERR_DEST_STAT:
        say(system, "cp: cannot stat destination\n");
        return;
    case CP_COPY_ERR_DEST_IS_DIR:
        say(system, "cp: destination is not a regular file path\n");
        return;
    case CP_COPY_ERR_OPEN_SOURCE:
        say(system, "cp: cannot open source\n");
        return;
    case CP_COPY_ERR_OPEN_DEST:
        say(system, "cp: cannot open destination\n");
        return;
    case CP_COPY_ERR_READ:
        say(system, "cp: read error\n");
        return;
    case CP_COPY_ERR_WRITE:
        say(system, "cp: write error\n");
        return;
    case CP_COPY_ERR_SYNC:
        say(system, "cp: sync error\n");
        return;
    case CP_COPY_ERR_CLOSE_DEST:
        say(system, "cp: destination close error\n");
        return;
    case CP_COPY_ERR_CLOSE_SOURCE:
        say(system, "cp: source close error\n");
        return;
    default:
        say(system, "cp: invalid operation\n");
        return;
    }
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
        fs->struct_size < FIELD_END(mini_fs_api_t, stat) ||
        fs->open == NULL || fs->close == NULL || fs->read == NULL ||
        fs->write == NULL || fs->sync == NULL || fs->stat == NULL) {
        return 2;
    }

    if (argc != 3) {
        say(system, "usage: cp <source> <destination>\n");
        return 1;
    }

    cp_copy_result_t result = cp_copy_file(fs, argv[1], argv[2]);
    report_result(system, result);
    return result == CP_COPY_OK ? 0 : 1;
}
