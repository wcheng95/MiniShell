# MiniShell ABI Foundation

Status: **foundational cross-ABI contract; append-only growth active**

MiniShell provides a small platform-neutral ABI between applications and platform implementations.

## Service set

The six foundational services are System, Memory, Filesystem, Time/Location, Display, and Input. Audio was added later as the first application-driven append-only top-level extension.

## Layering

```text
application
    -> MiniShell ABI
    -> resident MiniShell services
    -> private platform/backend boundary
    -> OS/RTOS/SDK/drivers
    -> hardware
```

Applications do not receive platform-private handles/types.

## Top-level table

`mini_api_t` is append-only. Audio is appended after the original prefix. Runtime binding is through `mini_api_get()`.

`system` is mandatory; other service pointers may be NULL on targets that do not provide them.

## Compatibility rules

- `abi_version` changes only for an incompatible generation.
- Compatible growth uses append-only tables, `struct_size`, capability bits, and optional sub-APIs.
- Existing fields/numeric meanings are never repurposed after stabilization.
- Applications check that `struct_size` reaches the field they need rather than requiring the newest whole structure.
- Caller-owned extensible structures begin with `struct_size`; MiniShell reads/writes only covered fields.
- Avoid extensible structs nested by value when future growth would shift outer offsets.

## Capability rules

```text
missing top-level pointer      => service unavailable
capability bit set             <=> corresponding optional sub-API exists
unknown future capability bits => ignored by older apps
unsupported optional operation => MINI_ERR_UNSUPPORTED
```

This applies to Display text, Input key, Audio RX/TX, and future optional families.

## Public types

Public signatures use fixed-width integers, MiniShell result/scalar types, opaque MiniShell handles, application pointers where direct buffer access is intended, and documented UTF-8/byte strings. Backend-native types/errors never cross the ABI.

## Synchronous first

Calls are synchronous unless a service explicitly documents otherwise. Application buffers remain application-owned and are not retained after a synchronous call returns.

## Ownership/lifecycle

MiniShell-managed resources are associated with the current foreground application where practical:

```text
foreground app
    +-- allocations
    +-- file/directory handles
    +-- audio streams
    `-- future logical resources
```

App teardown reclaims remaining resources; active Audio TX is aborted before close.

## Execution context

Unless documented otherwise, ABI calls are application-context calls, not guaranteed ISR-safe/reentrant, and callers serialize mutations of the same logical resource.

## Portability

The same source should rebuild against the same API on supported architectures; native application binaries are not promised to be cross-architecture compatible.

## Verification hierarchy

```text
comprehensive unit tests
    -> runtime-loaded native-app ABI tests
    -> platform/hardware validation
```

Unit tests are primary and block progress when failing.

## Compatibility philosophy

MiniShell compatibility relies on append-only tables, `struct_size`, capability bits, optional sub-APIs, stable numeric meanings, explicit ownership/lifetime, and fixed-width public types. Compatible additions do not require an ABI-generation bump.
