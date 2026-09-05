# Task 1 - ABI Foundation

## Goal

Task 1 develops and validates a small set of foundational MiniShell application
ABIs before adding real user applications.

Task 0 proved that a separately built ELF can be loaded, obtain the resident API,
call a MiniShell service, return, unload, and leave the shell healthy. Task 1
turns that proof into a useful, reusable runtime contract.

`med` remains postponed until the foundational services are independently proven.

The six Task 1 ABIs are:

```text
1. system
2. memory
3. filesystem
4. time/location
5. display
6. input
```

`console` is not a foundational ABI. A console/terminal is a higher-level
composition of output and input behavior.

Canonical cross-ABI rules live in `docs/abi-foundation.md`. Detailed service
contracts live in the six standalone `*-abi.md` files.

## Development discipline

Develop and validate one ABI at a time:

```text
define contract
    |
implement service logic behind a clean platform boundary
    |
build comprehensive unit tests
    |
run/fix unit suite until semantics are solid
    |
build focused separately-built ELF integration test
    |
validate real platform/hardware behavior
    |
repeat launch/run/exit where lifecycle matters
    |
review boundary after implementation
    |
only then move to the next ABI
```

Unit tests are the primary correctness suite. The focused ELF programs are
integration/ABI-boundary tests, not the main source of behavioral coverage.

## Common requirements

Every public ABI must:

- use MiniShell-owned/fixed-width public types;
- expose no ESP-IDF, M5Stack, FATFS, FreeRTOS, or driver-private objects;
- document ownership and lifetime;
- document success and error semantics;
- obey the common `struct_size` and capability rules;
- remain small enough to understand completely;
- permit implementation changes behind the boundary;
- have a backward-compatible growth path where practical;
- avoid unnecessary concurrency/asynchronous complexity in V0;
- have a comprehensive unit-test suite;
- have a focused runtime-loaded ELF integration test;
- have real-hardware validation for platform-dependent behavior.

Task 1 uses the shared result namespace defined in `docs/abi-foundation.md`,
including `MINI_ERR_TIMEOUT` for finite timed operations.

## Compatibility model

Task 1 establishes these common rules:

- `abi_version` changes for incompatible ABI generations, not ordinary compatible
  feature additions;
- `mini_api_t` and service tables grow append-only;
- service function tables begin with `struct_size`;
- caller-owned extensible structures begin with `struct_size`;
- an app checks that `struct_size` reaches the specific field it needs rather
  than blindly requiring the newest whole-struct size;
- optional capability families use capability bits and optional sub-API pointers;
- stable numeric meanings are never reused;
- extensible public structs are not embedded by value when growth would shift
  outer-field offsets;
- resident API/service-table pointers remain valid for the current app execution;
- V0 calls are application-context and synchronous unless explicitly documented;
- native apps use the public C ABI and must match the resident target machine ABI;
- source can be rebuilt across architectures while binaries remain machine-ABI
  specific.

## 1. System ABI

Canonical contract: `docs/system-abi.md`

V0:

```text
system
`-- write()
```

`system.write()` is the guaranteed diagnostic/system text sink. It is not the
application display surface and not a console abstraction.

Verification:

```text
primary       unit tests
integration   abi_system.elf
hardware      diagnostic backend output
```

## 2. Memory ABI

Canonical contract: `docs/memory-abi.md`

V0:

```text
alloc
realloc
free
get_info
```

Important properties:

- ordinary application RAM only;
- normal target-ABI alignment;
- explicit `MINI_ERR_NO_MEMORY` failure;
- failed realloc leaves the original allocation valid;
- MiniShell tracks allocations per foreground app;
- exact app byte accounting uses requested live allocation sizes;
- normal app teardown reclaims leftovers;
- DMA/aligned/executable/special memory remains deferred.

Verification:

```text
primary       unit tests
integration   abi_memory.elf
hardware      allocator/backend behavior as needed
```

## 3. Filesystem ABI

Canonical contract: `docs/filesystem-abi.md`

V0:

```text
open
close
read
write
seek
sync
stat
```

Important properties:

- absolute logical paths such as `/sd/...`;
- opaque `mini_file_t` handles;
- partial reads/writes are valid;
- EOF is successful zero-byte read;
- failed `seek()` leaves the file position unchanged;
- APPEND writes always begin at EOF;
- no FATFS, libc `FILE *`, or ESP-IDF types cross the ABI;
- normal teardown reclaims remaining app file resources.

Verification:

```text
primary       unit tests with fake/in-memory backend where practical
integration   abi_fs.elf
hardware      real SD/filesystem backend
```

## 4. Time/Location ABI

Canonical contract: `docs/time-location-abi.md`

V0 groups related system state while keeping monotonic timing logically
independent from UTC/location.

```text
time/location
|-- monotonic_us()
|-- sleep_ms()
|-- utc_get()
|-- optional utc_set()
|-- location_get()
|-- optional default-location get/set/clear
`-- snapshot_get()
```

Important properties:

- monotonic time never follows UTC corrections;
- implementation should use a free-running hardware timer where practical rather
  than a high-frequency software tick solely for microsecond timekeeping;
- UTC is maintained from trusted sources such as RTC/GPS/NTP/manual setting;
- MiniShell exclusively owns any hardware RTC driver;
- trusted apps/shell may request UTC changes through `utc_set()` when supported;
- configured/default location is persistent MiniShell-owned state;
- default location may be set, read, or cleared when supported;
- live location is separate from configured/default location;
- `location_get()` returns the effective location and identifies its source;
- live freshness is exposed through monotonic update time;
- snapshot is flat to preserve ABI offsets;
- local timezone/geocoding remain outside V0.

Verification:

```text
primary       unit tests with fake clock/RTC/location/persistence backends
integration   abi_time_location.elf
hardware      real timer, RTC persistence, live-source behavior when available
```

## 5. Display ABI

Canonical contract: `docs/display-abi.md`

V0 represents one primary logical display and starts with text capability:

```text
display
|-- capabilities
|-- text
|   |-- get_info()
|   |-- clear()
|   |-- clear_at()
|   `-- write_at()
`-- present()
```

Important properties:

- logical character cells, not physical pixels;
- app queries rows/columns through `get_info()`;
- explicit coordinates; no implicit cursor;
- no ANSI/terminal semantics;
- `clear_at()` supports rectangular partial logical clear;
- `present()` separates logical drawing from physical refresh so TFT, buffered,
  and e-paper implementations can share the same ABI;
- graphics/framebuffer/multiple-display features are deferred extensions.

Verification:

```text
primary       unit tests against a fake logical display backend
integration   abi_display.elf
hardware      visual/physical display validation
```

## 6. Input ABI

Canonical contract: `docs/input-abi.md`

V0 begins with normalized logical key input:

```text
input
`-- key
    `-- read(event, timeout_ms)
```

The same primitive supports:

```text
timeout_ms = 0            non-blocking poll
finite timeout            wait up to N ms
0xFFFFFFFF                wait forever
```

Important properties:

- input events are logical rather than HID/scan-code/device-specific;
- character and special-key events are distinct;
- `codepoint` is a Unicode scalar from V0, while ASCII is the minimum guaranteed
  repertoire;
- finite waits use MiniShell's internal monotonic timing source;
- pointer/touch/raw-keyboard/buttons remain separate future capability families;
- stale queued events must not leak across foreground app handoff.

Verification:

```text
primary       unit tests with synthetic normalized events/fake monotonic clock
integration   abi_input.elf
hardware      terminal/touch/keyboard source validation
```

## Test hierarchy

### 1. Unit tests — primary

Every ABI must have a unit-test suite. This is the main correctness and regression
suite and should carry substantially more coverage than the runtime-loaded ELF
test.

Tests should run without hardware wherever practical by placing policy/state
logic above mockable platform interfaces.

Typical unit coverage includes:

```text
success paths
invalid arguments
boundary values
struct_size compatibility
capability combinations
error translation
ownership/bookkeeping
cleanup
partial/failure behavior
state transitions
timeouts/freshness
repeated operations
```

Tests should exercise the public ABI-shaped behavior whenever practical, even
when the backend underneath is fake.

### 2. Runtime-loaded ELF tests — integration

The Task 1 integration set remains:

```text
abi_system.elf
abi_memory.elf
abi_fs.elf
abi_time_location.elf
abi_display.elf
abi_input.elf
```

Each ELF test:

- is built separately from MiniShell;
- uses `main(argc, argv)` plus `mini_api_get()`;
- includes only public MiniShell headers;
- targets the same machine ABI as the resident runtime;
- checks field/service discovery through `struct_size`/capabilities;
- performs a representative happy path and selected error checks;
- returns normally to the shell;
- is repeated where useful to catch loader/resource/lifecycle problems.

The ELF tests do not need to repeat every unit-test case. Their job is to verify
the real binary boundary:

```text
separately built ELF
  -> loader
  -> mini_api_get()
  -> public table layout/calling convention
  -> resident service
  -> teardown/unload
```

### 3. Hardware/platform tests

Hardware validation covers behavior that mocks cannot prove:

```text
real SD persistence/error behavior
actual RTC retention/correction
hardware timer behavior
physical display output/present behavior
terminal/touch/keyboard input routing
platform-specific backend integration
```

Unit tests remain primary even after hardware tests pass. A hardware smoke test is
not a substitute for exhaustive semantic coverage.

## Resource ownership

Task 1 establishes one reusable ownership pattern:

```text
foreground app context
    +-- allocated memory
    +-- open file handles
    `-- future logical service resources
```

When an app returns normally, MiniShell releases remaining MiniShell-managed
resources associated with that app before ELF unload where practical.

This is cooperative lifecycle cleanup, not process protection.

## ABI evolution rule

Task 1 is allowed to change the ABI while implementation teaches us. A service is
not stable merely because a struct exists in `api.h`.

A service becomes a candidate for stable ABI only after:

1. semantics are documented;
2. resident implementation exists behind a clean platform boundary;
3. comprehensive unit tests pass;
4. important invalid/error/boundary paths are covered by tests;
5. the focused ELF integration test passes through the real loader/runtime;
6. required real-hardware behavior is validated;
7. repeated runs do not leak or destabilize MiniShell;
8. teardown/foreground handoff works correctly;
9. the boundary remains platform-neutral after implementation;
10. its extension path has been re-reviewed for compatibility.

## Recommended implementation order

Now that all six design contracts exist, use this sequence unless implementation
teaches us a reason to change it:

```text
1. system          already largely proven by Task 0
2. memory          establishes app resource bookkeeping
3. filesystem      establishes opaque-handle bookkeeping
4. time/location   supplies monotonic timing and persistent UTC/location state
5. display         establishes foreground output
6. input           uses monotonic timeout semantics and foreground routing
```

For each service, build its testable logic and unit suite before expanding the
runtime ELF integration test.

## Task 1 success criteria

Task 1 is complete when:

1. all six V0 contracts remain documented and internally consistent;
2. each service has a resident implementation;
3. each service has a comprehensive unit-test suite;
4. all unit suites pass;
5. each service has a focused separately built ELF integration test;
6. each ELF test uses only public MiniShell headers;
7. all focused ELF tests pass on the Tab5 reference runtime;
8. hardware-dependent behavior is validated on Tab5;
9. important invalid/error/boundary cases are covered and fail cleanly;
10. repeated execution does not exhaust or corrupt MiniShell-managed resources;
11. normal app teardown reclaims app-owned MiniShell resources;
12. foreground Display/Input ownership returns cleanly to the shell;
13. no public ABI leaks platform-private types;
14. backward-compatible extension rules still hold after implementation;
15. Task 0 shell/load/run/unload behavior remains intact.

## After Task 1

Only after the six foundational boundaries are proven should MiniShell add real
user applications.

`med` remains a strong first candidate because it can consume already-proven
Memory, Filesystem, Display, Input, and lifecycle services instead of defining
those boundaries while being written.
