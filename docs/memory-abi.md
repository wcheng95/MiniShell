# MiniShell Memory ABI v0

Status: **Task 1 design contract; provisional until implementation, unit tests, ELF integration, and hardware validation pass**

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

The default contract is intentionally simple: the app asks for ordinary memory
suitable for normal C objects. MiniShell decides which eligible memory pool
supplies it.

## 2. Scope of V0

Memory ABI V0 contains:

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
sub-API without changing V0 semantics.

## 3. Allocation size

V0 uses fixed-width `uint32_t` allocation sizes rather than C `size_t`.

Reasons:

- the public ABI remains explicit across RV32, RV64, Xtensa, ARM, and other
  targets;
- a single allocation above 4 GiB is outside the intended MCU-oriented V0 scope;
- current reference targets are far below that limit.

If a future platform genuinely needs larger individual allocations, add an
explicit new capability rather than silently changing V0.

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

- `out_ptr` is non-NULL;
- `size == 0` returns `MINI_ERR_INVALID`;
- `*out_ptr` is set to NULL before the attempt;
- success returns `MINI_OK` plus a non-NULL native pointer;
- allocation failure returns `MINI_ERR_NO_MEMORY`;
- returned memory is aligned suitably for ordinary C objects required by the
  target ABI;
- contents are unspecified and are not guaranteed zeroed;
- over-aligned, DMA-capable, executable, or otherwise special memory is not
  promised by V0.

The pointer is native because the application must directly read/write it.
MiniShell binaries are already architecture-specific, so this is compatible with
source portability.

## 6. `realloc()`

```c
mini_result_t realloc(
    void *ptr,
    uint32_t new_size,
    void **out_ptr);
```

Semantics:

- `ptr` refers to the start of a live MiniShell allocation owned by the current
  app;
- `out_ptr` is non-NULL;
- `new_size == 0` returns `MINI_ERR_INVALID`;
- `realloc(NULL, ...)` is not an alias for `alloc()` in V0;
- `*out_ptr` is set to NULL before the attempt;
- existing contents are preserved through `min(old_size, new_size)` bytes;
- the allocation may move;
- on success, the old pointer is invalid and `*out_ptr` is the live allocation;
- on failure, the original allocation remains valid, unchanged, and owned by the
  application;
- invalid, interior, already-freed, or foreign pointers return
  `MINI_ERR_INVALID`.

Non-destructive realloc failure is part of the V0 contract.

## 7. `free()`

```c
mini_result_t free(void *ptr);
```

Semantics:

- `free(NULL)` returns `MINI_OK`;
- a valid pointer is the start of a live MiniShell allocation owned by the
  current app;
- success releases the allocation immediately;
- an interior, foreign, unknown, or already-freed pointer returns
  `MINI_ERR_INVALID`;
- after successful free, the pointer must not be used again.

Memory ABI bookkeeping can detect ownership mistakes in MiniShell-managed
allocations, but it cannot prevent arbitrary memory corruption in the shared MCU
address space.

## 8. Per-application ownership

Every successful allocation is associated with the current app context.

```text
foreground app context
    `-- allocations
        |-- pointer A / requested size
        |-- pointer B / requested size
        `-- pointer C / requested size
```

When the app returns normally, MiniShell releases remaining Memory-ABI
allocations before unloading the ELF.

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

This is cooperative lifecycle cleanup, not process isolation.

## 9. Pointer ownership across other ABIs

Passing an app-owned pointer to another synchronous MiniShell service does **not**
transfer ownership unless that service explicitly says otherwise.

Examples:

```c
api->fs->read(file, buffer, size, &n);
api->display->... /* future buffer-consuming call */
api->audio->...   /* future buffer-consuming call */
```

The called service may use the buffer for the documented duration of the call.
For Task 1 synchronous APIs it must not retain the buffer after return unless a
specific contract explicitly defines retention.

## 10. Memory information

MiniShell distinguishes exact per-app accounting from platform allocator-domain
information that may not be available everywhere.

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

The caller follows the common extensible-structure rules in
`docs/abi-foundation.md`: zero-initialize, set `struct_size`, and leave reserved
input fields zero.

### Exact app accounting

`app_allocated_bytes` is the sum of the **requested live allocation sizes** owned
by the current app through this ABI.

It does not include:

```text
allocator metadata
alignment padding
heap headers
platform bookkeeping overhead
ELF/static/stack memory
MiniShell resident memory
```

`app_allocation_count` is the number of live Memory-ABI allocations owned by the
current app.

Because MiniShell tracks requested sizes itself, these two values can be exact.

### Ordinary-allocation domain information

`free_bytes` and `largest_free_block` describe the memory domain eligible to
satisfy ordinary `alloc()` calls.

That domain may consist of one physical pool or several eligible pools. The ABI
does **not** imply one platform heap named "the default pool" and does not expose
which physical pool would satisfy a future allocation.

Semantics:

- `free_bytes` is the implementation's best portable measure of currently free
  ordinary-allocation capacity across the eligible domain;
- `largest_free_block` is the largest single ordinary allocation the
  implementation currently believes it can satisfy, subject to normal race/
  allocator changes between calls;
- a platform that cannot report one of these meaningfully leaves its validity bit
  clear;
- exact values may change immediately after `get_info()` returns.

The ABI does not expose ESP-IDF heap capability classes, PSRAM flags, FreeRTOS
heap structures, or backend allocator identities.

## 11. What Memory ABI accounting does not represent

Memory ABI accounting covers dynamic allocations explicitly requested by the app
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

V0 follows the common MiniShell ABI evolution rules:

- append fields/functions only;
- never reorder/repurpose established fields;
- use fixed-width public types;
- keep platform-specific memory classes below the boundary unless a real portable
  requirement appears;
- add special-memory capabilities explicitly rather than changing ordinary
  `alloc()` semantics.

## 13. Execution context

Memory calls follow the common V0 application-context rule. They are not promised
ISR-safe or generally reentrant unless a future extension explicitly says so.

## 14. Verification requirements

### 14.1 Unit tests — primary

The Memory service must have comprehensive unit tests with a controllable fake
allocator/backend so success and failure paths can be forced deterministically.
They should verify at least:

1. allocate/write/read/free;
2. multiple independent allocations;
3. ordinary alignment guarantees;
4. `alloc(0)` and NULL output-pointer handling;
5. deterministic allocation failure and `MINI_ERR_NO_MEMORY`;
6. grow/shrink `realloc()` and preserved contents;
7. moved and in-place realloc success;
8. failed realloc preserving the original allocation and data;
9. `realloc(NULL, ...)` and `realloc(..., 0)` invalid behavior;
10. `free(NULL)` success;
11. invalid, interior, foreign, and double-free detection;
12. exact requested-byte and allocation-count bookkeeping;
13. allocator-domain validity-bit behavior;
14. per-app ownership isolation in the bookkeeping layer;
15. teardown reclaiming intentionally leaked app allocations;
16. repeated allocate/realloc/free/teardown cycles;
17. `struct_size` handling for `mini_memory_info_t` including too-small and
    prefix-sized callers.

Where useful, randomized operation sequences may be used in addition to explicit
cases to stress bookkeeping invariants.

### 14.2 Runtime-loaded ELF integration test

`abi_memory.elf` should be a smaller integration test that proves the public
binary boundary. It should cover:

1. service/table discovery and V0 field presence;
2. one representative alloc/read/write/free path;
3. representative realloc behavior;
4. one representative invalid/error case;
5. `get_info()` through the public structure;
6. returning with one live allocation so app teardown can be observed;
7. repeated launch/exit without leak or runtime damage.

It does not need to duplicate the exhaustive unit suite.

### 14.3 Hardware/platform validation

Validate that the Tab5 allocator-domain implementation behaves correctly with its
real eligible RAM pools and that repeated real allocations do not destabilize the
runtime.

The ABI remains provisional until the unit suite, focused ELF integration test,
and required platform validation all pass.
