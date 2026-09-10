# MiniShell Design Principles

## Purpose

MiniShell is a platform-adaptive application runtime. Its purpose is to keep application cores independent of Linux, NuttX, ESP-IDF, board drivers, and mocks while providing coherent services and an application lifecycle.

Linux Mint on `pc-1` is the reference implementation and a full production target. Portability shapes the interfaces; it does not require every platform implementation to be identical.

## 1. Top-down design

Start from required application behavior, then define responsibilities downward:

```text
application behavior
    |
MiniShell public API
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

### Code-facing naming

Use lowercase/snake_case for code-facing project and application names:

```text
minishell
ft8
apps/ft8/
/flash/ft8/station.txt
```

Use **MiniShell** and **MiniFT8** as normal project/product names in prose. Standard C conventions take precedence where appropriate, so macros remain uppercase (`MINISHELL_*`, `MINIFT8_*`). Do not reintroduce mixed-case filesystem paths, executable names, targets, or runtime application names.

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

- **Public API:** application-facing source/application contract.
- **Private backend boundary:** MiniShell-internal and free to evolve as implementations are cleaned up.

Backends may freely use POSIX, NuttX, ESP-IDF, or simulation code below the private boundary. Applications may not.

The public API is also still under active architectural development. Backward source and binary compatibility are not currently promised. A formal binary ABI may be introduced later if independently built applications need cross-release compatibility.

## 5. Small API, compatibility frozen later

Add public surface only for a demonstrated application need.

Useful design tools include:

```text
struct_size
capability bits
optional sub-APIs
fixed-width public types
explicit ownership/lifetime
clear result values
```

These improve clarity and feature discovery, but they do **not** currently require append-only growth or stable field offsets. Breaking API changes are allowed when they improve the architecture; in-tree applications are rebuilt against the matching API.

Do not expand the API merely to imitate POSIX, Linux, or an SDK.

Recent examples of justified growth:

```text
MiniFT8/file management -> dir_open/read/close
storage/resource needs  -> filesystem space(path)
nano cursor             -> optional inverse text attribute
```

Compatibility should be frozen deliberately when there is a real distribution/use case for it, not accidentally during early design.

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
Linux/Mint          .so + dlopen/dlsym/dlclose
Cardputer ADV V1    compiled-in registry
Cardputer ADV next  runtime /sd/<app>.elf
Tab5/NuttX          loadable-app mechanism where practical
```

The ADV static registry remains a valid baseline and transition mechanism, but runtime ELF loading is now an **active architecture target**. The first field-usable external application is planned as:

```text
/sd/keyer.elf
```

Application source does not contain loader-specific logic. `keyer.elf` must depend only on the MiniShell public API and Keyer-owned modules; it must not know about ESP-IDF, FreeRTOS, M5/Cardputer, SD/FATFS implementation details, or the ELF loader itself.

The initial ADV external-app naming convention is `/sd/<app>.elf`. Discovery, relocation, symbol resolution, execution setup, unloading, and any compiled-in-versus-external precedence policy remain private runtime/backend concerns and may evolve during implementation.

Starting ELF work does **not** freeze a long-term binary ABI. Until compatibility is deliberately frozen, an external application may need to be rebuilt against the matching MiniShell API generation.

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

`../project/consistency-check.md` records architecture audit/history.

## 12. Synchronous and understandable first

Prefer synchronous APIs and explicit lifecycle until a real requirement justifies concurrency or asynchronous contracts.

Do not add ISR safety, general reentrancy, worker tasks, queues, callbacks, or async state machines speculatively.

## 13. Tests at the boundary that matters

For each service:

1. test service semantics independently;
2. test the public API through an application/probe;
3. test real platform behavior where platform-specific behavior matters.

Packaging may differ while the observable API behavior stays the same:

```text
Linux                runtime-loaded .so
ADV V1 baseline      statically composed probe/app
ADV ELF milestone    runtime-loaded /sd/<app>.elf
```

The ELF loader itself also requires focused tests for discovery, load/start/return/unload/cleanup and failure handling. Those loader tests are additional to, not a substitute for, service/API tests.

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
