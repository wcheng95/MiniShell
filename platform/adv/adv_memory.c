#include <stdbool.h>
#include <stdint.h>

#include "esp_heap_caps.h"

#include "adv_internal.h"

void *adv_memory_alloc(void *ctx, uint32_t size)
{
    (void)ctx;
    return heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

void *adv_memory_realloc(void *ctx, void *ptr, uint32_t new_size)
{
    (void)ctx;
    return heap_caps_realloc(ptr, new_size, MALLOC_CAP_8BIT);
}

void adv_memory_free(void *ctx, void *ptr)
{
    (void)ctx;
    heap_caps_free(ptr);
}

bool adv_memory_get_info(void *ctx, uint64_t *free_bytes, uint64_t *largest_free_block)
{
    (void)ctx;
    if (free_bytes == NULL || largest_free_block == NULL) return false;
    *free_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    *largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    return true;
}
