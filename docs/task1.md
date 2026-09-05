# Task 1 - ABI Foundation

## Goal

Task 1 develops and validates a small set of basic MiniShell application ABIs
before adding real user applications.

Task 0 proved that a separately built ELF can be loaded, call a resident
MiniShell service, return, unload, and leave the shell healthy. Task 1 now turns
that proof into a useful, testable runtime contract.

`med` is postponed until these basic services are defined, implemented, and
validated independently.

The six basic Task 1 ABIs are:

```text
1. system
2. memory
3. filesystem
4. time
5. display
6. input
```

`console` is not one of the fundamental ABIs. A console/terminal is a higher-level
composition of text output plus text input and may later be built on top of the
basic services.

See `docs/abi-foundation.md` for the detailed design contract.

## Principle

Develop one ABI at a time:

```text
define contract
    |
implement resident service
    |
build focused ELF test
    |
run on real hardware
    |
exercise success + failure cases
    |
repeat launch/run/exit
    |
only then move to next ABI
```

The test ELF programs are infrastructure tests, not product applications.

Each public ABI must:

- use MiniShell-owned types only
- expose no ESP-IDF, M5Stack, FATFS, FreeRTOS, or driver-private objects
- have explicit ownership and lifetime rules
- have defined error semantics
- remain small enough to understand completely
- allow the implementation behind it to change independently
- be designed for append-only, backward-compatible extension where practical

## Compatibility Model

Task 1 establishes a common ABI evolution pattern:

- top-level service pointers are appended to `mini_api_t`
- service function tables begin with `struct_size`
- existing fields/functions are never reordered or repurposed after stabilization
- caller-owned extensible structures begin with `struct_size`
- optional capability families use capability bits and optional sub-API pointers
- unknown future capability bits are ignored by older applications
- public numeric meanings are never reused after ABI stabilization
- the same source API may be rebuilt for different CPU architectures; binaries
  themselves are architecture-specific

Display and input deliberately use sub-APIs so their boundaries can grow without
forcing today's text model to absorb every future hardware type:

```text
display
  `-- text          now
  `-- graphics      later

input
  `-- text          now
  `-- pointer       later
  `-- raw keyboard  later
  `-- buttons       later
```

## 1. System ABI

Task 0 already proved the minimal system service:

```text
system.write()
```

Task 1 formalizes it as a diagnostic/system text sink rather than a display
surface. It is tested explicitly through `abi_system.elf`.

Do not add unrelated system calls merely to make the table look complete.

## 2. Memory ABI

The memory ABI provides MiniShell-managed dynamic memory without exposing the
platform heap implementation.

Initial operations:

```text
alloc
realloc
free
get_info
```

Important properties:

- normal target-ABI alignment
- explicit allocation failure through MiniShell result codes
- failed realloc leaves the original allocation valid
- app-owned allocations are tracked by MiniShell
- remaining MiniShell-managed allocations are reclaimed during normal app exit
- DMA/special/aligned memory classes remain deferred

## 3. Filesystem ABI

Filesystem ABI v0 is defined in detail in `docs/app-abi.md` and summarized in
`docs/abi-foundation.md`.

Initial operations:

```text
open
close
read
write
seek
sync
stat
```

Important properties include:

- absolute MiniShell paths such as `/sd/...`
- opaque `mini_file_t` handles
- MiniShell-owned result codes
- partial reads/writes are valid
- EOF is successful zero-byte read
- no FATFS, libc `FILE *`, or ESP-IDF types cross the ABI
- MiniShell reclaims app-owned file handles during normal teardown

`readline`, directory operations, rename/remove, and other convenience functions
remain deferred until justified.

## 4. Time ABI

The time ABI keeps two fundamentally different concepts separate:

```text
monotonic time
UTC / wall-clock time
```

Initial operations:

```text
monotonic_us
utc_get
sleep_ms
```

Important properties:

- monotonic time never follows RTC/network clock corrections
- UTC is expressed independently of local timezone
- platforms may lack valid UTC while still supporting monotonic time
- sleep is a blocking/cooperative application delay, not a hard real-time
  guarantee
- alarms and richer timer services remain deferred

## 5. Display ABI

Display is an output service, not a console.

The initial capability is text display:

```text
display
  `-- text
       |-- get_info
       |-- clear
       `-- write_at
```

Text display uses explicit row/column coordinates and has no implicit terminal
cursor or ANSI semantics.

The top-level display ABI is designed so a later graphics sub-API can be appended
without changing the existing text contract.

## 6. Input ABI

Input is separate from display and initially exposes text-oriented logical input
rather than raw hardware scan codes.

Initial form:

```text
input
  `-- text
       `-- read normalized event
```

The text event model distinguishes character input from special keys and carries
logical modifiers where available.

Future pointer/mouse/touch-pointer, raw-keyboard, button, encoder, or other input
families are appended as separate optional sub-APIs rather than bloating or
changing the text event contract.

## Focused ABI Tests

Use one separately built ELF test per service:

```text
abi_system.elf
abi_memory.elf
abi_fs.elf
abi_time.elf
abi_display.elf
abi_input.elf
```

These tests call the public ABI exactly as a future application would. They must
not include platform headers or bypass MiniShell services.

Each test should be small enough that its expected behavior is obvious from
reading the source.

## Test Layers

Each ABI should have two useful test levels where practical.

### Host/unit tests

Test platform-independent policy and bookkeeping without hardware when useful,
for example:

- flag validation
- handle validation
- allocation/resource ownership tables
- error translation helpers
- cleanup logic
- event normalization
- compatibility/struct-size checks

### Real-hardware ELF tests

The decisive ABI test is a separately built ELF running through the real
MiniShell loader on Tab5:

```text
M$> abi_fs
[filesystem ABI tests]
PASS
M$>
```

The hardware test validates the whole boundary:

```text
ELF app
  -> MiniShell ABI table
  -> resident service
  -> platform implementation
  -> hardware/backend
```

## Resource Ownership

Task 1 establishes a reusable resource-ownership pattern rather than inventing
separate cleanup rules for every service.

Conceptually:

```text
foreground app context
    |
    +-- allocated memory
    +-- open file handles
    +-- future service resources
```

When the application returns normally, MiniShell releases any remaining
MiniShell-managed resources owned by that app before unloading its ELF.

This is cooperative lifecycle cleanup, not memory protection.

## ABI Evolution Rule

Task 1 is allowed to change the ABI while we learn. The ABI should not be called
stable merely because a struct exists in `api.h`.

A service becomes a candidate for stable ABI only after:

1. its semantics are documented,
2. its implementation exists,
3. its focused ELF test passes on real hardware,
4. error paths have been exercised,
5. repeated runs do not leak or destabilize MiniShell,
6. the interface still looks small and platform-neutral after implementation,
7. its extension path has been reviewed for backward compatibility.

Prefer adding fields/functions compatibly rather than leaking implementation
assumptions into the public boundary.

## Task 1 Success Criteria

Task 1 is complete when:

1. system, memory, filesystem, time, display, and input ABIs have documented V0
   contracts;
2. each service has a resident MiniShell implementation;
3. each service has a focused separately built ELF test;
4. each ELF test uses only public MiniShell headers;
5. all tests pass on the M5Stack Tab5 reference hardware;
6. important invalid/error cases fail cleanly;
7. repeated test execution does not exhaust MiniShell-managed resources;
8. application teardown reclaims resources owned through MiniShell APIs;
9. no public ABI exposes ESP-IDF, M5Stack, FATFS, FreeRTOS, USB-driver, or other
   platform-private types;
10. additive display/input growth remains possible without breaking the initial
    text contracts;
11. Task 0's shell/load/run/unload behavior remains intact.

## After Task 1

Only after the six basic ABI boundaries are proven should MiniShell add real user
applications.

`med` remains a strong candidate because it can then consume already-proven
memory, filesystem, display, input, time, and lifecycle services instead of
helping define the platform architecture while it is being written.
