#include "platform_backend.h"

int minishell_platform_resource_limits(minishell_resource_limits_t *out_limits)
{
    if (out_limits == NULL) return -1;

    /* A1 does not impose an additional global quota. Physical heap/filesystem
     * capacity remains authoritative. This also avoids treating the 2 MiB
     * internal LittleFS partition as a cap on a future removable /sd. */
    out_limits->memory_bytes = 0u;
    out_limits->storage_bytes = 0u;
    return 0;
}
