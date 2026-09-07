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
    (void)argv;
    const mini_api_t *api = mini_api_get();
    if (argc != 1) return 2;
    if (api == NULL || api->system == NULL || api->system->write == NULL ||
        api->memory == NULL || api->memory->get_info == NULL) {
        return 2;
    }

    mini_memory_info_t info = {.struct_size = sizeof(info)};
    if (api->memory->get_info(&info) != MINI_OK ||
        (info.valid_fields & MINI_MEM_INFO_APP_USAGE) == 0u ||
        (info.valid_fields & MINI_MEM_INFO_FREE_BYTES) == 0u) {
        api->system->write("free: unavailable\n");
        return 1;
    }

    uint64_t total = info.app_allocated_bytes + info.free_bytes;
    char used_text[24];
    char free_text[24];
    char total_text[24];
    char line[96];
    format_bytes(info.app_allocated_bytes, used_text, sizeof(used_text));
    format_bytes(info.free_bytes, free_text, sizeof(free_text));
    format_bytes(total, total_text, sizeof(total_text));
    (void)snprintf(line, sizeof(line), "used %s  free %s  total %s\n",
                   used_text, free_text, total_text);
    api->system->write(line);
    return 0;
}
