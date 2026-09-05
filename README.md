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
- diagnostics, provisioning, recovery, and power/system facilities

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

> If MiniShell needs a function to manage, provision, diagnose, recover, power,
> or own the application environment itself, keep it resident. Ordinary
> user/domain functionality should normally be a separately built `.elf`
> application.

Examples:

```text
resident MiniShell
    shell / loader / ABI services
    platform ownership
    diagnostics / recovery
    file transfer
    battery / suspend / poweroff
    future USB device-state manager

runtime applications
    cat.elf
    nano.elf
    cp.elf
    mv.elf
    rm.elf
    mkdir.elf
    rmdir.elf
    future MiniFT8
```

Do not begin with BusyBox-style packaging. One ELF per ordinary tool/application
remains the default; related tiny tools may be bundled later only if measurements
show a real benefit.

MiniShell uses familiar Linux command names when the behavior is close enough to
be unsurprising, but implements only the minimum useful subset rather than trying
to reproduce an entire GNU/Linux userland. The planned editor is therefore named
`nano`, replacing the earlier working name `med`.

See [`docs/resident-vs-app.md`](docs/resident-vs-app.md),
[`docs/command-roadmap.md`](docs/command-roadmap.md), and
[`docs/power-system-plan.md`](docs/power-system-plan.md).

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

All six focused ELF tests pass on real Tab5/ESP32-P4 hardware, and lifecycle
stress passes through 100 repeated ELF runs.

See [`docs/task1.md`](docs/task1.md).

## Task 2 — Complete: Resident File Transfer

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

MFT1 receive uses a temporary file, whole-file CRC-32, two startup handshakes,
1024-byte block acknowledgements, and FATFS replacement/rollback handling.

Real Tab5 hardware has validated small and large transfers, replacement,
interruption cleanup, and runtime installation/execution of `cat.elf`.

See [`docs/task2.md`](docs/task2.md).

## Task 3 — Complete: Power/System

Task 3 adds resident power ownership without changing the public application ABI.

Implemented and validated on real Tab5 hardware:

```text
status      battery percentage + charging state     PASS
suspend     ESP32-P4 deep sleep                     PASS
poweroff    Tab5 shutdown request                   PASS
```

Architecture:

```text
shell
  -> core/minishell_power
      -> private platform callbacks
          -> Tab5 INA226 + charger/power expander + ESP32-P4 sleep
```

The Tab5 backend explicitly enables charging during startup. Power initialization
failure is non-fatal so MiniShell remains usable as a diagnostic/recovery shell;
`status` reports unavailable fields when telemetry cannot be obtained.

Task 3 adds `resident_power_unit`. No `Power ABI` has been added to
`include/minishell/api.h`.

USB attach/detach detection remains planned, but is explicitly deferred to a
later resident-system milestone rather than keeping Task 3 open.

See [`docs/power-system-plan.md`](docs/power-system-plan.md).

## Command Roadmap

Linux is the naming/behavior reference for ordinary MiniShell commands, with
**minimum/useful** as the selection rule.

Planned application command set:

```text
Stage A: cat  nano  cp  mv  rm  mkdir  rmdir
Stage C: free  date  df
```

`cat` is kept because it is already implemented, not because it is considered
essential. Text search belongs inside `nano`; no separate `grep` is planned.

See [`docs/command-roadmap.md`](docs/command-roadmap.md).

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

Milestones and policy:

- [`docs/task0.md`](docs/task0.md)
- [`docs/task1.md`](docs/task1.md)
- [`docs/task2.md`](docs/task2.md)
- [`docs/resident-vs-app.md`](docs/resident-vs-app.md)
- [`docs/command-roadmap.md`](docs/command-roadmap.md)
- [`docs/power-system-plan.md`](docs/power-system-plan.md)

## Current Status

**Task 0, Task 1, Task 2, and Task 3 are complete.**

The ordinary ELF application roadmap remains independent and currently starts
with `nano`, followed by the minimal file-management command set. Future resident
system work includes USB device attach/detach detection when it becomes useful.
