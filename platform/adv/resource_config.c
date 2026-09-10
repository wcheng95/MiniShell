#include "platform_backend.h"

int minishell_platform_resource_limits(minishell_resource_limits_t *out_limits)
{
    if (out_limits == NULL) return -1;

    /* ADV does not impose an additional global storage quota. /flash is a
     * 2 MiB internal FATFS volume while optional /sd has independent physical
     * capacity, so one global limit would be misleading. */
    out_limits->memory_bytes = 0u;
    out_limits->storage_bytes = 0u;
    return 0;
}
