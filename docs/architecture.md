# MiniShell Architecture

## 1. System view

MiniShell is a resident MCU application environment.

```text
+------------------------------------------------------+
|                    Applications                      |
|          future: med.elf   minift8.elf   ...        |
+----------------------- MiniShell ABI ----------------+
|                    MiniShell Core                    |
|                                                      |
|  shell   app manager   diagnostics   service APIs    |
+-------------------- service boundary ----------------+
|                  Platform Services                   |
|                                                      |
| system memory filesystem time/location display input|
|                 future: audio USB network power     |
+-------------------- platform boundary ---------------+
|              ESP32-P4 / ESP-IDF / Tab5              |
+------------------------------------------------------+
```

The key rule is that portable applications depend on MiniShell, not directly on
ESP-IDF or Tab5 hardware.

## 2. Reference platform

V1 reference platform:

- M5Stack Tab5
- ESP32-P4
- native RISC-V applications
- external PSRAM for comfortable runtime loading and buffers
- microSD as an important app/user storage backend
- ESP-IDF as the underlying platform SDK

MiniShell may depend heavily on ESP-IDF inside the ESP32-P4 platform port. That
dependency stops at the public MiniShell ABI.

## 3. Major modules

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
|   `-- hello/
`-- tests/              unit tests + ABI integration support
```

Exact filenames may evolve. Responsibility and dependency boundaries matter more
than directory aesthetics.

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
- returning to `M$>` after app exit
- platform/service diagnostics
- future trusted configuration commands such as setting UTC/default location

V1 does not need POSIX pipelines, redirection, background jobs, users, or process
management.

The shell may consume MiniShell services internally but is not itself part of the
application ABI.

## 5. Filesystem namespace

MiniShell exposes storage through a logical namespace.

Initial targets include:

```text
/flash   internal persistent filesystem
/sd      microSD filesystem
```

Applications use the Filesystem ABI and do not initialize, mount, unmount, or own
the underlying storage hardware.

## 6. App manager

The app manager owns application lifecycle.

```text
shell command
    |
resolve app name/path
    |
validate/load ELF
    |
prepare app context
    |
main(argc, argv)
    |
app obtains mini_api_get()
    |
application runs in foreground
    |
application returns
    |
cleanup MiniShell-managed app resources
    |
unload ELF
    |
restore shell foreground
```

V1 runs one foreground native application at a time. No process isolation is
provided.

## 7. Foundational service layer

Task 1 defines and independently validates these six foundational application
ABIs:

```text
system
memory
filesystem
time/location
display
input
```

Later service groups may include:

```text
audio
USB
network
power/system control
```

`console` is not a foundational ABI. A terminal/console is a higher-level
composition of output and input behavior. `system.write()` is a diagnostic sink,
not application UI.

A service is not considered established merely because a table exists. Each
foundational ABI requires:

1. a platform-neutral documented contract;
2. one clear hardware/state owner;
3. a resident implementation behind a clean platform boundary;
4. comprehensive unit tests as the primary correctness/regression suite;
5. a focused separately built ELF integration test;
6. real-hardware validation for platform-dependent behavior;
7. defined success/error behavior;
8. defined ownership/cleanup behavior;
9. a reviewed backward-compatible extension path.

Canonical contracts live in the standalone `*-abi.md` documents.

## 8. Platform layer

The platform layer translates MiniShell concepts into hardware/SDK operations.

For Tab5 this may include:

```text
platform/minishell_platform_tab5/
    boot/startup
    diagnostic transport
    memory allocator support
    storage/filesystem support
    monotonic timer / RTC / location sources
    display
    input
    future audio / USB / network / power
```

The platform layer may freely include ESP-IDF and M5Stack-specific headers.
Public MiniShell headers may not.

The ELF loader is resident infrastructure used by the app manager. It is not an
application-facing hardware service.

## 9. Hardware ownership

MiniShell owns shared hardware after boot.

Example filesystem path:

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

Example RTC path:

```text
application
   |
utc_get() / utc_set()
   |
Time/Location service
   |
MiniShell-owned RTC driver
   |
hardware RTC
```

An application may request a global state change through the ABI when an
operation such as `utc_set()` or default-location setting is supported. It still
does not own or manipulate the RTC/storage device directly.

Direct hardware access remains technically possible because MiniShell provides no
protection. Such access leaves the portable contract.

## 10. Replaceable internals

MiniShell modularity is not simply source-file splitting. The goal is that one
implementation can change without forcing unrelated modules or apps to change.

Examples:

```text
change FATFS backend              -> Filesystem ABI unchanged
change diagnostic transport       -> System ABI unchanged
change allocator internals        -> Memory ABI unchanged
change RTC/GPS implementation     -> Time/Location ABI unchanged
change TFT/e-paper implementation -> Display ABI unchanged
change keyboard/terminal source   -> Input ABI unchanged
change platform port              -> app source rebuilds, not redesigns
```

This requires small interfaces, explicit ownership, and no private-type leakage.

It also improves testability: service policy/state logic should sit above narrow
platform interfaces so host unit tests can replace hardware backends with fakes.

## 11. Memory model

MiniShell and applications share the MCU address space.

V1 assumptions:

- no virtual memory
- no process address-space isolation
- no privilege boundary
- one foreground loaded app
- MiniShell remains resident while the app runs
- loaded app memory is reclaimed after exit where the ELF loader permits

The Memory ABI gives apps ordinary dynamic memory without exposing the platform
heap and lets MiniShell track app-owned allocations for normal teardown.

MiniShell cannot safely recover from arbitrary memory corruption.

## 12. ABI boundary

Task 0 validated:

```text
app.elf
   |
main(argc, argv)
   |
mini_api_get()
   |
versioned mini_api_t
   |
resident services
```

Cross-ABI rules are centralized in `docs/abi-foundation.md`:

- append-only tables
- `struct_size` field-presence checks
- capability bits and optional sub-APIs
- stable numeric meanings
- fixed-width MiniShell-owned public types
- explicit ownership/lifetime
- source portability with architecture-specific binaries
- application-context, synchronous-first V0 behavior

## 13. Resource ownership

MiniShell remains the hardware/service owner. The foreground app may own logical
resources produced through MiniShell APIs.

```text
foreground app context
    +-- MiniShell-managed allocations
    +-- open file handles
    `-- future logical service resources
```

Normal app teardown reclaims remaining MiniShell-managed resources before ELF
unload where practical.

This is cooperative cleanup, not protection.

## 14. Foreground Display/Input model

V0 has one foreground app, one primary logical display, and one primary logical
input stream.

```text
shell foreground
    -> launch app
    -> app uses Display/Input ABI
    -> app returns
    -> stale queued app input is discarded
    -> shell foreground restored
```

No acquire/release handles are needed in V0. MiniShell remains the physical
hardware owner throughout.

## 15. ABI testing

Testing has three layers with different responsibilities.

### Unit tests — primary

Every foundational service has a comprehensive unit suite. Wherever practical,
service semantics are exercised through ABI-shaped interfaces over fake platform
backends.

Unit tests carry the broad behavioral coverage:

```text
success/error paths
boundary values
struct_size compatibility
capability combinations
resource bookkeeping
state transitions
partial/failure behavior
timeouts/freshness
cleanup and repeated operations
```

They should be fast and deterministic enough to run constantly during
implementation and refactoring.

### Runtime-loaded ELF integration tests

Task 1 integration tests are:

```text
abi_system.elf
abi_memory.elf
abi_fs.elf
abi_time_location.elf
abi_display.elf
abi_input.elf
```

Each is separately built and uses only public MiniShell headers. Their purpose is
not exhaustive behavior coverage; they prove the real binary/runtime path:

```text
ELF app
   -> loader
   -> mini_api_get()
   -> public table layout/calling convention
   -> resident service
   -> teardown/unload
```

### Hardware/platform validation

Real Tab5 testing proves backend behavior that unit mocks cannot establish, such
as SD persistence, RTC retention, hardware timer behavior, physical display
refresh, and terminal/touch input routing.

The preferred sequence is:

```text
unit suite
    -> focused ELF integration
    -> hardware/backend validation
```

A passing ELF or hardware smoke test does not replace a failing or incomplete
unit suite.

## 16. Diagnostics as architecture

Platform/service state should be inspectable independently of user apps.

Examples may include:

```text
M$> status
M$> mem
M$> storage status
M$> ls /sd
M$> date
M$> location
M$> rtc status
```

A service that works independently narrows later application debugging sharply.

## 17. Development milestones

### Task 0 — Framework proof — COMPLETE

Validated on real Tab5 / ESP32-P4 hardware:

1. boot to `M$>` over USB Serial/JTAG;
2. mount microSD;
3. load `/sd/apps/hello.elf`;
4. resolve `mini_api_get()`;
5. call resident `system.write()`;
6. return and unload;
7. repeat without rebooting.

See `docs/task0.md`.

### Task 1 — ABI Foundation — ACTIVE

Define, implement, and independently validate:

```text
system
memory
filesystem
time/location
display
input
```

All six design contracts are provisional and documented. Implementation now uses
unit-test-first verification, followed by focused ELF integration and required
real-hardware validation.

See `docs/task1.md`.

### Later milestones

After Task 1 is proven, add the first real application. `med` remains a strong
candidate because it can exercise several established services without requiring
a specialized hardware path.

Additional ABIs should be added only when justified by real system/application
requirements.
