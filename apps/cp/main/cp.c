#include <stddef.h>
#include <stdint.h>

#include "minishell/api.h"
#include "cp_copy.h"

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

static void say(const mini_console_api_t *console, const char *text)
{
    console->write(text);
}

static void report_result(const mini_console_api_t *console, cp_copy_result_t result)
{
    switch (result) {
    case CP_COPY_OK:
        return;
    case CP_COPY_ERR_SAME_PATH:
        say(console, "cp: source and destination are the same path\n");
        return;
    case CP_COPY_ERR_SOURCE_STAT:
        say(console, "cp: cannot stat source\n");
        return;
    case CP_COPY_ERR_SOURCE_IS_DIR:
        say(console, "cp: source is not a regular file\n");
        return;
    case CP_COPY_ERR_DEST_STAT:
        say(console, "cp: cannot stat destination\n");
        return;
    case CP_COPY_ERR_DEST_IS_DIR:
        say(console, "cp: destination is not a regular file path\n");
        return;
    case CP_COPY_ERR_OPEN_SOURCE:
        say(console, "cp: cannot open source\n");
        return;
    case CP_COPY_ERR_OPEN_DEST:
        say(console, "cp: cannot open destination\n");
        return;
    case CP_COPY_ERR_READ:
        say(console, "cp: read error\n");
        return;
    case CP_COPY_ERR_WRITE:
        say(console, "cp: write error\n");
        return;
    case CP_COPY_ERR_SYNC:
        say(console, "cp: sync error\n");
        return;
    case CP_COPY_ERR_CLOSE_DEST:
        say(console, "cp: destination close error\n");
        return;
    case CP_COPY_ERR_CLOSE_SOURCE:
        say(console, "cp: source close error\n");
        return;
    default:
        say(console, "cp: invalid operation\n");
        return;
    }
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
        console->write == NULL ||
        fs->struct_size < FIELD_END(mini_fs_api_t, stat) ||
        fs->open == NULL || fs->close == NULL || fs->read == NULL ||
        fs->write == NULL || fs->sync == NULL || fs->stat == NULL) {
        return 2;
    }

    if (argc != 3) {
        say(console, "usage: cp <source> <destination>\n");
        return 1;
    }

    cp_copy_result_t result = cp_copy_file(fs, argv[1], argv[2]);
    report_result(console, result);
    return result == CP_COPY_OK ? 0 : 1;
}
