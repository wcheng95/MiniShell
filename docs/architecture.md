# MiniShell Architecture

## 1. System view

MiniShell is a resident MCU application environment.

```text
+------------------------------------------------------+
|                    Applications                      |
|       cat.elf   nano.elf   future minift8.elf ...   |
+----------------------- MiniShell ABI ----------------+
|                    MiniShell Core                    |
|                                                      |
| shell   app manager   resident facilities   services |
|              |             |                        |
|              |             +-- file transfer        |
|              `---------------- power/system          |
+-------------------- service boundary ----------------+
|                  Platform Services                   |
|                                                      |
| system memory filesystem time/location display input|
|                 future: audio USB network            |
+-------------------- platform boundary ---------------+
|              ESP32-P4 / ESP-IDF / Tab5              |
+------------------------------------------------------+
```

Portable applications depend on MiniShell, not directly on ESP-IDF or Tab5
hardware.

## 2. Reference platform

Current reference platform:

- M5Stack Tab5
- ESP32-P4
- native RISC-V ELF applications
- external PSRAM
- microSD as app/user storage
- USB Serial/JTAG as the first shell, Display/Input, and file-transfer transport
- ESP-IDF as the platform SDK

ESP-IDF dependencies stop at the public MiniShell ABI.

## 3. Major modules

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
|   |-- minishell_services/
|   |-- minishell_transfer/
|   `-- minishell_power/
|-- apps/
|   `-- cat/
|-- platform/
|   `-- minishell_platform_tab5/
|-- examples/
|-- tools/
|   |-- minishell_transfer.py
|   `-- task2_validate.py
`-- tests/
```

Directory names may evolve. Responsibility, dependency, and ownership boundaries
matter more than source-file aesthetics.

## 4. Resident vs application placement

Placement follows the policy in `docs/resident-vs-app.md`.

Keep functionality resident when MiniShell needs it to manage, provision,
diagnose, recover, power, or own the application environment. Ordinary
user/domain functionality normally belongs in a separately built ELF.

```text
resident
    shell
    app loader/lifecycle
    ABI services
    platform ownership
    diagnostics/recovery
    file transfer
    power/system ownership

applications
    cat
    nano
    cp / mv / rm
    mkdir / rmdir
    free / date / df
    future MiniFT8
```

Resident does not mean monolithic. A shell built-in should normally dispatch to a
small resident module rather than implementing the whole feature in `shell.c`.

BusyBox-style bundling is not the default. Use independent ELFs first; bundle
closely related tiny tools only if measurement later justifies it.

Ordinary commands use familiar Linux names when the implemented behavior is
close enough to avoid surprise. MiniShell implements the minimum useful subset,
not the full GNU/POSIX option surface. See `docs/command-roadmap.md`.

## 5. Shell

The shell is the user-facing control plane.

Current responsibilities include:

- command-line input and parsing;
- built-in command dispatch;
- filesystem listing;
- application search/launch;
- lifecycle stress command;
- platform/service diagnostics;
- dispatch of resident provisioning commands such as `put` and `get`;
- dispatch of resident power commands such as `status`, `suspend`, and `poweroff`.

The shell does not own file-transfer protocol or board power mechanics. It
delegates to the relevant resident module.

V1 does not need POSIX pipelines, redirection, background jobs, users, or process
management.

## 6. App manager

The app manager owns application lifecycle.

```text
shell command
    |
resolve app
    |
load / relocate ELF
    |
MiniShell app begin
    |
main(argc, argv)
    |
application uses mini_api_get()
    |
application returns
    |
MiniShell resource cleanup
    |
unload ELF
    |
restore shell foreground
```

One foreground native application runs at a time. No process isolation is
provided.

## 7. Foundational service layer

Task 1 established six application-facing service ABIs:

```text
system
memory
filesystem
time/location
display
input
```

Cross-cutting rules include:

- append-only tables;
- `struct_size` field checks;
- capability bits and optional sub-APIs;
- stable numeric meanings;
- fixed-width MiniShell-owned public types;
- explicit ownership/lifetime;
- synchronous application-context V0 behavior;
- architecture-specific binaries from portable source.

A console is not itself an application ABI. The serial terminal is currently a
platform implementation of Display/Input plus shell transport behavior.

Later application-facing service groups may include audio, USB, network, and
possibly power if an application requirement justifies it. Resident Task 3 did
not add a speculative public Power ABI.

## 8. Filesystem namespace

Initial logical storage namespace:

```text
/sd
future: /flash
```

Applications use the Filesystem ABI and do not initialize or own storage
hardware.

Resident facilities may compose the established service semantics internally.
File transfer uses the Filesystem service for data I/O while platform-private
callbacks handle the final filesystem publication operation that is intentionally
not part of the current app ABI.

## 9. Resident file transfer

Task 2 established a resident bootstrap/recovery facility:

```text
shell put/get
     |
     v
minishell_transfer
     |                    |
     |                    +--> Filesystem service
     |
     `--> raw byte-stream callbacks
                              |
                              v
                       USB Serial/JTAG
```

The transfer module owns:

- MFT1 framing;
- ready handshakes;
- block pacing;
- raw payload transfer;
- CRC-32 verification;
- timeout/error handling;
- temporary-file workflow.

The platform owns:

- raw transport driver access;
- completed-file replace/remove operations.

On receive, MiniShell writes `<destination>.mft.part`, verifies and syncs the
complete payload, then publishes it. FATFS cannot rename over an existing name,
so the Tab5 backend temporarily moves the old destination to
`<destination>.mft.bak`, publishes the verified new file, and removes the backup;
failed publication attempts rollback the old destination where possible.

Task 2 is complete. Hardware validation proved round-trip transfer, replacement,
a 524325-byte binary, timeout cleanup, and preservation of the old destination
after an intentionally interrupted replacement.

## 10. Resident power/system

Task 3 established resident power ownership:

```text
shell status/suspend/poweroff
        |
        v
core/minishell_power
        |
        | private normalized callback port
        v
Tab5 power backend
        |
        +-- charger enable/status via IO expander
        +-- INA226 battery telemetry
        +-- ESP32-P4 deep sleep
        `-- board poweroff pulse
```

The resident core contains no ESP-IDF or M5Stack types. Board-specific details
remain below the platform boundary.

The V1 user-visible behaviors are:

```text
status      battery percentage + charging state
suspend     deep sleep; restart MiniShell on external wake/reset
poweroff    board shutdown request with deep-sleep fallback
```

All three paths pass on real Tab5 hardware. The backend also enables charging
during normal MiniShell startup. Power-backend initialization failure is non-fatal
so the shell remains available for diagnostics/recovery.

USB attach/detach detection is intentionally deferred to a later resident-system
milestone rather than being part of Task-3 completion.

## 11. Platform layer

The platform layer translates MiniShell concepts into hardware/SDK operations.

For Tab5 this currently includes:

```text
USB Serial/JTAG console transport
memory allocation support
SD / POSIX-VFS filesystem support
monotonic timer / delay
terminal Display backend
terminal Input backend
raw transfer byte stream
completed-file replace/remove
battery/charging telemetry
charger initialization
suspend/deep sleep
poweroff control
```

Later work may add USB attach/detach management, RTC/location persistence,
physical LCD/touch, audio, and network support.

Platform code may freely include ESP-IDF/M5Stack types. Public MiniShell headers
may not.

## 12. Hardware ownership

MiniShell owns shared hardware after boot.

Examples:

```text
application
   |
Filesystem ABI
   |
MiniShell filesystem service
   |
ESP-IDF VFS/FATFS
   |
SD hardware
```

```text
shell put/get
   |
resident transfer module
   |
private transport callbacks
   |
USB Serial/JTAG driver
```

```text
shell suspend/poweroff
   |
resident power module
   |
private platform callbacks
   |
Tab5 power hardware / ESP32-P4 sleep
```

Direct hardware access remains technically possible because MiniShell provides no
protection, but it leaves the portable contract.

## 13. Memory model and protection

MiniShell and applications share one MCU address space.

Current assumptions:

- no virtual memory;
- no process address-space isolation;
- no privilege boundary;
- one foreground loaded app;
- MiniShell remains resident;
- MiniShell-managed memory/files are reclaimed on normal app exit.

This is cooperative cleanup, not protection from arbitrary memory corruption.

## 14. Foreground Display/Input model

```text
shell foreground
    -> launch app
    -> app uses Display/Input ABI
    -> app returns
    -> stale queued app input discarded
    -> shell foreground restored
```

MiniShell remains the hardware owner throughout.

Resident transfer is different: while `put`/`get` runs, the shell synchronously
hands the serial transport to the transfer protocol; no foreground app is active.

## 15. Testing model

Testing has three layers:

```text
host unit tests
    -> focused ELF ABI integration
    -> hardware/platform validation
```

Task 1's six ABI groups remain the primary service correctness suites.

Task 2 added a resident transfer unit group. Task 3 adds
`resident_power_unit`, deliberately separate from the public ABI groups because
no Power ABI was added.

Hardware testing verifies platform behavior after resident/module semantics are
covered on the host.

Application commands should likewise keep command logic host-testable where
practical, then validate the separately built ELF on real MiniShell.

## 16. Milestones

### Task 0 — Framework proof — COMPLETE

Validated boot, shell, SD, ELF load, `mini_api_get()`, resident service call,
return, and unload.

### Task 1 — ABI Foundation — COMPLETE

System, Memory, Filesystem, Time/Location, Display, and Input are documented,
implemented, unit tested, ELF tested, hardware tested through the available
backend, and lifecycle-stress tested through 100 repeated ELF runs.

### Task 2 — Resident File Transfer — COMPLETE

Resident `put`/`get` over USB Serial/JTAG is implemented and validated. MFT1 uses
CRC verification, paced multi-block transfer, safe temporary-file publication,
and FATFS replacement/rollback.

See `docs/task2.md`.

### Task 3 — Power/System — COMPLETE

Resident battery/charging status, ESP32-P4 deep-sleep suspend, and Tab5 poweroff
are implemented and validated on real hardware. No public Power ABI was required.

USB attach/detach detection is deferred to a later system milestone.

See `docs/power-system-plan.md`.

### Applications

Ordinary functionality continues independently as ELF applications following
`docs/command-roadmap.md`. The planned set remains intentionally small.
