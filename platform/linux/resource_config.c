#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "platform_backend.h"

#define DEFAULT_MEMORY_LIMIT  (8ull * 1024ull * 1024ull)
#define DEFAULT_STORAGE_LIMIT (64ull * 1024ull * 1024ull)

static int parse_limit(const char *text, uint64_t fallback, uint64_t *out_value)
{
    if (out_value == NULL) return -EINVAL;
    if (text == NULL || text[0] == '\0') {
        *out_value = fallback;
        return 0;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || end == text) return -EINVAL;

    uint64_t multiplier = 1u;
    if (*end != '\0') {
        char suffix = *end++;
        if (suffix == 'K' || suffix == 'k') multiplier = 1024ull;
        else if (suffix == 'M' || suffix == 'm') multiplier = 1024ull * 1024ull;
        else if (suffix == 'G' || suffix == 'g') multiplier = 1024ull * 1024ull * 1024ull;
        else return -EINVAL;
        if (*end != '\0') return -EINVAL;
    }

    if (value != 0ull && value > UINT64_MAX / multiplier) return -ERANGE;
    *out_value = (uint64_t)value * multiplier;
    return 0;
}

int minishell_platform_resource_limits(minishell_resource_limits_t *out_limits)
{
    if (out_limits == NULL) return -EINVAL;

    uint64_t memory = 0u;
    uint64_t storage = 0u;
    int ret = parse_limit(getenv("MINISHELL_MEMORY_LIMIT"), DEFAULT_MEMORY_LIMIT, &memory);
    if (ret != 0) return ret;
    ret = parse_limit(getenv("MINISHELL_STORAGE_LIMIT"), DEFAULT_STORAGE_LIMIT, &storage);
    if (ret != 0) return ret;

    out_limits->memory_bytes = memory;
    out_limits->storage_bytes = storage;
    return 0;
}
