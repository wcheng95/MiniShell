# MiniShell ABI Foundation

Status: **foundational cross-ABI contract; append-only growth active**

MiniShell exists to provide a small, reusable, platform-neutral ABI between applications and platform implementations. The ABI is the main architectural product of MiniShell, not an implementation detail.

## 1. Service set

The six foundational Task-1 ABIs are:

```text
system
memory
filesystem
time/location
display
input
```

Audio was added later as the first application-driven append-only top-level service extension.

Canonical detailed contracts live under `docs/abi/`, including `audio-abi.md`.

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

Normal applications do not depend on platform-private handles or SDK types.

## 3. Top-level API

The public table is append-only. Its current tail includes Audio after the original service prefix:

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
    const mini_audio_api_t         *audio;
} mini_api_t;
```

Runtime binding is through `mini_api_get()`.

`system` is mandatory. Other service pointers may be NULL when the target does not provide that service.

## 4. API acquisition and lifetime

Resident API/service-table pointers are MiniShell-owned, read-only to applications, valid for the current app execution, and must not be persisted across app instances.

## 5. ABI version meaning

`abi_version` identifies an incompatible ABI generation. Ordinary compatible growth does not bump it. Compatible additions use append-only tables, `struct_size`, capability bits, and optional sub-APIs.

Filesystem namespace growth and the later Audio service are examples of compatible append-only extension.

## 6. Append-only tables

Every public service function table begins with `uint32_t struct_size`. Existing fields are never reordered, removed, repurposed, or given incompatible semantics after stabilization. New top-level service pointers are appended to `mini_api_t`.

## 7. Correct `struct_size` use

Applications check that `struct_size` reaches the specific field they need rather than requiring the newest complete structure size.

## 8. Caller-owned extensible structures

Caller-owned public structures begin with `struct_size`. Callers zero-initialize, set the size they know, and leave reserved input fields zero. MiniShell reads/writes only fields covered by that size.

## 9. Do not nest extensible structs by value

Avoid embedding one extensible public structure by value inside another when future growth would shift outer-field offsets. Prefer flat fields or pointers to separate structures.

## 10. Capability rules

```text
missing top-level pointer      => service unavailable
capability bit set             <=> corresponding optional sub-API exists
unknown future capability bits => ignored by older apps
unsupported optional operation => MINI_ERR_UNSUPPORTED
```

This applies to optional families such as Display text, Input key, and Audio RX/TX.

## 11. Public ABI-owned types

Public signatures use fixed-width integers, MiniShell-defined result/scalar types, opaque MiniShell handles, native application pointers when direct memory access is required, and documented byte strings/UTF-8 where appropriate. Platform-private types do not cross the public boundary.

## 12. Stable numeric meanings

Numeric meanings are never reused after stabilization, including result codes, capability bits, flags, key/event values, file types, seek origins, and sample formats.

## 13. Shared result codes

`mini_result_t` is shared by all services. Backend-native values such as `errno`, FATFS `FRESULT`, or SDK-specific error types never cross the public boundary.

`MINI_ERR_END_OF_STREAM` is used by finite providers such as WAV Audio RX. Add result codes only when a real portable distinction is needed.

## 14. Synchronous first

Calls are synchronous unless a service explicitly says otherwise. Application-owned buffers remain application-owned and are not retained after synchronous calls return.

## 15. Ownership and application context

MiniShell-managed resources belong to the current foreground app context where practical:

```text
foreground app
    +-- MiniShell-managed allocations
    +-- open file handles
    +-- open audio streams
    `-- future logical resources
```

App teardown reclaims resources before unload; active Audio TX is aborted before close.

## 16. Foreground service routing

MiniShell has one foreground application at a time. Display and Input therefore route to the foreground without explicit app-level acquire/release handles. MiniShell remains the hardware owner.

## 17. Execution-context rule

Unless a service says otherwise, ABI calls are application-context calls, not guaranteed ISR-safe or reentrant, and callers should serialize mutations of the same logical resource.

## 18. Source portability, architecture-specific binaries

The same source should rebuild against the same MiniShell API on supported RV32/RV64/Xtensa/ARM/etc. targets. Native app binaries are not expected to be cross-architecture compatible.

## 19. Verification hierarchy

Primary correctness comes from comprehensive unit tests against mock/fake platform backends. Runtime-loaded native app tests then prove separate compilation, table layout/binding, representative calls, and teardown. Finally, platform/hardware tests validate backend-specific effects.

```text
implement/refactor
    -> unit tests
    -> native app ABI integration
    -> real platform/hardware validation
```

Unit-test failures block progress even if a smoke test appears to work.

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

A compatible appended service/function is not a reason to bump the ABI generation.
