# MiniShell Application ABI

Status: **initial design contract; not frozen ABI v1**

This document defines the boundary between MiniShell and a normal native
application: loading, entry, runtime API acquisition, lifecycle, compatibility,
and foreground ownership. Individual service semantics live in the standalone
service ABI documents.

Task 0 validated native ELF loading and runtime binding on real M5Stack Tab5 /
ESP32-P4 hardware.

## 1. Goals

The Application ABI should:

- allow an app to be built separately from MiniShell;
- allow an app to load and execute without rebooting;
- keep MiniShell resident while the app runs;
- expose MiniShell-owned runtime services without exposing ESP-IDF;
- keep application source portable across MiniShell platforms where practical;
- allow MiniShell and apps to evolve independently behind a compatible ABI;
- remain small enough to understand and test.

The ABI is not a process-isolation or security boundary.

## 2. Native application format

V1 native applications use ELF.

ELF is the initial loading/container format, not the conceptual MiniShell service
architecture. A future runtime such as MicroPython or WASM may coexist with the
same service model.

Binaries are architecture-specific. The same source may be rebuilt for ESP32-P4
RV32, a future RV64 target, Xtensa, ARM, or another supported architecture, but
one compiled ELF is not expected to run across those architectures.

## 3. Entry point

The current native application model uses the ordinary C entry point:

```c
int main(int argc, char **argv);
```

Shell arguments are passed through `argc` and `argv`.

Example:

```text
M$> minift8 --band 20m
```

conceptually produces:

```text
argc = 3
argv[0] = "minift8"
argv[1] = "--band"
argv[2] = "20m"
```

Task 0 validated this model through the Espressif ELF loader.

The application entry point is deliberately separate from MiniShell API
acquisition. The API pointer is not passed as an additional `main()` argument.

## 4. Runtime API acquisition

An application obtains the resident MiniShell API through:

```c
const mini_api_t *api = mini_api_get();
```

Task 0 proved runtime resolution of `mini_api_get()` from a separately built ELF.

The returned API table and all resident service-table pointers reached through it
are owned by MiniShell and valid for the duration of the current app execution.
They are read-only to the application and must not be freed, modified, or reused
by a later app instance.

## 5. Top-level API shape

Task 1 intends this top-level service prefix:

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

`system` is mandatory. Other services may be absent on a smaller platform and
are represented by NULL top-level pointers.

New services are appended after the established prefix.

The cross-ABI compatibility rules for `struct_size`, capabilities, stable numeric
values, caller-owned structures, ownership, and execution context live in
`docs/abi-foundation.md`.

## 6. ABI versioning

MiniShell exposes an ABI version plus `struct_size` on the top-level table.

Compatibility is not determined by comparing against `sizeof(the newest
mini_api_t)` unconditionally. An application checks that the particular fields it
requires are present, then checks the corresponding service-table fields and
capabilities it needs.

Incompatible semantic changes require a new major ABI version. Compatible growth
should prefer append-only extension.

ABI v1 must not be considered frozen until the Task 1 service contracts have
survived implementation and hardware tests.

## 7. MiniShell-owned public types

Public MiniShell headers use MiniShell-owned and fixed-width types.

Examples include:

```c
typedef int32_t  mini_result_t;
typedef uint32_t mini_file_t;
```

Normal portable applications must not need ESP-IDF types, FreeRTOS handles,
FATFS objects, libc `FILE *`, M5Stack driver objects, or raw platform-driver
handles.

Platform-specific extension APIs may intentionally expose platform concepts, but
those are outside the standard portable ABI.

## 8. Foundational service contracts

Task 1 defines six foundational service ABIs:

```text
system
memory
filesystem
time/location
display
input
```

Canonical documents:

- `docs/system-abi.md`
- `docs/memory-abi.md`
- `docs/filesystem-abi.md`
- `docs/time-location-abi.md`
- `docs/display-abi.md`
- `docs/input-abi.md`

This document does not duplicate those service definitions.

## 9. Foreground application lifecycle

V1 supports one foreground native application at a time.

```text
shell command
    -> resolve app name/path
    -> validate/load ELF
    -> prepare app context
    -> enter main(argc, argv)
    -> application runs
    -> application returns exit status
    -> reclaim remaining MiniShell-managed app resources
    -> unload ELF
    -> restore shell foreground
```

The app manager owns this lifecycle.

An application should normally return from `main()` rather than rebooting or
powering down the MCU.

## 10. Resource ownership during an app

MiniShell remains the owner of shared hardware and resident services while an app
runs.

The foreground app may own logical resources obtained through MiniShell APIs,
for example:

```text
dynamic allocations
open file handles
future logical service/session resources
```

Where practical, MiniShell associates those resources with the current app
context so normal teardown can reclaim leftovers before the ELF is unloaded.

This cleanup does not protect MiniShell from arbitrary memory corruption in the
shared MCU address space.

## 11. Foreground display/input model

V0 has one foreground application and one primary logical display/input stream.

```text
shell owns foreground
    -> launch app
    -> app receives foreground Display/Input service access
    -> app returns
    -> MiniShell restores shell foreground
```

MiniShell remains the physical hardware owner. Display and Input do not require
application acquire/release handles in V0.

Queued stale input from a previous app instance must not be exposed across
foreground handoff unless a future API explicitly defines such behavior.

## 12. Direct-access escape hatch

MiniShell intentionally does not provide MMU/process protection. A developer may
bypass the portable ABI and directly access hardware or platform SDK APIs when a
specialized need justifies it.

That application becomes platform-specific and assumes responsibility for any
system-state corruption, ownership conflict, or crash it causes.

The existence of this escape hatch must not weaken normal service ownership.

## 13. Application levels

MiniShell recognizes three useful styles:

### Portable

Uses only the standard MiniShell ABI.

### Platform-aware

Uses the standard ABI plus documented platform-specific extensions.

### Bare hardware

Directly accesses SDKs, registers, or peripherals and owns the consequences.

## 14. Non-goals for ABI V1

V1 does not need:

```text
POSIX compatibility
fork/exec process semantics
users or permissions
virtual memory
memory protection
background applications
full RTOS API exposure
full platform SDK exposure
binary compatibility across CPU architectures
```

## 15. Task 0 validation

Task 0 proved the runtime mechanism with `hello.elf`:

```text
hello.elf
    -> main(argc, argv)
    -> mini_api_get()
    -> api->system->write(...)
    -> return
    -> unload
    -> M$>
```

The application was built separately, loaded from microSD, called a resident
MiniShell service, returned normally, and was launched repeatedly while the
resident shell remained usable.

See `docs/task0.md` for the historical hardware validation.

## 16. Task 1 relationship

Task 1 does not add a real product application yet. It implements and validates
the six foundational ABIs independently through focused ELF tests.

Only after those boundaries are proven should MiniShell add a real application
such as `med`.
