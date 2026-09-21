/* Host backing for the MiniShell Memory API, with failure/leak instrumentation. */
#include <stdlib.h>
static void *memory_live;
static uint32_t memory_bytes;
static unsigned memory_calls, memory_fail_at, memory_frees;
static void (*memory_before_free)(void);
static mini_result_t memory_resize(void *old, uint32_t bytes, void **out)
{
    assert(old == memory_live); *out = NULL;
    assert(bytes == (old ? memory_bytes * 2U : 64U * sizeof(keyer_op_entry_t)));
    if (++memory_calls == memory_fail_at) return MINI_ERR_NO_MEMORY;
    void *next = malloc(bytes); assert(next);
    if (old) { memcpy(next, old, memory_bytes < bytes ? memory_bytes : bytes); free(old); }
    memory_live = next; memory_bytes = bytes; *out = next; return MINI_OK;
}
static mini_result_t memory_alloc(uint32_t bytes, void **out) { return memory_resize(NULL, bytes, out); }
static mini_result_t memory_release(void *pointer)
{
    assert(pointer && pointer == memory_live);
    if (memory_before_free) memory_before_free();
    free(pointer); memory_live = NULL; memory_bytes = 0; ++memory_frees; return MINI_OK;
}
static const mini_memory_api_t memory_api = {
    .struct_size = sizeof(memory_api), .alloc = memory_alloc, .realloc = memory_resize, .free = memory_release
};
static void memory_reset(void)
{
    assert(!memory_live); memory_calls = memory_fail_at = memory_frees = memory_bytes = 0;
    memory_before_free = NULL;
}
