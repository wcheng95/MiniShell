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
implement resident service
    |
build focused separately-built ELF test
    |
run on real Tab5 hardware
    |
exercise success + failure cases
    |
repeat launch/run/exit
    |
review boundary after implementation
    |
only then move to the next ABI
```

The focused ELF programs are infrastructure tests, not product apps.

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
- avoid unnecessary concurrency/asynchronous complexity in V0.

Task 1 uses the shared result namespace defined in `docs/abi-foundation.md`,
including `MINI_ERR_TIMEOUT` for finite timed operations.

## Compatibility model

Task 1 establishes these common rules:

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
- V0 calls are application-context and synchronous unless explicitly documented
  otherwise;
- source can be rebuilt across architectures while binaries remain
  architecture-specific.

## 1. System ABI

Canonical contract: `docs/system-abi.md`

V0:

```text
system
`-- write()
```

`system.write()` is the guaranteed diagnostic/system text sink. It is not the
application display surface and not a console abstraction.

Focused test:

```text
abi_system.elf
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
- normal app teardown reclaims leftovers;
- DMA/aligned/executable/special memory remains deferred.

Focused test:

```text
abi_memory.elf
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

Focused test:

```text
abi_fs.elf
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
|-- optional default-location get/set
`-- snapshot_get()
```

Important properties:

- monotonic time never follows UTC corrections;
- implementation should use a free-running hardware timer where practical rather
  than a periodic software tick solely for microsecond timekeeping;
- UTC is maintained from trusted sources such as RTC/GPS/NTP/manual setting;
- MiniShell exclusively owns any hardware RTC driver;
- trusted apps/shell may request UTC changes through `utc_set()` when supported;
- configured/default location is persistent MiniShell-owned state;
- live location is separate from configured/default location;
- `location_get()` returns the effective location and identifies its source;
- live freshness is exposed through monotonic update time;
- snapshot is flat to preserve ABI offsets;
- local timezone/geocoding remain outside V0.

Focused test:

```text
abi_time_location.elf
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

Focused test:

```text
abi_display.elf
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
- V0 guarantees ASCII input repertoire but uses a 32-bit Unicode scalar field;
- finite waits use MiniShell's internal monotonic timing source;
- pointer/touch/raw-keyboard/buttons remain separate future capability families;
- stale queued events must not leak across foreground app handoff.

Focused test:

```text
abi_input.elf
```

## Focused ABI tests

The complete Task 1 runtime-test set is:

```text
abi_system.elf
abi_memory.elf
abi_fs.elf
abi_time_location.elf
abi_display.elf
abi_input.elf
```

Each test:

- is built separately from MiniShell;
- uses `main(argc, argv)` plus `mini_api_get()`;
- includes only public MiniShell headers;
- checks field/service availability using the common compatibility rules;
- exercises normal and important error behavior;
- returns normally to the shell;
- is repeated to catch leaks/stale state/lifecycle damage.

## Test layers

### Host/unit tests

Use host tests where practical for platform-independent policy such as:

```text
flag validation
handle/ownership tables
allocation bookkeeping
error translation helpers
struct_size compatibility checks
event normalization
foreground queue cleanup
snapshot/layout rules
```

### Real-hardware ELF tests

The decisive ABI test is a separately built ELF running through the actual
MiniShell loader on Tab5:

```text
M$> abi_fs
[focused filesystem ABI tests]
PASS
M$>
```

This validates:

```text
ELF app
  -> mini_api_get()
  -> public MiniShell service table
  -> resident service
  -> platform implementation
  -> hardware/backend
```

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
2. resident implementation exists;
3. focused ELF test passes on real hardware;
4. important invalid/error paths have been exercised;
5. repeated runs do not leak or destabilize MiniShell;
6. teardown/foreground handoff works correctly;
7. the boundary remains platform-neutral after implementation;
8. its extension path has been re-reviewed for compatibility.

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

The order is chosen to build dependencies progressively rather than because later
services are less important.

## Task 1 success criteria

Task 1 is complete when:

1. all six V0 contracts remain documented and internally consistent;
2. each service has a resident implementation;
3. each service has a focused separately built ELF test;
4. each test uses only public MiniShell headers;
5. all focused tests pass on the Tab5 reference hardware;
6. important invalid/error cases fail cleanly;
7. repeated execution does not exhaust or corrupt MiniShell-managed resources;
8. normal app teardown reclaims app-owned MiniShell resources;
9. foreground Display/Input ownership returns cleanly to the shell;
10. no public ABI leaks platform-private types;
11. backward-compatible extension rules still hold after implementation;
12. Task 0 shell/load/run/unload behavior remains intact.

## After Task 1

Only after the six foundational boundaries are proven should MiniShell add real
user applications.

`med` remains a strong first candidate because it can consume already-proven
Memory, Filesystem, Display, Input, and lifecycle services instead of defining
those boundaries while being written.
