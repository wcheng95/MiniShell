# MiniShell ABI Foundation

Status: **foundational cross-ABI contract; append-only growth active**

MiniShell provides a platform-neutral ABI between applications and resident services/backends.

## Service history

The original service prefix is System, Memory, Filesystem, Time/Location, Display, and Input. Audio was later appended as the first application-driven top-level extension.

## Core compatibility rules

- `mini_api_t` and service tables grow append-only.
- Every extensible public table/structure uses `struct_size`.
- `abi_version` changes only for incompatible generations.
- Optional families use capabilities and nullable sub-APIs.
- Existing numeric meanings are never repurposed.
- Backend-native types/errors do not cross the public ABI.
- Public resources have explicit ownership/lifetime.
- Compatible additions do not require a version-generation bump.

## Dependency direction

```text
application
    -> MiniShell public ABI
    -> resident services
    -> private platform/backend boundary
    -> OS/RTOS/SDK/drivers/hardware
```

## Lifecycle

MiniShell associates managed allocations, file handles, Audio streams, and future logical resources with the foreground app where practical. Teardown reclaims them; active Audio TX is aborted before close.

## Execution model

Calls are synchronous unless documented otherwise. Buffers remain caller-owned. ABI calls are application-context calls unless a service explicitly promises stronger ISR/reentrancy/thread guarantees.

## Verification

```text
unit tests (primary)
    -> runtime-loaded native-app ABI integration
    -> platform/hardware validation
```

Unit-test failures block progress.
