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

## 5. Application lifecycle

V1 keeps one foreground application active at a time:

```text
shell
  -> resolve application
  -> backend load/prepare
  -> main(argc, argv)
  -> application uses MiniShell ABI
  -> application returns
  -> MiniShell cleanup
  -> backend unload/release
  -> shell
```

The physical loading mechanism is not part of the application ABI.

Reference mechanisms:

```text
Linux/Mint      .so + dlopen()/dlsym()/dlclose()
Tab5/NuttX      NuttX loadable application mechanism where practical
ADV             compiled-in registry is acceptable if dynamic loading costs too much RAM
```

The user-facing model should remain `apps`, `run <app>`, application exit, and return to `M$>`.

## 6. Linux reference backend

The Linux backend is real production infrastructure, not a mock. It currently owns:

- terminal output for System.write;
- application discovery;
- runtime loading of `.so` applications;
- application unload after return.

The default application directory is `runtime/apps` beside the MiniShell executable. `MINISHELL_APP_DIR` may override it.

The first reference application is `hello.so`.

## 7. Service ABI

ABI generation 1 retains six established service groups:

```text
system
memory
filesystem
time/location
display
input
```

The Linux baseline initially activates System. Remaining service implementations are added behind the same public contracts as real applications require them.

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

A mock must emulate a MiniShell service, not bypass application logic. For example, mock audio input is appropriate; mock FT8 decode results are not an architectural substitute for exercising the FT8 engine.

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

## 11. Reference-development rule

New application behavior is developed and tested first on Linux unless a feature is inherently target-specific. The Linux implementation establishes golden observable behavior for later ports.

Embedded constraints must still influence core design: bounded buffers, explicit ownership, deterministic lifecycle, and avoidance of accidental dependence on unlimited host resources.
