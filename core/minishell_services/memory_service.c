#include <string.h>

#include "services_internal.h"

#define MINI_MEMORY_MAX_TRACKED 128u

typedef struct {
    void *ptr;
    uint32_t requested_size;
} memory_slot_t;

static memory_slot_t s_slots[MINI_MEMORY_MAX_TRACKED];
static bool s_available;

static uint64_t current_usage(uint32_t *out_count)
{
    uint64_t bytes = 0u;
    uint32_t count = 0u;
    for (uint32_t i = 0; i < MINI_MEMORY_MAX_TRACKED; ++i) {
        if (s_slots[i].ptr != NULL) {
            bytes += s_slots[i].requested_size;
            ++count;
        }
    }
    if (out_count != NULL) *out_count = count;
    return bytes;
}

static bool budget_allows(uint64_t used, uint64_t requested_total)
{
    uint64_t limit = minishell_memory_limit_bytes();
    if (limit == 0u) return true;
    return requested_total <= limit && used <= limit;
}

static memory_slot_t *find_slot(void *ptr)
{
    for (uint32_t i = 0; i < MINI_MEMORY_MAX_TRACKED; ++i) {
        if (s_slots[i].ptr == ptr) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static memory_slot_t *find_free_slot(void)
{
    for (uint32_t i = 0; i < MINI_MEMORY_MAX_TRACKED; ++i) {
        if (s_slots[i].ptr == NULL) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static mini_result_t memory_alloc(uint32_t size, void **out_ptr)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (out_ptr == NULL) {
        return MINI_ERR_INVALID;
    }
    *out_ptr = NULL;
    if (!s_available || size == 0u) {
        return size == 0u ? MINI_ERR_INVALID : MINI_ERR_UNSUPPORTED;
    }

    uint64_t used = current_usage(NULL);
    if ((uint64_t)size > UINT64_MAX - used || !budget_allows(used, used + size)) {
        return MINI_ERR_NO_MEMORY;
    }

    memory_slot_t *slot = find_free_slot();
    if (slot == NULL) {
        return MINI_ERR_NO_MEMORY;
    }

    void *ptr = port->memory_alloc(port->ctx, size);
    if (ptr == NULL) {
        return MINI_ERR_NO_MEMORY;
    }

    slot->ptr = ptr;
    slot->requested_size = size;
    *out_ptr = ptr;
    return MINI_OK;
}

static mini_result_t memory_realloc(void *ptr, uint32_t new_size, void **out_ptr)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (out_ptr == NULL) {
        return MINI_ERR_INVALID;
    }
    *out_ptr = NULL;
    if (!s_available) {
        return MINI_ERR_UNSUPPORTED;
    }
    if (ptr == NULL || new_size == 0u) {
        return MINI_ERR_INVALID;
    }

    memory_slot_t *slot = find_slot(ptr);
    if (slot == NULL) {
        return MINI_ERR_INVALID;
    }

    uint64_t used = current_usage(NULL);
    uint64_t without_old = used - slot->requested_size;
    if ((uint64_t)new_size > UINT64_MAX - without_old ||
        !budget_allows(used, without_old + new_size)) {
        return MINI_ERR_NO_MEMORY;
    }

    void *new_ptr = port->memory_realloc(port->ctx, ptr, new_size);
    if (new_ptr == NULL) {
        return MINI_ERR_NO_MEMORY;
    }

    slot->ptr = new_ptr;
    slot->requested_size = new_size;
    *out_ptr = new_ptr;
    return MINI_OK;
}

static mini_result_t memory_free(void *ptr)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!s_available) {
        return MINI_ERR_UNSUPPORTED;
    }
    if (ptr == NULL) {
        return MINI_OK;
    }

    memory_slot_t *slot = find_slot(ptr);
    if (slot == NULL) {
        return MINI_ERR_INVALID;
    }

    port->memory_free(port->ctx, ptr);
    slot->ptr = NULL;
    slot->requested_size = 0u;
    return MINI_OK;
}

static mini_result_t memory_get_info(mini_memory_info_t *out_info)
{
    const uint32_t v0_size = MINI_FIELD_END(mini_memory_info_t, largest_free_block);
    const minishell_services_port_t *port = minishell_services_port();

    if (!s_available) {
        return MINI_ERR_UNSUPPORTED;
    }
    if (out_info == NULL || out_info->struct_size < v0_size) {
        return MINI_ERR_INVALID;
    }

    uint32_t caller_size = out_info->struct_size;
    memset((uint8_t *)out_info + sizeof(out_info->struct_size), 0,
           v0_size - sizeof(out_info->struct_size));
    out_info->struct_size = caller_size;

    uint32_t count = 0u;
    uint64_t bytes = current_usage(&count);
    out_info->app_allocated_bytes = bytes;
    out_info->app_allocation_count = count;
    out_info->valid_fields |= MINI_MEM_INFO_APP_USAGE;

    uint64_t limit = minishell_memory_limit_bytes();
    if (limit != 0u) {
        out_info->free_bytes = bytes < limit ? limit - bytes : 0u;
        out_info->valid_fields |= MINI_MEM_INFO_FREE_BYTES;
        return MINI_OK;
    }

    if (port->memory_get_info != NULL) {
        uint64_t free_bytes = 0u;
        uint64_t largest = 0u;
        if (port->memory_get_info(port->ctx, &free_bytes, &largest)) {
            out_info->free_bytes = free_bytes;
            out_info->largest_free_block = largest;
            out_info->valid_fields |= MINI_MEM_INFO_FREE_BYTES;
            out_info->valid_fields |= MINI_MEM_INFO_LARGEST_BLOCK;
        }
    }

    return MINI_OK;
}

static const mini_memory_api_t s_memory_api = {
    .struct_size = sizeof(mini_memory_api_t),
    .alloc = memory_alloc,
    .realloc = memory_realloc,
    .free = memory_free,
    .get_info = memory_get_info,
};

void minishell_memory_service_configure(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    memset(s_slots, 0, sizeof(s_slots));
    s_available = port->memory_alloc != NULL &&
                  port->memory_realloc != NULL &&
                  port->memory_free != NULL;
}

void minishell_memory_service_app_begin(void)
{
    minishell_memory_service_app_end();
}

void minishell_memory_service_app_end(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (port->memory_free == NULL) {
        memset(s_slots, 0, sizeof(s_slots));
        return;
    }

    for (uint32_t i = 0; i < MINI_MEMORY_MAX_TRACKED; ++i) {
        if (s_slots[i].ptr != NULL) {
            port->memory_free(port->ctx, s_slots[i].ptr);
            s_slots[i].ptr = NULL;
            s_slots[i].requested_size = 0u;
        }
    }
}

bool minishell_memory_service_available(void)
{
    return s_available;
}

const mini_memory_api_t *minishell_memory_service_api(void)
{
    return &s_memory_api;
}
