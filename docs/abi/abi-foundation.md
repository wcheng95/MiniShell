# MiniShell ABI Foundation

Status: **foundational cross-ABI contract; append-only growth active**

MiniShell exists to provide a small, reusable, platform-neutral ABI between MCU
applications and platform implementations. The ABI is the main architectural
product of MiniShell, not an implementation detail.

This document owns the rules that apply across all application-facing services.
Detailed service contracts live in their own files and are not duplicated here.

## 1. Task 1 service set

The six foundational Task 1 ABIs are:

```text
system
memory
filesystem
time/location
display
input
```

Canonical detailed contracts:

- `docs/system-abi.md`
- `docs/memory-abi.md`
- `docs/filesystem-abi.md`
- `docs/time-location-abi.md`
- `docs/display-abi.md`
- `docs/input-abi.md`

`console` is not a foundational ABI. It is a higher-level composition of output
and input behavior.

## 2. Layering

```text
application
    |
    | MiniShell ABI
    v
MiniShell resident services
    |
    | platform-private boundary
    v
SDK / RTOS / bare-metal support / drivers
    |
    v
hardware
```

Normal applications do not depend on ESP-IDF, FATFS, FreeRTOS handles, M5Stack
objects, raw peripheral registers, or other platform-private types.

## 3. Top-level API

The established top-level shape is:

```c
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;

    const mini_system_api_t        *system;
    const mini_memory_api_t        *memory;
    const mini_fs_api_t            *fs;
    const mini_time_location_api_t *time_location;
    const mini_display_api_t       *display;
    const mini_input_api_t         *input;
} mini_api_t;
```

Runtime binding is through:

```c
const mini_api_t *mini_api_get(void);
```

`system` is mandatory for a conforming MiniShell runtime. Other top-level service
pointers may be NULL on a platform that does not provide that service.

## 4. API acquisition and lifetime

An application obtains the resident API through `mini_api_get()`.

The returned `mini_api_t` pointer and any resident service-table pointers reached
through it:

- are owned by MiniShell;
- are read-only from the application's point of view;
- remain valid for the duration of the application's execution;
- must not be freed, modified, or persisted for use by a later app instance.

The current native model remains ordinary `main(argc, argv)` plus
`mini_api_get()`.

## 5. ABI version meaning

`abi_version` identifies an incompatible ABI generation. It is not a feature
counter and is not incremented for ordinary append-only compatible growth.

Compatible additions are discovered through `struct_size`, capability bits, and
optional sub-APIs. An incompatible change that cannot preserve the established
prefix requires a new ABI generation.

Task 6 demonstrates this rule: Filesystem namespace functions are appended to the
existing Filesystem table without changing the top-level ABI generation.

## 6. Append-only tables

Every public service function table begins with:

```c
uint32_t struct_size;
```

After stabilization, tables grow only by appending fields or function pointers.
Existing fields are never reordered, removed, repurposed, or given incompatible
semantics.

The same rule applies to `mini_api_t`: new top-level service pointers are appended
rather than inserted into the established prefix.

## 7. Correct `struct_size` use

An application must not require `struct_size >= sizeof(the newest struct)` unless
it truly requires every field in that newest struct.

Instead, it checks that `struct_size` reaches the end of the specific field it
plans to access.

Conceptually:

```c
field_present = struct_size >= offset_of_field + sizeof(field);
```

This rule is essential for backward-compatible extension: an old resident table
may provide the prefix an app needs even when it is smaller than a newer header's
full structure.

## 8. Caller-owned extensible structures

Caller-owned input/output structures begin with:

```c
uint32_t struct_size;
```

Before the call, the caller:

1. zero-initializes the structure it knows;
2. sets `struct_size` to that structure size;
3. leaves documented reserved input fields zero.

MiniShell:

- reads only fields covered by the caller-provided size;
- writes only fields covered by that size;
- ignores unknown future tail space;
- returns `MINI_ERR_INVALID` when the structure is too small to contain the
  minimum required prefix for that operation.

Unless an operation explicitly documents useful error outputs, output fields are
not meaningful after an error return.

## 9. Do not nest extensible structs by value

One extensible public structure must not embed another extensible public
structure by value when future growth of the inner structure would shift later
fields of the outer structure.

Prefer:

- flat fields;
- pointers to separate structures; or
- another layout whose offsets remain stable.

The Time/Location snapshot intentionally uses a flat layout for this reason.

## 10. Capability rules

Capability discovery follows these common rules.

### Missing top-level service

```text
service pointer == NULL
```

The service is unavailable.

### Present service, mandatory foundational operation

A function that is mandatory for the present service's established prefix must
have a non-NULL function pointer when its field is present in `struct_size`.

### Optional capability family

For optional sub-APIs such as Display text or Input key:

```text
capability bit set   <=> corresponding sub-API pointer is non-NULL
capability bit clear <=> corresponding sub-API pointer is NULL
```

Unknown future capability bits are ignored by older applications.

### Optional flat operation

A service may keep an optional operation in the base table when that keeps the
interface simpler. Unsupported calls return `MINI_ERR_UNSUPPORTED`.

Filesystem Task-6 namespace operations follow this pattern at the platform
backend boundary: the resident table can expose the appended call while a port
without the corresponding backend hook returns `MINI_ERR_UNSUPPORTED`.

## 11. Public ABI-owned types

Public signatures use:

- fixed-width integer types;
- MiniShell-defined scalar/result types;
- opaque MiniShell handles where stateful resources are needed;
- native application pointers when the app must directly access memory;
- documented byte strings/UTF-8 where appropriate.

Public signatures must not expose SDK/RTOS/backend-private types.

The ABI does not rely on C `enum` representation. Public constants are carried in
fixed-width integer fields.

Portable source still assumes a compatible C machine ABI on each target build:
function-pointer calling convention, integer widths from `<stdint.h>`, pointer
representation, alignment, and structure layout must match between MiniShell and
the separately built native application for that target. Cross-architecture
binary compatibility is not promised.

## 12. Stable numeric meanings

After ABI stabilization, numeric meanings are never reused for another purpose,
including:

```text
result codes
capability bits
flags
special-key codes
event types
source values
file types
seek origins
```

New values may be appended. Older apps must ignore unknown capability/modifier
bits and must not assume that every future enumerated numeric value is known.

## 13. Shared result codes

`mini_result_t` is shared by all MiniShell services. Zero means success and
negative values mean errors.

Current shared values are:

```c
#define MINI_OK                  ((mini_result_t)  0)
#define MINI_ERR_INVALID         ((mini_result_t) -1)
#define MINI_ERR_NOT_FOUND       ((mini_result_t) -2)
#define MINI_ERR_EXISTS          ((mini_result_t) -3)
#define MINI_ERR_BAD_HANDLE      ((mini_result_t) -4)
#define MINI_ERR_ACCESS          ((mini_result_t) -5)
#define MINI_ERR_IO              ((mini_result_t) -6)
#define MINI_ERR_NO_SPACE        ((mini_result_t) -7)
#define MINI_ERR_TOO_MANY_OPEN   ((mini_result_t) -8)
#define MINI_ERR_NAME_TOO_LONG   ((mini_result_t) -9)
#define MINI_ERR_UNSUPPORTED     ((mini_result_t)-10)
#define MINI_ERR_NOT_DIR         ((mini_result_t)-11)
#define MINI_ERR_IS_DIR          ((mini_result_t)-12)
#define MINI_ERR_NO_MEMORY       ((mini_result_t)-13)
#define MINI_ERR_NOT_READY       ((mini_result_t)-14)
#define MINI_ERR_TIMEOUT         ((mini_result_t)-15)
#define MINI_ERR_NOT_EMPTY       ((mini_result_t)-16)
```

`MINI_ERR_NOT_EMPTY` was added in Task 6 because `rmdir` has a real portable need
to distinguish a non-empty directory from a generic I/O or access failure.

Add another result only when a real ABI requires a distinct portable meaning.
Backend-native values such as `errno`, FATFS `FRESULT`, or `esp_err_t` never cross
the public boundary.

## 14. Synchronous first

Calls are synchronous unless a service contract explicitly says otherwise.
Future asynchronous behavior is added through new functions or sub-APIs rather
than by changing existing synchronous semantics.

For synchronous calls, an application-owned buffer passed to MiniShell remains
owned by the application and must not be retained after the call returns unless
the specific service explicitly documents otherwise.

## 15. Ownership and application context

Every resource crossing the ABI has explicit ownership and lifetime rules.

MiniShell-managed resources acquired by the current foreground application are
associated with that app context where practical.

```text
foreground app context
    +-- MiniShell-managed allocations
    +-- open file handles
    `-- future logical service resources
```

Normal app teardown reclaims remaining MiniShell-managed resources before the ELF
is unloaded.

This is cooperative lifecycle cleanup, not memory protection.

## 16. Foreground service routing

MiniShell currently has one foreground application at a time. Display and Input
therefore do not require acquire/release handles.

Conceptually:

```text
shell owns foreground
    -> launch app
    -> app receives foreground display/input access
    -> app returns
    -> MiniShell restores shell foreground
```

MiniShell remains the hardware owner throughout.

Foreground handoff must not expose stale queued logical input from a previous app
instance unless a future API explicitly provides such behavior.

## 17. Execution-context rule

Portable ABI calls are application-context calls.

Unless a specific service explicitly documents otherwise:

- ABI calls are not guaranteed ISR-safe;
- ABI calls are not guaranteed reentrant;
- portable applications should serialize calls that mutate the same logical
  service/resource;
- no general multi-thread safety guarantee is part of the current contract.

A future concurrency model can add stronger guarantees without changing existing
single-foreground-app semantics.

## 18. Source portability, architecture-specific binaries

The same application source should be rebuildable against the same MiniShell API
on RV32, RV64, Xtensa, ARM, or other supported architectures where the required
services exist.

A compiled ELF is not expected to be binary-compatible across architectures.

## 19. Verification hierarchy

Every foundational ABI uses three complementary test layers.

### 19.1 Unit tests — primary correctness suite

Unit tests are the main source of behavioral coverage. They should be exhaustive
where practical and run quickly enough to use throughout implementation and
refactoring.

Prefer testing service semantics through the public ABI-shaped interface with
mock/fake platform backends. Platform-independent policy should be separable from
hardware drivers so it can be tested without real hardware.

Unit tests should cover, as applicable:

```text
success paths
invalid arguments
boundary values
error translation
struct_size compatibility
capability combinations
resource ownership/bookkeeping
cleanup and repeated-use behavior
state transitions
partial/failure behavior
timeout/freshness logic
```

A service implementation is not considered trustworthy merely because its ELF
smoke test passes.

### 19.2 Runtime-loaded ELF tests — ABI integration suite

Each service gets a separately built `.elf` test using only public MiniShell
headers.

These tests are intentionally smaller than the unit suite. Their primary purpose
is to prove:

```text
separate compilation
native ABI/calling convention
mini_api_get() binding
struct/table layout
service discovery
runtime loader integration
representative service calls
normal app teardown/unload
```

They should contain a small happy path, a few representative failure checks, and
repeated launch/exit validation. They do not need to duplicate every unit-test
case.

### 19.3 Real-hardware/platform tests

Hardware-dependent behavior is validated on the reference platform, including
actual RTC persistence, SD behavior, display output, touch/input routing, timer
behavior, and other backend-specific effects.

The preferred development loop is:

```text
implement/refactor service
        |
        v
run comprehensive unit tests
        |
        v
run focused .elf ABI integration test
        |
        v
validate hardware-specific behavior
```

Unit-test failures block progress even if the ELF or hardware smoke test appears
to work.

## 20. Compatibility philosophy

MiniShell compatibility is based on:

```text
append-only tables
struct_size
capability bits
optional sub-APIs
stable numeric meanings
explicit ownership/lifetime
fixed-width public types
```

It is not based on mirroring one SDK forever.

Do not treat a newly appended compatible function as a reason to bump the ABI
generation. Require a new generation only when an incompatible change cannot
preserve the established prefix and semantics.
