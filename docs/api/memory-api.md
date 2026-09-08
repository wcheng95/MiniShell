# MiniShell Memory API

Status: **implemented and exercised on the Linux reference backend.**

## Purpose

The Memory API gives a portable application ordinary dynamic memory without exposing the platform allocator, heap objects, PSRAM/SRAM layout, or SDK-specific allocation flags.

```text
application
    |
Memory API
    |
portable Memory service
    |
resource policy + app bookkeeping
    |
private backend allocator
```

The Memory service is the owner of application-visible allocation policy and per-app accounting.

## Service table

```c
typedef struct {
    uint32_t struct_size;
    mini_result_t (*alloc)(uint32_t size, void **out_ptr);
    mini_result_t (*realloc)(void *ptr, uint32_t new_size, void **out_ptr);
    mini_result_t (*free)(void *ptr);
    mini_result_t (*get_info)(mini_memory_info_t *out_info);
} mini_memory_api_t;
```

Allocation sizes are `uint32_t`; single allocations above 4 GiB are outside the current API's intended domain.

## Ownership

Every successful allocation belongs to the current foreground app until it is freed or automatically reclaimed when the app returns.

The portable service tracks allocations rather than letting application ownership depend on backend allocator metadata.

```text
app
  -> alloc A
  -> alloc B
  -> free B
  -> return with A live
  -> MiniShell reclaims A
```

Automatic cleanup is a safety net, not a replacement for normal app cleanup/error handling.

## `alloc()`

- `out_ptr` is required;
- zero-size allocation is invalid;
- success returns an ordinary C-aligned writable pointer;
- failure returns `MINI_ERR_NO_MEMORY`;
- contents are unspecified.

## `realloc()`

- pointer must be a live allocation owned by the current app;
- zero new size and NULL input pointer are invalid in the current API;
- failure preserves the original allocation;
- success may move the allocation and preserves the normal prefix contents.

## `free()`

- `free(NULL)` succeeds;
- unknown/interior/already-freed pointers return `MINI_ERR_INVALID`;
- successful free immediately releases ownership.

## Memory information

```c
#define MINI_MEM_INFO_APP_USAGE      (1ull << 0)
#define MINI_MEM_INFO_FREE_BYTES     (1ull << 1)
#define MINI_MEM_INFO_LARGEST_BLOCK  (1ull << 2)
```

`mini_memory_info_t` reports fields only when their `valid_fields` bit is set.

Exact per-app fields:

```text
app_allocated_bytes
app_allocation_count
```

These describe allocations made through the Memory API. They do not include application code, static data, stack, resident MiniShell memory, loader buffers, or platform-driver allocations.

## MiniShell resource budget

MiniShell may enforce a logical application-memory budget that is smaller than the host's physical memory.

On Linux the default budget is 8 MiB. `alloc()`/`realloc()` reject requests that would exceed it, and `get_info().free_bytes` reports the remaining MiniShell budget. `free` therefore describes the same domain applications can actually allocate from.

This is deliberate: reporting the PC's many gigabytes of free RAM would provide little useful information for applications intended to move to constrained targets.

A configured limit of zero means MiniShell does not impose its own logical cap. In that case the backend may report meaningful native allocator information if it can do so portably.

`largest_free_block` is optional. Linux deliberately does not invent a fake value when no meaningful allocator-domain equivalent exists.

## One-owner model

```text
application owns returned pointer for its lifetime
        |
Memory service owns accounting + limit + lifecycle
        |
backend owns allocator primitive
        |
OS/RTOS owns physical allocator implementation
```

Apps that directly call a host allocator have intentionally left the portable MiniShell contract.

## API evolution

`struct_size`, reserved fields, and `valid_fields` remain useful API design mechanisms, but they do not create a backward-compatibility guarantee. The Memory API may change incompatibly while MiniShell is still in active architectural development.

## Current verification

Linux tests cover representative:

- alloc/realloc/free;
- exact per-app bookkeeping;
- resource-limit rejection;
- automatic app teardown cleanup;
- `free` output against a configured budget.

## Deferred memory classes

No current portable need justifies:

```text
explicit alignment API
DMA-capable memory
executable memory
internal/external RAM selection
shared memory
mmap
memory protection
```

Add these as explicit capabilities rather than changing ordinary `alloc()` semantics without a deliberate API review.
