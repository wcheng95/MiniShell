# MiniShell Architecture

## 1. System View

MiniShell is a resident MCU application environment.

```text
+------------------------------------------------------+
|                    Applications                      |
|          med.elf   minift8.elf   minicw.elf ...     |
+----------------------- MiniShell ABI ----------------+
|                    MiniShell Core                    |
|                                                      |
|  shell   app manager   diagnostics   service APIs    |
+-------------------- service boundary ----------------+
|                  Platform Services                   |
|                                                      |
| display input audio storage time USB network power  |
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

MiniShell may depend heavily on ESP-IDF inside the ESP32-P4 platform port.
That dependency must stop at the MiniShell ABI boundary.

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
|   |-- hello/
|   `-- med/
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

Example:

```text
M$> ls /sd/apps
hello.elf
med.elf

M$> med /sd/notes.txt
[MiniEditor owns the foreground terminal through MiniShell services]
[MiniEditor exits]
M$>
```

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

The shell may later provide configurable command/application search paths, for
example:

```text
/apps
/sd/apps
```

A merged visual `ls` view is optional convenience; the canonical filesystem
namespace should remain explicit and predictable.

## 6. App Manager

The app manager owns application lifecycle.

Launch sequence:

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
call app entry point
    |
application runs in foreground
    |
application returns / requests exit
    |
cleanup registered resources
    |
unload ELF
    |
return to shell
```

V1 runs one foreground native application at a time.

No process isolation is provided.

## 7. Service Layer

MiniShell services are the normal path from applications to hardware-related
capabilities.

Likely service groups:

```text
system
console/input
filesystem
memory/status
time
display
audio
USB
network
power
```

These are categories, not a commitment to implement every service immediately.

Each service should have:

1. a platform-neutral public interface
2. one clear owner
3. a platform implementation
4. shell diagnostics where useful

Task 1 intentionally lets a real application (`med`) determine the minimum
console/input and filesystem operations we actually need.

## 8. Platform Layer

The platform layer translates MiniShell services into hardware/SDK operations.

For Tab5 this may include:

```text
platform/minishell_platform_tab5/
    boot/startup
    console
    display
    input
    storage
    time
    audio
    USB
    network
    memory
```

The platform layer may freely include ESP-IDF and M5Stack-specific headers.
Public MiniShell headers may not.

The ELF loader is resident infrastructure used by the app manager. It is not an
application-facing hardware service.

## 9. Hardware Ownership

MiniShell owns shared hardware after boot.

Example storage path:

```text
med
   |
mini filesystem API
   |
MiniShell filesystem service
   |
ESP-IDF VFS/FATFS
   |
SD hardware
```

`med` should not call SD initialization, FATFS mount, SDMMC bus initialization,
or equivalent platform operations.

Example console path:

```text
med
   |
MiniShell console/input API
   |
console service
   |
USB Serial/JTAG VFS/driver
```

The application sees normalized key events and logical terminal operations, not
USB driver structures or raw platform ownership.

Direct access remains technically possible because MiniShell provides no
protection. Such access is outside the standard portable contract.

## 10. Replaceable Internals

MiniShell's modularity goal is not simply to split source code into files. The
important property is that one implementation can change without forcing
unrelated modules to change.

Task 1 should demonstrate examples such as:

```text
change document representation  -> filesystem service unaffected
change FATFS implementation      -> med unaffected
change terminal implementation   -> med unaffected
change editor rendering strategy -> app loader unaffected
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

The app manager should track MiniShell-managed resources so cooperative cleanup
is possible even when an app forgets to close a normal service handle before
returning.

It cannot recover safely from arbitrary memory corruption.

## 12. ABI Boundary

The ABI is represented by MiniShell-owned types and function signatures.

Task 0 validated the current runtime binding model:

```text
app.elf
   |
mini_api_get()
   |
versioned MiniShell API table
   |
resident services
```

The current ABI is still deliberately small and not frozen. Task 1 extends it
only with operations needed by the first real application.

Public ABI rules:

- no ESP-IDF types
- no M5Stack BSP objects
- no `FILE *` or FATFS objects
- no raw USB driver handles
- opaque MiniShell handles where stateful resources are needed
- backward-compatible extension preferred where practical

## 13. Resource Ownership

MiniShell remains the owner of system services while an app is active.

An app may acquire logical resources such as:

- open files
- foreground console/display ownership
- audio stream/session
- input subscription
- timers
- network handles

Task 1 begins concrete bookkeeping with filesystem handles.

Conceptually:

```text
app sees:       mini_file_t = opaque value

MiniShell owns:
    handle slot
      -> underlying file object
      -> owning foreground application
      -> state
```

Normal app teardown releases MiniShell-managed resources before the ELF is
unloaded.

This is cooperative cleanup, not protection from arbitrary memory corruption.

## 14. Foreground UI Model

A foreground application may temporarily control the user-facing terminal,
display, or input through MiniShell services, but MiniShell remains the hardware
owner.

Conceptually:

```text
shell owns foreground
    |
launch app
    v
app owns foreground session through API
    |
app exits
    v
MiniShell restores shell foreground
```

Task 1 uses this model for a full-screen terminal editor. Future applications
such as MiniFT8 may use the same concept for the physical display/input system.

## 15. Diagnostics as Architecture

Diagnostics are not an afterthought. They are how platform problems are isolated
before an application is blamed.

Examples:

```text
M$> status
M$> mem
M$> storage status
M$> ls /flash
M$> ls /sd
M$> rtc status
M$> usb status
```

If a subsystem cannot be verified from MiniShell itself, the service boundary is
not yet complete enough.

## 16. Development Milestones

### Task 0 - Framework proof — COMPLETE

Validated on real M5Stack Tab5 / ESP32-P4 hardware:

1. Boot to the `M$>` shell over USB Serial/JTAG.
2. Mount microSD through MiniShell-owned platform code.
3. Load `/sd/apps/hello.elf` dynamically.
4. Resolve the resident `mini_api_get()` runtime symbol.
5. Call a MiniShell system service from the separately built ELF.
6. Return from the app, unload it, and return to `M$>`.
7. Repeat load/run/unload without rebooting.

See `docs/task0.md`.

### Task 1 - MiniEditor (`med`) — ACTIVE

Build a small nano-like terminal editor as the first useful MiniShell app.

Task 1 drives the implementation of:

- platform-neutral console/input service
- normalized key events
- platform-neutral filesystem service
- opaque file handles
- app-owned resource cleanup
- first multi-module real ELF application

Expected user flow:

```text
M$> med /sd/notes.txt
[edit file]
Ctrl-S
Ctrl-X
M$>
```

See `docs/task1.md`.

### Later milestones

After `med` proves these boundaries, add other services only when real
applications require them. MiniFT8 should come later, once console/input,
storage, lifecycle, and additional needed service boundaries have proven
themselves with smaller applications.
