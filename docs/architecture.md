# MiniShell Architecture

## 1. System View

MiniShell is a resident MCU application environment.

```text
+------------------------------------------------------+
|                    Applications                      |
|          future: med.elf   minift8.elf   ...        |
+----------------------- MiniShell ABI ----------------+
|                    MiniShell Core                    |
|                                                      |
|  shell   app manager   diagnostics   service APIs    |
+-------------------- service boundary ----------------+
|                  Platform Services                   |
|                                                      |
| console memory filesystem time display audio ...    |
+-------------------- platform boundary ---------------+
|              ESP32-P4 / ESP-IDF / Tab5              |
+------------------------------------------------------+
```

The key architectural rule is that applications normally depend on MiniShell,
not directly on ESP-IDF or Tab5 hardware.

## 2. Reference Platform

V1 reference platform:

- M5Stack Tab5
- ESP32-P4
- native RISC-V applications
- external PSRAM available for runtime application loading and buffers
- microSD as an important user/app storage backend
- ESP-IDF as the underlying platform SDK

MiniShell may depend heavily on ESP-IDF inside the ESP32-P4 platform port. That
dependency must stop at the MiniShell ABI boundary.

## 3. Major Modules

The framework should remain deliberately small and modular.

```text
MiniShell/
|-- README.md
|-- docs/
|-- include/
|   `-- minishell/
|       `-- api.h
|-- core/
|   |-- minishell_shell/
|   |-- minishell_app/
|   `-- minishell_services/
|-- platform/
|   `-- minishell_platform_tab5/
|-- examples/
|   `-- hello/
`-- tests/
```

Exact filenames may change as implementation teaches us more. Module boundaries
matter more than directory aesthetics.

A module is replaceable only when unrelated modules depend on its interface
rather than its internal representation.

## 4. Shell

The shell is the user-facing control plane.

Initial responsibilities:

- command-line input
- command parsing
- built-in command dispatch
- filesystem commands
- application search path
- launching an app by command name
- returning to the prompt after app exit
- platform diagnostics

V1 does not need POSIX pipelines, redirection, background jobs, users, or process
management.

## 5. Filesystem Namespace

MiniShell exposes storage through a clear logical namespace.

Initial proposal:

```text
/flash   internal persistent filesystem
/sd      microSD filesystem
```

Applications receive paths through MiniShell filesystem services and should not
mount/unmount or initialize the underlying storage hardware.

## 6. App Manager

The app manager owns application lifecycle.

```text
shell command
    |
resolve app name/path
    |
validate executable + ABI
    |
load ELF
    |
prepare app context
    |
application runs in foreground
    |
application returns
    |
cleanup MiniShell-managed resources
    |
unload ELF
    |
return to shell
```

V1 runs one foreground native application at a time. No process isolation is
provided.

## 7. Service Layer

MiniShell services are the normal path from applications to runtime and
hardware-related capabilities.

Task 1 develops the first basic ABI set in this order:

```text
system
memory
filesystem
console/input
time
```

Later service groups may include:

```text
display
audio
USB
network
power
```

A service is not considered established merely because an API table exists. Each
basic ABI must have:

1. a platform-neutral documented contract,
2. one clear owner,
3. a resident implementation,
4. a focused separately built ELF test,
5. real-hardware validation,
6. defined error and cleanup behavior.

Real applications are postponed until this foundation is proven.

## 8. Platform Layer

The platform layer translates MiniShell services into hardware/SDK operations.

For Tab5 this may include:

```text
platform/minishell_platform_tab5/
    boot/startup
    console
    memory
    storage
    time
    display
    input
    audio
    USB
    network
```

The platform layer may freely include ESP-IDF and M5Stack-specific headers.
Public MiniShell headers may not.

The ELF loader is resident infrastructure used by the app manager. It is not an
application-facing hardware service.

## 9. Hardware Ownership

MiniShell owns shared hardware after boot.

Example filesystem path:

```text
application
   |
MiniShell filesystem ABI
   |
filesystem service
   |
ESP-IDF VFS/FATFS
   |
SD hardware
```

Applications should not call SD initialization, FATFS mount, SDMMC bus
initialization, or equivalent platform operations.

Direct access remains technically possible because MiniShell provides no
protection. Such access is outside the standard portable contract.

## 10. Replaceable Internals

MiniShell's modularity goal is not simply to split source code into files. The
important property is that one implementation can change without forcing
unrelated modules to change.

Examples:

```text
change FATFS implementation       -> application ABI unchanged
change console transport          -> application ABI unchanged
change memory allocator internals -> applications unchanged
change platform port              -> MiniShell source users rebuild, not redesign
```

This requires small interfaces, clear ownership, and no leakage of private
implementation types across module boundaries.

## 11. Memory Model

MiniShell and applications share the MCU address space.

V1 assumptions:

- no virtual memory
- no process address-space isolation
- no privilege boundary
- one foreground loaded app
- MiniShell remains resident while the app runs
- loaded app memory is reclaimed after exit where the ELF loader permits

Task 1 will define a MiniShell memory ABI so applications that need dynamic
memory do not depend directly on the platform allocator.

The app manager should track MiniShell-managed resources so cooperative cleanup
is possible when an application returns normally.

It cannot recover safely from arbitrary memory corruption.

## 12. ABI Boundary

The ABI is represented by MiniShell-owned types and function signatures.

Task 0 validated the runtime binding model:

```text
app.elf
   |
mini_api_get()
   |
versioned MiniShell API table
   |
resident services
```

Public ABI rules:

- no ESP-IDF types
- no M5Stack BSP objects
- no `FILE *` or FATFS objects
- no FreeRTOS or raw driver handles
- opaque MiniShell handles where stateful resources are needed
- explicit ownership, lifetime, error, and compatibility semantics
- backward-compatible extension preferred where practical

The filesystem ABI v0 contract is already documented in `docs/app-abi.md`.

## 13. Resource Ownership

MiniShell remains the owner of resources acquired through its services.

Conceptually:

```text
foreground app context
    |
    +-- MiniShell-managed allocations
    +-- open file handles
    +-- future service resources
```

Normal app teardown releases remaining MiniShell-managed resources before the
ELF is unloaded.

This is cooperative cleanup, not memory protection.

## 14. ABI Testing

Task 1 uses focused ELF tests as first-class architecture tests.

Suggested test programs:

```text
abi_system.elf
abi_memory.elf
abi_fs.elf
abi_console.elf
abi_time.elf
```

Each test is built separately from MiniShell and uses only the public MiniShell
ABI. A typical validation path is:

```text
M$> abi_fs
[focused filesystem ABI tests]
PASS
M$>
```

This validates:

```text
ELF app
  -> ABI table
  -> resident service
  -> platform implementation
  -> hardware/backend
```

Host/unit tests should also be used for platform-independent policy and
bookkeeping where useful.

## 15. Diagnostics as Architecture

Diagnostics are how platform problems are isolated before an application is
blamed.

Examples:

```text
M$> status
M$> mem
M$> storage status
M$> ls /sd
M$> rtc status
```

A service should be independently diagnosable and testable.

## 16. Development Milestones

### Task 0 - Framework proof — COMPLETE

Validated on real M5Stack Tab5 / ESP32-P4 hardware:

1. boot to `M$>` over USB Serial/JTAG,
2. mount microSD,
3. load `/sd/apps/hello.elf`,
4. resolve `mini_api_get()`,
5. call a resident MiniShell service,
6. return and unload,
7. repeat without rebooting.

See `docs/task0.md`.

### Task 1 - ABI Foundation — ACTIVE

Define, implement, and independently validate:

```text
system
memory
filesystem
console/input
time
```

Each ABI gets a focused runtime-loaded ELF test and real-hardware validation.

See `docs/task1.md`.

### Later milestones

After the ABI foundation is proven, add the first real application. `med` remains
a strong candidate because it can exercise several already-established services
without specialized hardware.

Additional ABIs should continue to be added only when justified by real system or
application requirements.
