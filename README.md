# MiniShell

MiniShell is a lightweight MCU application environment that provides a shell,
runtime-loadable native applications, reusable system services, and a stable
application ABI.

MiniShell is not intended to be a small Linux clone. It adopts only the
operating-system ideas that are useful on an MCU:

- a resident shell
- runtime-loadable applications
- a stable application ABI
- reusable system services
- clear hardware ownership
- a simple foreground application lifecycle
- diagnostics available before an application starts

Protection is by convention rather than privilege separation. A normal
application uses MiniShell services and does not own shared system hardware. A
trusted application may access hardware directly when necessary, but doing so
leaves the portable MiniShell contract; if it corrupts state or crashes the MCU,
the application developer owns the bug.

## Reference Platform

The first reference platform is the **M5Stack Tab5 / ESP32-P4**. Tab5 provides
enough PSRAM and peripherals to develop the architecture without forcing early
memory compromises.

ESP-IDF may be used heavily below the platform boundary. The public MiniShell ABI
must not expose ESP-IDF types or require portable applications to use ESP-IDF
APIs.

## User Model

After power-on, MiniShell presents a local shell:

```text
MiniShell 0.1

M$> ls
flash/
sd/

M$> hello
Hello from a MiniShell ELF app.
M$>
```

An unknown shell command may be resolved through an application search path such
as `/sd/apps`.

## V1 Application Model

V1 focuses on native ELF applications.

```text
                 application.elf
                       |
                 MiniShell ABI
================================================
                 MiniShell core
      shell / app manager / resident services
================================================
              platform implementation
             ESP-IDF + Tab5 hardware
================================================
                    ESP32-P4
```

ELF is the initial loading format, not the MiniShell architecture itself. Future
runtimes such as MicroPython or WASM may coexist with the same service model.

The current native app contract is:

```text
main(argc, argv)
    -> mini_api_get()
    -> resident MiniShell service tables
```

## Core Principles

1. **One hardware owner.** MiniShell owns shared hardware and exposes services.
2. **Runtime services, not app-owned drivers.** Portable apps normally use
   MiniShell APIs instead of repeatedly integrating platform drivers.
3. **Small stable ABI.** The application boundary is MiniShell-owned and remains
   independent of ESP-IDF.
4. **Portable source, architecture-specific binaries.** An app can be rebuilt for
   another supported CPU while keeping the same MiniShell API.
5. **Convention instead of protection.** MiniShell does not attempt process or
   memory isolation.
6. **Direct hardware access remains possible.** It is an explicit trusted escape
   hatch, not the normal app model.
7. **Independent optimization.** MiniShell services and apps may evolve
   independently while the ABI remains compatible.
8. **Diagnose the platform first.** Services should be independently testable
   before an application is blamed.
9. **Top-down modular design.** Boundaries and ownership are defined before
   implementation details.
10. **Keep it understandable.** MiniShell should remain small enough to study,
    debug, and port.

## Task 0 — Complete

Task 0 proved the smallest working vertical slice on real hardware:

```text
power on
   -> USB Serial/JTAG M$> shell
   -> MiniShell mounts /sd
   -> hello
   -> load hello.elf
   -> hello calls mini_api_get()
   -> hello calls system.write()
   -> hello returns
   -> unload
   -> M$>
```

Task 0 is complete and hardware-validated on M5Stack Tab5 / ESP32-P4 rev v1.3.
See [`docs/task0.md`](docs/task0.md).

## Task 1 — ABI Foundation

Task 1 defines, implements, and independently validates six foundational ABIs
before adding real applications:

```text
system
memory
filesystem
time/location
display
input
```

Display and Input are separate fundamental services. A console/terminal is a
higher-level composition rather than a basic ABI.

Each ABI follows the same development path:

```text
define contract
   -> implement service logic
   -> build comprehensive unit tests
   -> run/fix unit suite
   -> build focused ELF integration test
   -> validate real hardware/backend behavior
   -> repeat lifecycle checks where relevant
```

The test hierarchy is deliberate:

```text
unit tests      primary correctness and regression coverage
ELF tests       binary ABI / loader / runtime integration
hardware tests  real platform/backend behavior
```

Runtime-loaded integration tests:

```text
abi_system.elf
abi_memory.elf
abi_fs.elf
abi_time_location.elf
abi_display.elf
abi_input.elf
```

The ELF tests use only the public MiniShell ABI and must not include
platform-private headers. They are intentionally smaller than the unit suites.

The ABI is designed for compatible growth through append-only tables,
`struct_size`, capability bits, optional sub-APIs, stable numeric meanings, and
explicit ownership/lifetime rules.

`med` is postponed until all six foundational boundaries are proven.

## ABI Documents

Cross-cutting contracts:

- [`docs/design-principles.md`](docs/design-principles.md)
- [`docs/architecture.md`](docs/architecture.md)
- [`docs/app-abi.md`](docs/app-abi.md)
- [`docs/abi-foundation.md`](docs/abi-foundation.md)

Canonical service contracts:

- [`docs/system-abi.md`](docs/system-abi.md)
- [`docs/memory-abi.md`](docs/memory-abi.md)
- [`docs/filesystem-abi.md`](docs/filesystem-abi.md)
- [`docs/time-location-abi.md`](docs/time-location-abi.md)
- [`docs/display-abi.md`](docs/display-abi.md)
- [`docs/input-abi.md`](docs/input-abi.md)

Milestones:

- [`docs/task0.md`](docs/task0.md)
- [`docs/task1.md`](docs/task1.md)

## Current Status

**Task 0 is complete. Task 1 ABI Foundation is active.**

Task 0 validated:

- interactive interrupt-driven USB Serial/JTAG shell
- MiniShell-owned FAT32 microSD mounted at `/sd`
- `help`, `status`, and `ls` shell commands
- runtime lookup of `/sd/apps/hello.elf`
- native RISC-V ELF loading through `espressif/elf_loader`
- runtime binding through `mini_api_get()`
- resident `system.write()` call from the ELF
- clean return to the shell
- repeated load/run/unload cycles without reboot or an obvious leak
- shell availability even when SD initialization fails

Task 1 has resident service-core implementations for all six foundational ABIs
plus one host unit-test group per ABI. The clean host suite passes 6/6 with
`-Wall -Wextra -Werror` and also passes 6/6 under AddressSanitizer and
UndefinedBehaviorSanitizer.

The serial-terminal backend now exposes Display and Input through the same
USB Serial/JTAG transport already used by the shell. On real M5Stack Tab5 /
ESP32-P4 rev v1.3 hardware, all six focused runtime-loaded ELF tests pass:

```text
abi_system         PASS
abi_memory         PASS
abi_fs             PASS
abi_time_location  PASS
abi_display        PASS
abi_input          PASS
```

This validates the public table layout, `mini_api_get()` binding,
function-pointer calling convention, service logic, ELF loader integration,
serial-terminal Display output, interactive terminal Input routing, and normal
foreground return to the shell. `hello.elf` was also run successfully immediately
after the Display/Input tests, confirming that the shell remained healthy after
foreground app handoff and teardown.

Long FAT filenames are enabled and functionally verified: long-named ABI ELF
files load successfully and the Filesystem ABI long-name test passes. The earlier
legacy Tab5 BSP long-filename warning has been suppressed at its BSP log tag while
preserving BSP errors.

Physical Tab5 LCD/touch integration and optional RTC/default-location backends are
separate later platform work; they are not required for the serial-terminal ABI
validation above.
