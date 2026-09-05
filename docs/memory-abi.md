# MiniShell Memory ABI v0

Status: **Task 1 design contract; provisional until implementation and hardware ABI tests pass**

## 1. Purpose

The Memory ABI gives a MiniShell application ordinary dynamic read/write memory
without exposing the platform allocator, heap implementation, PSRAM/SRAM layout,
RTOS heap objects, or SDK-specific allocation flags.

```text
application
    |
    | MiniShell Memory ABI
    v
MiniShell memory service
    |
    | allocation policy + per-app bookkeeping
    v
platform allocator(s)
    |
    +-- internal RAM
    +-- PSRAM / external RAM
    `-- other implementation-defined pools
```

The default application contract is intentionally simple: an app asks for
ordinary memory suitable for normal C objects. MiniShell decides where that
memory comes from.

## 2. Scope of v0

Memory ABI v0 contains only:

```text
alloc
realloc
free
get_info
```

Deferred capabilities include:

```text
zeroed allocation convenience
explicit alignment
DMA-capable memory
executable memory
fast/internal memory selection
external-memory selection
shared memory
memory mapping
memory protection
```

These may be added later through append-only table extension or a new optional
sub-API without changing the v0 operations.

## 3. Allocation size

V0 uses fixed-width `uint32_t` allocation sizes rather than C `size_t`.

Reasons:

- the ABI remains explicit across RV32, RV64, Xtensa, ARM, and other targets;
- MiniShell is an MCU-oriented environment where a single allocation above 4 GiB
  is outside the intended v0 scope;
- current reference targets are far below that limit.

If a future MiniShell platform genuinely needs allocations larger than 4 GiB,
that capability can be added explicitly rather than silently changing the v0
contract.

## 4. Service table

```c
typedef struct {
    uint32_t struct_size;

    mini_result_t (*alloc)(
        uint32_t size,
        void **out_ptr);

    mini_result_t (*realloc)(
        void *ptr,
        uint32_t new_size,
        void **out_ptr);

    mini_result_t (*free)(
        void *ptr);

    mini_result_t (*get_info)(
        mini_memory_info_t *out_info);
} mini_memory_api_t;
```

The table is append-only after ABI stabilization.

## 5. `alloc()`

```c
mini_result_t alloc(uint32_t size, void **out_ptr);
```

Semantics:

- `out_ptr` must be non-NULL;
- `size == 0` returns `MINI_ERR_INVALID`;
- `*out_ptr` is set to NULL before the allocation attempt;
- success returns `MINI_OK` and a non-NULL pointer;
- allocation failure returns `MINI_ERR_NO_MEMORY`;
- returned memory is aligned suitably for ordinary C objects required by the
  target ABI;
- contents are unspecified and are not guaranteed to be zeroed;
- over-aligned, DMA-capable, executable, or otherwise special memory is not
  promised by v0.

The returned pointer is a native pointer because the application must directly
read and write the memory. MiniShell binaries are already architecture-specific,
so this does not violate the project's source-portability model.

## 6. `realloc()`

```c
mini_result_t realloc(void *ptr, uint32_t new_size, void **out_ptr);
```

Semantics:

- `ptr` must refer to the start of a live MiniShell allocation owned by the
  current application;
- `out_ptr` must be non-NULL;
- `new_size == 0` returns `MINI_ERR_INVALID`;
- `realloc(NULL, ...)` is not used as an alias for `alloc()` in v0;
- `*out_ptr` is set to NULL before the attempt;
- existing contents are preserved through
  `min(old_size, new_size)` bytes;
- the allocation may move;
- on success, the old pointer is invalid and `*out_ptr` is the live allocation;
- on failure, the original allocation remains valid, unchanged, and owned by the
  application;
- an invalid, interior, already-freed, or foreign pointer returns
  `MINI_ERR_INVALID`.

Keeping failure non-destructive is part of the ABI contract.

## 7. `free()`

```c
mini_result_t free(void *ptr);
```

Semantics:

- `free(NULL)` returns `MINI_OK`;
- a valid pointer must be the start of a live MiniShell allocation owned by the
  current application;
- success releases the allocation immediately;
- an interior pointer, foreign pointer, unknown pointer, or double free returns
  `MINI_ERR_INVALID`;
- once `free()` succeeds, the pointer must not be used again.

The Memory ABI can detect ownership mistakes in MiniShell-managed allocations,
but it cannot prevent arbitrary memory corruption in the shared MCU address
space.

## 8. Per-application ownership

Every successful MiniShell allocation is associated with the currently running
application context.

Conceptually:

```text
foreground app context
    |
    `-- allocations
        |-- pointer A / size
        |-- pointer B / size
        `-- pointer C / size
```

When an application returns normally, MiniShell releases any remaining
MiniShell-managed allocations before unloading the ELF.

Example:

```text
app starts
  -> alloc A
  -> alloc B
  -> free B
  -> app returns without freeing A
  -> MiniShell reclaims A
  -> ELF unloads
  -> shell resumes
```

This prevents ordinary forgotten `free()` calls from leaking memory across
application launches. It is cooperative lifecycle cleanup, not process
isolation.

## 9. Pointer ownership across other ABIs

Passing an application-owned pointer to another MiniShell service does **not**
transfer ownership unless that service explicitly documents a transfer.

Examples:

```c
api->fs->read(file, buffer, size, &n);
api->display->... /* future buffer-consuming call */
api->audio->...   /* future buffer-consuming call */
```

The called service may use the buffer for the documented duration of the call,
but the application remains responsible for the allocation afterward unless the
specific API states otherwise.

For synchronous Task 1 APIs, a service must not retain an application buffer
after the call returns unless that behavior is explicitly part of the service
contract.

## 10. Memory information

MiniShell should distinguish information it knows exactly from backend heap
information that may not be available on every platform.

```c
#define MINI_MEM_INFO_APP_USAGE      (1ull << 0)
#define MINI_MEM_INFO_FREE_BYTES     (1ull << 1)
#define MINI_MEM_INFO_LARGEST_BLOCK  (1ull << 2)

typedef struct {
    uint32_t struct_size;
    uint32_t reserved0;
    uint64_t valid_fields;

    uint64_t app_allocated_bytes;
    uint32_t app_allocation_count;
    uint32_t reserved1;

    uint64_t free_bytes;
    uint64_t largest_free_block;
} mini_memory_info_t;
```

The caller zero-initializes the structure and sets `struct_size` before calling
`get_info()`.

Semantics:

- `app_allocated_bytes` and `app_allocation_count` describe allocations owned by
  the current application through the Memory ABI;
- MiniShell can report those exactly because it owns the bookkeeping;
- `free_bytes` and `largest_free_block` refer to the default pool from which
  ordinary `alloc()` requests are satisfied;
- platforms that cannot report a backend field leave its validity bit clear;
- future fields are appended without changing existing offsets or meanings.

The ABI intentionally does not expose ESP-IDF heap capability classes, FreeRTOS
heap structures, PSRAM flags, or other backend-specific concepts.

## 11. What the Memory ABI does not represent

Memory ABI accounting covers dynamic allocations requested by an application
through `mini_memory_api_t`.

It does not represent:

```text
application ELF code
application static/global data
application stack
MiniShell resident memory
ELF loader internal allocations
platform-driver buffers
hardware-owned memory
```

Those remain runtime/platform implementation details.

## 12. Extension rule

The v0 table and structures follow the common MiniShell ABI evolution rules:

- append fields/functions only;
- never reorder or repurpose stable fields;
- use fixed-width public types;
- keep platform-specific memory classes below the boundary unless a real portable
  requirement appears;
- add special memory capabilities as explicit extensions rather than changing the
  meaning of ordinary `alloc()`.

## 13. ABI test requirements

`abi_memory.elf` should validate at least:

1. allocate, write, read back, and free;
2. multiple independent allocations;
3. grow with `realloc()` and verify preserved contents;
4. shrink with `realloc()` and verify preserved prefix;
5. failed allocation returns NULL plus `MINI_ERR_NO_MEMORY`;
6. failed `realloc()` preserves the original allocation;
7. `alloc(0)` and `realloc(..., 0)` fail cleanly;
8. `free(NULL)` succeeds;
9. double free and invalid/interior pointers fail cleanly;
10. `get_info()` reflects current app allocations;
11. intentionally leak one allocation, return from the ELF, relaunch the test,
    and verify teardown reclaimed it;
12. repeat the test many times without exhausting or destabilizing MiniShell.

The ABI remains provisional until these tests pass through the real runtime ABI
on the Tab5 reference platform.
