#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "minishell/api.h"

static void format_bytes(uint64_t bytes, char *out, size_t out_size)
{
    const uint64_t kib = 1024ull;
    const uint64_t mib = 1024ull * 1024ull;
    const uint64_t gib = 1024ull * 1024ull * 1024ull;
    uint64_t unit = 1u;
    char suffix = 'B';

    if (bytes >= gib) { unit = gib; suffix = 'G'; }
    else if (bytes >= mib) { unit = mib; suffix = 'M'; }
    else if (bytes >= kib) { unit = kib; suffix = 'K'; }

    if (unit == 1u) {
        (void)snprintf(out, out_size, "%lluB", (unsigned long long)bytes);
    } else {
        uint64_t whole = bytes / unit;
        uint64_t tenth = ((bytes % unit) * 10u) / unit;
        (void)snprintf(out, out_size, "%llu.%llu%c",
                       (unsigned long long)whole,
                       (unsigned long long)tenth, suffix);
    }
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->console == NULL || api->console->write == NULL ||
        api->fs == NULL || api->fs->space == NULL) {
        return 2;
    }
    if (argc > 2) {
        api->console->write("usage: df [path]\n");
        return 2;
    }

    const char *path = argc == 2 ? argv[1] : "/";
    mini_fs_space_t space = {.struct_size = sizeof(space)};
    if (api->fs->space(path, &space) != MINI_OK) {
        api->console->write("df: unavailable\n");
        return 1;
    }

    char used_text[24];
    char free_text[24];
    char total_text[24];
    char line[96];
    format_bytes(space.used_bytes, used_text, sizeof(used_text));
    format_bytes(space.free_bytes, free_text, sizeof(free_text));
    format_bytes(space.total_bytes, total_text, sizeof(total_text));
    (void)snprintf(line, sizeof(line), "used %s  free %s  total %s\n",
                   used_text, free_text, total_text);
    api->console->write(line);
    return 0;
}
