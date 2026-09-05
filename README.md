# MiniShell

MiniShell is a lightweight MCU application environment that provides a shell,
runtime-loadable native applications, reusable system services, and a stable
application API.

MiniShell is not intended to be a small Linux clone. It deliberately adopts only
the operating-system ideas that are useful on an MCU:

- a resident shell
- runtime-loadable applications
- a stable application ABI
- reusable system services
- clear hardware ownership
- a simple application lifecycle
- diagnostics available before an application starts

Protection is by convention rather than privilege separation. A normal
application uses MiniShell services and does not own system hardware. A trusted
application may access hardware directly when necessary, but doing so leaves the
portable MiniShell contract; if it corrupts system state or crashes the MCU, the
application developer owns the bug.

## First Platform

The first reference platform is the **M5Stack Tab5 / ESP32-P4**. Tab5 gives the
project enough PSRAM and peripherals to develop the architecture without forcing
early memory compromises.

ESP-IDF may be used heavily below the platform boundary. The public MiniShell ABI
must not expose ESP-IDF types or require applications to use ESP-IDF APIs.

## Initial User Model

After power-on, MiniShell presents a local shell:

```text
MiniShell 0.1

$> ls
flash/
sd/

$> ls /sd
apps/
logs/

$> minift8
[MiniFT8 runs]

[application exits]
$>
```

An unknown shell command may be resolved through an application search path such
as `/apps`, `/sd/apps`, or another configured location.

## V1 Application Model

V1 will focus on native ELF applications.

```text
                 application.elf
                       |
                 MiniShell ABI
================================================
                 MiniShell core
      shell / app manager / system services
================================================
              platform implementation
             ESP-IDF + Tab5 hardware
================================================
                    ESP32-P4
```

ELF is the initial loading format, not the MiniShell architecture itself. Future
runtimes such as MicroPython or WASM may coexist without changing the service
model.

## Core Principles

1. **One hardware owner.** MiniShell owns shared hardware and exposes services.
2. **Runtime services, not application-owned drivers.** Applications normally use
   MiniShell APIs for storage, display, input, audio, time, USB, networking, and
   other shared resources.
3. **Small stable ABI.** The application boundary is ours and remains independent
   of ESP-IDF.
4. **Portable source, architecture-specific binaries.** An application can be
   rebuilt for different MCU architectures while keeping the same MiniShell API.
5. **Convention instead of protection.** MiniShell does not attempt process or
   memory isolation.
6. **Direct hardware access remains possible.** It is an explicit escape hatch,
   not the normal application model.
7. **Independent optimization.** MiniShell services and applications may evolve
   independently as long as the ABI contract remains compatible.
8. **Diagnose the platform first.** Storage, RTC, USB, audio, networking, and
   other services should be testable from the shell before launching an app.
9. **Top-down modular design.** Boundaries and ownership are defined before
   implementation details.
10. **Keep it understandable.** MiniShell should remain small enough to study,
    debug, and port.

## Task 0

The first implementation milestone uses **physical UART0 as the only terminal**.
Display, touch, Wi-Fi, USB host, audio, RTC, and other Tab5 features stay out of
the framework until the basic app lifecycle works.

Task 0 target:

```text
power on
   -> UART $> shell
   -> MiniShell mounts /sd
   -> ls /sd/apps
   -> hello
   -> load hello.elf
   -> hello calls MiniShell runtime API
   -> hello returns
   -> unload
   -> $>
```

See [`docs/task0.md`](docs/task0.md) for wiring, build steps, and success criteria.

## Documents

- [`docs/design-principles.md`](docs/design-principles.md)
- [`docs/architecture.md`](docs/architecture.md)
- [`docs/app-abi.md`](docs/app-abi.md)
- [`docs/task0.md`](docs/task0.md)

## Current Status

Task 0 framework committed for M5Stack Tab5 / ESP32-P4. It includes the UART
shell, MiniShell-owned SD mount, native ELF app manager, minimal runtime API, and
a separately built `hello.elf` example.

**The framework has not yet been validated on physical Tab5 hardware.** The next
step is to build it with ESP-IDF, flash the Tab5, and run the Task 0 session in
`docs/task0.md`.
