# MiniShell Design Principles

## Purpose

MiniShell is a platform-adaptive application runtime. Its purpose is to keep application cores independent of Linux, NuttX, ESP-IDF, board drivers, and mocks while providing stable services and an application lifecycle.

Linux Mint on `pc-1` is the reference implementation and a full production target. Portability shapes the interfaces; it does not require every platform implementation to be identical.

## 1. Top-down design

Start from required application behavior, then define responsibilities downward:

```text
application behavior
    |
MiniShell public ABI
    |
portable service/runtime semantics
    |
private platform backend
    |
OS / RTOS / SDK / drivers
    |
hardware
```

Do not start from a platform API and expose it upward merely because it exists.

## 2. Application core stays platform-independent

A portable application depends only on `include/minishell/api.h` and its own internal modules.

Platform types must not cross the public boundary:

```text
POSIX fd / DIR *
errno
ESP-IDF types
FreeRTOS handles
NuttX driver objects
board-driver objects
```

MiniShell-owned fixed-width structures, result codes, capability bits, and opaque handles are the public contract.

## 3. One owner per shared resource/state domain

Each application-visible resource has one MiniShell owner.

Examples:

```text
Memory service       app allocation bookkeeping/policy
Filesystem service   logical paths, handles, quota semantics
Time/Location        UTC/location state and correction policy
Display service      logical display semantics
Input service        normalized logical input queue
App manager          foreground app lifecycle
```

On Linux the kernel/OS physically owns many resources. The one-owner rule means applications still have exactly one MiniShell gateway and one MiniShell module owns the application-visible semantics.

On a thick embedded backend MiniShell may also directly own the driver/hardware.

## 4. Clean public and private boundaries

There are two important contracts:

- **Public ABI:** application-facing and deliberately stable.
- **Private backend boundary:** MiniShell-internal and free to evolve as implementations are cleaned up.

Backends may freely use POSIX, NuttX, ESP-IDF, or simulation code below the private boundary. Applications may not.

## 5. Stable ABI, small surface

Add public surface only for a demonstrated application need.

Prefer:

```text
append-only tables
struct_size
capability bits
optional sub-APIs
fixed-width public types
explicit ownership/lifetime
stable result values
```

Do not expand the ABI merely to imitate POSIX, Linux, or an SDK.

Recent examples of justified growth:

```text
MiniFT8/file management -> dir_open/read/close
storage/resource needs  -> filesystem space(path)
nano cursor             -> optional inverse text attribute
```

## 6. Platform-dependent capability is allowed

MiniShell is not a lowest-common-denominator abstraction.

Some functions are inherently platform-dependent:

```text
serial/USB/BLE recovery transfer
suspend/poweroff
special hardware controls
```

A platform may provide them when useful and omit them when meaningless. Do not create fake behavior simply so command sets look identical.

Portable applications should use capability discovery rather than assume optional functionality exists.

## 7. Resource policy must be real

A reported resource limit should correspond to an enforced resource domain.

On Linux the host has far more RAM and storage than an embedded target, so MiniShell can intentionally constrain the application environment. `free` and `df` report MiniShell-visible resources, not the raw PC capacity.

Resource policy belongs to MiniShell, below applications. Applications experience it through ordinary Memory/Filesystem service results.

## 8. Runtime packaging is a backend detail

The application model is:

```text
apps
run <app>
<app>
app returns
shell resumes
```

The physical form differs by target:

```text
Linux/Mint      .so + dlopen/dlsym/dlclose
Tab5/NuttX      loadable-app mechanism where practical
Cardputer ADV   compiled-in registry acceptable when dynamic loading costs too much
```

Application source does not contain loader-specific logic.

## 9. Resident versus application

Keep functionality resident when MiniShell itself needs it to manage the runtime, own shared state, diagnose the platform, or bootstrap/recover a target.

Make ordinary user/domain functionality an application.

Current Linux resident shell is intentionally small:

```text
help
status
apps
run <app>
exit
```

Current ordinary tools (`ls`, `cat`, `cp`, `nano`, `free`, `df`, `date`, etc.) are portable applications.

## 10. Mocks stay below MiniShell

Mocks emulate MiniShell providers, not domain application results.

Good:

```text
mock audio samples
mock radio transport
mock UTC/location source
mock filesystem backend
```

Bad architectural shortcut:

```text
mock FT8 decode result
mock scheduler decision
mock completed QSO
```

The application core should still execute its real domain logic.

## 11. Small understandable modules

There is no rigid line-count rule. A module should have one explainable responsibility and one coherent state/ownership domain.

Split when:

- several unrelated responsibilities accumulate;
- tests naturally separate into unrelated groups;
- changing one concern repeatedly risks another;
- understanding one file requires understanding several independent subsystems.

Do not split a coherent state machine merely to hit a size number, and do not accept a mixed-responsibility file merely because it is short.

`docs/consistency-check.md` records current modules that need decomposition.

## 12. Synchronous and understandable first

Prefer synchronous APIs and explicit lifecycle until a real requirement justifies concurrency or asynchronous contracts.

Do not add ISR safety, general reentrancy, worker tasks, queues, callbacks, or async state machines speculatively.

## 13. Tests at the boundary that matters

For each service:

1. test service semantics independently;
2. test the public ABI through a separately loaded application/probe;
3. test real platform behavior where platform-specific behavior matters.

Linux CI is the reference regression gate. Embedded ports should validate the same observable contract against their hardware/backend.

## 14. Linux first, embedded constraints always visible

New portable behavior is normally developed on Linux first because it gives fast builds, sanitizers/debugging, deterministic tests, CI, and easy mocks.

That does not mean designing like a desktop application. Keep embedded suitability visible:

```text
bounded resources
explicit ownership
controlled allocation
deterministic lifecycle
small interfaces
no accidental dependence on host-only facilities
```

## Review question

Before merging a new feature, ask:

> Does this make the application depend more strongly on MiniShell concepts, or more strongly on a particular platform?

The desired direction is always toward MiniShell concepts.
