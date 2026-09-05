# Task 1 - ABI Foundation

## Goal

Task 1 develops and validates a small set of basic MiniShell application ABIs
before adding real user applications.

Task 0 proved that a separately built ELF can be loaded, call a resident
MiniShell service, return, unload, and leave the shell healthy. Task 1 now turns
that proof into a useful, testable runtime contract.

`med` is postponed until these basic services are defined, implemented, and
validated independently.

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

## Task 1 ABI Set

Initial development order:

```text
1. system
2. memory
3. filesystem
4. console/input
5. time
```

This order may be adjusted if implementation exposes a dependency, but no real
application should drive new ABI design during Task 1.

### 1. System ABI

Task 0 already proved the minimal system service:

```text
system.write()
```

Task 1 should formalize its contract and test it as part of the ABI suite rather
than relying only on the original `hello.elf` demonstration.

Do not add unrelated system calls merely to make the table look complete.

### 2. Memory ABI

Real applications will need dynamic memory without importing the platform heap
API directly.

The exact memory ABI remains to be designed. It should begin with the smallest
useful allocation contract and define ownership, failure behavior, and cleanup at
application exit.

Potential operations are intentionally not frozen here.

### 3. Filesystem ABI

The filesystem ABI v0 design is already defined in `docs/app-abi.md`.

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

### 4. Console/Input ABI

The console/input ABI should provide platform-neutral text output and normalized
input events without exposing USB Serial/JTAG details or ANSI byte parsing to an
application.

Exact operations and event structures are still to be designed.

Likely concepts include:

```text
write text
read normalized key event
terminal geometry
basic screen/cursor operations
```

Only operations justified as generally useful MiniShell primitives should enter
the ABI.

### 5. Time ABI

The time ABI should give applications useful timing without exposing ESP-IDF
timers or RTC drivers.

The design must distinguish at least conceptually between:

```text
monotonic elapsed time
wall-clock / UTC time
```

The exact V0 function set is still to be decided and tested independently.

## Focused ABI Tests

Use one separately built ELF test per service. Suggested names:

```text
abi_system.elf
abi_memory.elf
abi_fs.elf
abi_console.elf
abi_time.elf
```

These tests should call the public ABI exactly as a future application would.
They must not include platform headers or bypass MiniShell services.

Each test should be small enough that its expected behavior is obvious from
reading the source.

## Test Layers

Each ABI should have two useful test levels where practical.

### Host/unit tests

Test platform-independent policy and bookkeeping without hardware when that is
useful, for example:

- flag validation
- handle validation
- resource tables
- error translation helpers
- cleanup logic

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

Task 1 should establish a reusable resource-ownership pattern rather than invent
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
6. the interface still looks small and platform-neutral after implementation.

Prefer adding fields/functions compatibly rather than leaking implementation
assumptions into the public boundary.

## Task 1 Success Criteria

Task 1 is complete when:

1. system, memory, filesystem, console/input, and time ABIs have documented V0
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
10. Task 0's shell/load/run/unload behavior remains intact.

## After Task 1

Only after the basic ABI suite is proven should MiniShell add a real application.

`med` remains a strong candidate because it will exercise memory, filesystem,
console/input, and application lifecycle together without requiring specialized
hardware.

At that point the application should consume already-proven MiniShell services
rather than define the platform architecture while it is being written.
