# MiniShell Architecture

## 1. System View

MiniShell is a resident MCU application environment.

```text
+------------------------------------------------------+
|                    Applications                      |
|          minift8.elf   minicw.elf   ...             |
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

The initial framework should remain deliberately small.

```text
MiniShell/
|-- README.md
|-- docs/
|-- include/
|   `-- minishell/
|       |-- api.h
|       |-- app.h
|       |-- result.h
|       `-- version.h
|-- core/
|   |-- shell/
|   |-- app/
|   `-- services/
|-- platform/
|   `-- esp32p4_tab5/
|-- examples/
|   `-- hello/
`-- tests/
```

Exact filenames may change as implementation teaches us more. Module boundaries
matter more than directory aesthetics.

## 4. Shell

The shell is the user-facing control plane.

Initial responsibilities:

- command-line input
- command parsing
- built-in command dispatch
- current directory
- filesystem commands
- application search path
- launching an app by command name
- returning to the prompt after app exit
- platform diagnostics

Example:

```text
$> ls /sd/apps
hello.elf
minift8.elf

$> minift8
[MiniFT8 owns the foreground UI through MiniShell services]
[MiniFT8 exits]
$>
```

V1 does not need POSIX pipelines, redirection, background jobs, users, or process
management.

## 5. Filesystem Namespace

MiniShell should expose storage through a clear logical namespace.

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

Proposed launch sequence:

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

V1 should run one foreground native application at a time.

No process isolation is provided.

## 7. Service Layer

MiniShell services are the normal path from applications to hardware-related
capabilities.

Likely service groups:

```text
system
memory
filesystem
storage diagnostics
time
display
input
audio
USB
network
power
```

These are categories, not a commitment to implement every service in Task 0.

Each service should have:

1. a platform-neutral public interface
2. one clear owner
3. a platform implementation
4. shell diagnostics where useful

## 8. Platform Layer

The platform layer translates MiniShell services into hardware/SDK operations.

For Tab5 this may include:

```text
platform/esp32p4_tab5/
    boot/startup
    display
    input
    storage
    time
    audio
    USB
    network
    memory
    ELF loader integration
```

The platform layer may freely include ESP-IDF and M5Stack-specific headers.
Public MiniShell headers may not.

## 9. Hardware Ownership

MiniShell owns shared hardware after boot.

Example storage path:

```text
MiniFT8
   |
mini_file_open()
   |
MiniShell filesystem service
   |
ESP-IDF VFS/FATFS
   |
SD hardware
```

MiniFT8 should not call SD initialization, FATFS mount, SPI bus initialization,
or equivalent platform operations in the portable application path.

Direct access remains technically possible because MiniShell provides no
protection. Such access is outside the standard portable contract.

## 10. Memory Model

MiniShell and applications share the MCU address space.

V1 assumptions:

- no virtual memory
- no process address-space isolation
- no privilege boundary
- one foreground loaded app
- MiniShell remains resident while the app runs
- loaded app memory is reclaimed after exit where the ELF loader permits

The app manager should track MiniShell-managed resources so cooperative cleanup
is possible even when an app returns through the normal lifecycle.

It cannot recover safely from arbitrary memory corruption.

## 11. ABI Boundary

The ABI should be represented by MiniShell-owned types and function signatures.

A useful initial model is a versioned API table passed to the application entry
point:

```c
int mini_main(const mini_api_t *api, int argc, char **argv);
```

This avoids requiring application code to bind directly to ESP-IDF symbols and
makes the dependency direction explicit.

The exact calling and ELF-linking mechanism will be validated against the
ESP32-P4 ELF loader during Task 0 before the ABI is frozen.

## 12. Diagnostics as Architecture

Diagnostics are not an afterthought. They are how platform problems are isolated
before an application is blamed.

Examples:

```text
$> status
$> mem
$> storage status
$> ls /flash
$> ls /sd
$> rtc status
$> usb status
```

If a subsystem cannot be verified from MiniShell itself, the service boundary is
not yet complete enough.

## 13. Initial Development Milestones

### Task 0 - Framework proof

Goal: prove the architecture with the minimum working vertical slice.

1. Boot Tab5 into a text shell.
2. Accept a command.
3. Mount and list storage through MiniShell-owned storage code.
4. Integrate the ESP32-P4 ELF loader.
5. Load `/sd/apps/hello.elf`.
6. Pass the MiniShell API table to it.
7. Have `hello.elf` call at least one MiniShell runtime service.
8. Return from the app.
9. Unload it.
10. Return to `$>` without reboot.

Success looks like:

```text
MiniShell 0.1
$> ls /sd/apps
hello.elf
$> hello
Hello from a MiniShell ELF app.
$>
```

### Task 1 - Useful diagnostics

Add enough filesystem, memory, time, display/input, and platform status commands
to debug MiniShell itself independently of applications.

### Task 2 - First real app port

Choose a small existing application before attempting MiniFT8. The goal is to
exercise lifecycle and services without importing a large application's legacy
hardware assumptions.

MiniFT8 should come later, once the boundary has proven itself.
