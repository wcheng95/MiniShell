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

M$> ls
flash/
sd/

M$> ls /sd
apps/
logs/

M$> minift8
[MiniFT8 runs]

[application exits]
M$>
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

The first implementation milestone uses the ESP32-P4 built-in **USB Serial/JTAG
controller as the interactive terminal**. The same USB connection can be used for
flashing, `idf.py monitor`, and JTAG debugging. Display, touch, Wi-Fi, USB host,
audio, RTC, and other Tab5 features stay out of the framework until the basic app
lifecycle works.

Task 0 target:

```text
power on
   -> USB Serial/JTAG M$> shell
   -> MiniShell mounts /sd
   -> ls /sd/apps
   -> hello
   -> load hello.elf
   -> hello calls MiniShell runtime API
   -> hello returns
   -> unload
   -> M$>
```

See [`docs/task0.md`](docs/task0.md) for build steps and validation details.

## Documents

- [`docs/design-principles.md`](docs/design-principles.md)
- [`docs/architecture.md`](docs/architecture.md)
- [`docs/app-abi.md`](docs/app-abi.md)
- [`docs/task0.md`](docs/task0.md)

## Current Status

**Task 0 is complete and hardware-validated on M5Stack Tab5 / ESP32-P4 rev v1.3.**

Validated behavior includes:

- interactive USB Serial/JTAG shell using the interrupt-driven driver
- MiniShell-owned FAT32 microSD mounted at `/sd`
- `help`, `status`, and `ls` shell commands
- runtime lookup of `/sd/apps/hello.elf`
- native RISC-V ELF loading through `espressif/elf_loader` 1.3.3
- ELF application call into the resident MiniShell API through `mini_api_get()`
- clean application return to the resident shell
- repeated load/run/unload cycles without reboot or an obvious leak
- shell remains available when SD initialization fails

The next milestone can build on this validated vertical slice rather than adding
more loader or console infrastructure.
