# MiniShell Architecture

## 1. Purpose

MiniShell is a platform-adaptive application runtime. The architectural invariant is:

> Application cores depend on MiniShell, never directly on the host operating system, RTOS, SDK, board support package, or test/mock implementation.

MiniShell is not required to have the same thickness on every target.

## 2. System view

```text
+------------------------------------------------------+
|                   Applications                       |
|          MiniFT8 / MiniCW / MiniRTTY / tools         |
+---------------------- MiniShell ABI -----------------+
|                    MiniShell Core                    |
|       shell / app lifecycle / service contracts      |
+---------------- private backend boundary ------------+
| Linux/POSIX | NuttX | ESP-IDF/HW | mocks/simulation |
+------------------------------------------------------+
```

Linux Mint on `pc-1` is the reference behavior and a production target.

## 3. Two MiniShell responsibilities

MiniShell has two closely related responsibilities:

1. **Platform adaptation** — normalize services such as time, storage, display, input, audio, radio control, and networking.
2. **Application runtime** — discover, start, stop, and where supported dynamically load/unload applications without rebooting or rebuilding MiniShell.

Neither responsibility permits platform types to leak through the public ABI.

## 4. Public and private boundaries

The public application boundary is `include/minishell/api.h`.

The private backend boundary is internal to MiniShell. Core code calls normalized backend hooks; a backend may use POSIX, NuttX, ESP-IDF, FreeRTOS, board drivers, or simulation code freely.

```text
application
    |
mini_api_get()
    |
MiniShell service table
    |
portable MiniShell core
    |
private backend hook
    |
platform implementation
```

The portable service core owns application-visible handles, validation, capabilities, lifecycle cleanup, and ABI semantics. A platform backend owns only the normalized implementation primitives needed to provide those semantics.

## 5. Application lifecycle

V1 keeps one foreground application active at a time:

```text
shell
  -> resolve application
  -> MiniShell app_begin
  -> backend load/prepare
  -> main(argc, argv)
  -> application uses MiniShell ABI
  -> application returns
  -> backend unload/release
  -> MiniShell app_end / resource reclamation
  -> shell
```

The physical loading mechanism is not part of the application ABI.

Reference mechanisms:

```text
Linux/Mint      .so + dlopen()/dlsym()/dlclose()
Tab5/NuttX      NuttX loadable application mechanism where practical
ADV             compiled-in registry is acceptable if dynamic loading costs too much RAM
```

The user-facing model remains `apps`, `run <app>`, application exit, and return to `M$>`.

## 6. Linux reference backend

The Linux backend is real production infrastructure, not a mock. It currently provides:

```text
System          stdout terminal output
Memory          malloc/realloc/free
Filesystem      POSIX files under the MiniShell logical root
Time/Location   CLOCK_MONOTONIC, system UTC, persisted default location
Display         ANSI terminal text surface
Input           terminal key events using poll/read
App loader      .so discovery and dlopen/dlsym/dlclose
```

The default application directory is `runtime/apps` beside the MiniShell executable. `MINISHELL_APP_DIR` may override it.

The default MiniShell logical filesystem root is `~/.local/share/minishell/fs`; `MINISHELL_ROOT` may override it. Portable applications continue to use MiniShell paths such as `/sd/file.txt` and `/flash/config.ini`.

The shell and foreground application share the terminal deliberately. MiniShell prevents buffered shell input from leaking across foreground handoff, switches the terminal to non-canonical/no-echo mode while an app owns foreground input, then restores normal shell behavior after the app returns.

## 7. Service ABI

ABI generation 1 has six established service groups:

```text
system
memory
filesystem
time/location
display
input
```

All six are active on the Linux reference backend. Their public shape remains defined by `include/minishell/api.h` and the canonical service documents.

The Linux port reuses the existing portable service implementations. In particular, MiniShell still tracks and reclaims app-owned allocations and file handles at lifecycle boundaries rather than exposing raw POSIX ownership to applications.

Future application-driven service groups may include:

```text
audio
radio/CAT
USB
network
```

Do not add speculative APIs merely because a platform exposes a feature.

## 8. Mocks and simulation

Mocks live below MiniShell:

```text
MiniFT8 core
    |
MiniShell ABI
    +-- Linux/QMX real backend
    +-- file-audio backend
    +-- simulated-radio backend
```

A mock emulates a MiniShell service, not application behavior. Mock audio input is appropriate; mock FT8 decode results are not an architectural substitute for exercising the FT8 engine.

## 9. Hardware ownership

On embedded systems, MiniShell remains the owner or normalized gateway for shared hardware resources. Applications receive logical services rather than board-driver handles.

The implementation can be thin when an underlying OS already owns the resource, or thick when MiniShell itself must own drivers and resource arbitration.

## 10. Platform thickness

```text
Linux/Mint
  MiniShell: thin
  underlying owner: Linux/POSIX

Tab5/P4
  MiniShell: thin
  underlying owner: NuttX where practical

Cardputer ADV
  MiniShell: thick
  underlying owner: MiniShell + selected ESP-IDF/HW services
```

The application core sees the same MiniShell contract in all cases.

## 11. Testing model

The Linux reference is tested at three levels:

```text
portable service semantics
    -> separately loaded ABI probe applications
    -> real terminal handoff through a pseudo-terminal
```

CI builds from a clean Linux environment and exercises application discovery/loading, service calls, filesystem state, time/location behavior, display operations, key input, lifecycle cleanup, and return to the shell.

## 12. Reference-development rule

New application behavior is developed and tested first on Linux unless a feature is inherently target-specific. The Linux implementation establishes golden observable behavior for later ports.

Embedded constraints still influence core design: bounded buffers, explicit ownership, deterministic lifecycle, and avoidance of accidental dependence on unlimited host resources.
