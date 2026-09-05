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
- diagnostics, provisioning, and recovery facilities

Protection is by convention rather than privilege separation. A normal
application uses MiniShell services and does not own shared system hardware. A
trusted application may access hardware directly when necessary, but doing so
leaves the portable MiniShell contract.

## Reference Platform

The first reference platform is the **M5Stack Tab5 / ESP32-P4**. ESP-IDF may be
used heavily below the platform boundary, but the public MiniShell ABI must not
expose ESP-IDF types or require portable applications to use ESP-IDF APIs.

## User Model

```text
MiniShell

M$> ls
sd/

M$> hello
Hello from a MiniShell ELF app.
M$>
```

An unknown shell command may be resolved through an application search path such
as `/sd/apps`.

## Application Model

```text
                 application.elf
                       |
                 MiniShell ABI
================================================
                 MiniShell core
 shell / app manager / resident facilities / services
================================================
              platform implementation
             ESP-IDF + Tab5 hardware
================================================
                    ESP32-P4
```

The current native app contract is:

```text
main(argc, argv)
    -> mini_api_get()
    -> resident MiniShell service tables
```

ELF is the initial loading format, not the MiniShell architecture itself.

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
7. **Independent evolution.** MiniShell and runtime applications can evolve
   independently while the ABI remains compatible.
8. **Diagnose the platform first.** Services should be independently testable
   before an application is blamed.
9. **Top-down modular design.** Boundaries and ownership are defined before
   implementation details.
10. **Keep it understandable.** MiniShell should remain small enough to study,
    debug, and port.

## Resident MiniShell vs `.elf` Applications

Placement follows one primary rule:

> If MiniShell needs a function to manage, provision, diagnose, recover, or own
> the application environment itself, keep it resident. Ordinary user/domain
> functionality should normally be a separately built `.elf` application.

Examples:

```text
resident MiniShell
    shell / loader / ABI services
    platform ownership
    diagnostics / recovery
    file transfer

runtime applications
    med.elf
    calculator.elf
    radio applications
    future MiniFT8
```

Do not begin with BusyBox-style packaging. One ELF per ordinary tool/application
remains the default; related tiny tools may be bundled later only if measurements
show a real benefit.

See [`docs/resident-vs-app.md`](docs/resident-vs-app.md).

## Task 0 — Complete

Task 0 proved the smallest working vertical slice on real hardware:

```text
power on
   -> USB Serial/JTAG M$> shell
   -> MiniShell mounts /sd
   -> load hello.elf
   -> hello calls mini_api_get()
   -> hello calls system.write()
   -> return / unload
   -> M$>
```

See [`docs/task0.md`](docs/task0.md).

## Task 1 — Complete: ABI Foundation

Task 1 defines, implements, and validates six foundational ABIs:

```text
system
memory
filesystem
time/location
display
input
```

The test hierarchy is deliberate:

```text
unit tests      primary correctness and regression coverage
ELF tests       binary ABI / loader / runtime integration
hardware tests  real platform/backend behavior
```

All six focused ELF tests pass on real Tab5/ESP32-P4 hardware:

```text
abi_system         PASS
abi_memory         PASS
abi_fs             PASS
abi_time_location  PASS
abi_display        PASS
abi_input          PASS
```

Lifecycle stress also passes:

```text
repeat 20 abi_stress     PASS 20/20
repeat 100 abi_stress    PASS 100/100
```

The public boundary review found no ESP-IDF, FreeRTOS, FATFS, or M5Stack type
leaking into the application ABI.

See [`docs/task1.md`](docs/task1.md).

## Task 2 — Active: Resident File Transfer

Task 2 adds bootstrap/recovery file transfer over the existing USB Serial/JTAG
transport without expanding the application ABI.

Resident shell commands:

```text
put <remote-path>    receive a host file
get <remote-path>    send a MiniShell file
```

The companion host tool performs the binary MFT1 handshake and CRC verification:

```bash
python3 tools/minishell_transfer.py /dev/ttyACM0 put local.elf /sd/apps/local.elf
python3 tools/minishell_transfer.py /dev/ttyACM0 get /sd/log.txt log.txt
```

`pyserial` is required by the host helper:

```bash
python3 -m pip install pyserial
```

The receive path writes to `<destination>.mft.part`, verifies the complete
payload CRC, syncs/closes it, and only then publishes it. On FATFS, updating an
existing destination uses a temporary backup/rollback sequence because FatFs
`f_rename()` does not overwrite an existing name.

Current Task 2 implementation includes:

```text
core/minishell_transfer/       resident MFT1 protocol module
shell put/get dispatch         control plane only
Tab5 raw USB serial backend    private platform callbacks
tools/minishell_transfer.py    host helper
abi_transfer_unit              fake-stream/fake-filesystem unit test
```

Task 2 hardware validation is the next step. See [`docs/task2.md`](docs/task2.md).

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

Milestones and placement policy:

- [`docs/task0.md`](docs/task0.md)
- [`docs/task1.md`](docs/task1.md)
- [`docs/task2.md`](docs/task2.md)
- [`docs/resident-vs-app.md`](docs/resident-vs-app.md)

## Current Status

**Task 0 and Task 1 are complete. Task 2 resident file transfer is active and
ready for host-unit/build/hardware validation.**

Physical Tab5 LCD/touch integration and optional RTC/default-location backends
remain later platform work. Display/Input are already proven through the serial
terminal backend.
