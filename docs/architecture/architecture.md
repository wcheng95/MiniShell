# MiniShell Architecture

## 1. Purpose

MiniShell is a platform-adaptive application runtime. Its invariant is:

> Application cores depend on MiniShell, never directly on the host OS, RTOS, SDK, board support package, or test/mock implementation.

Linux Mint on `pc-1` is the reference behavior and a full production target.

## 2. System view

```text
+------------------------------------------------------+
|                   Applications                       |
|          ft8 / future keyer/ft4/rtty/js8 / tools    |
+---------------------- MiniShell API -----------------+
|                  Portable MiniShell                  |
| shell / app lifecycle / service semantics / policy   |
+---------------- private backend boundary ------------+
| Linux/POSIX | NuttX | ESP-IDF/HW | mocks/simulation |
+------------------------------------------------------+
```

MiniShell may be thin or thick depending on the target. The application-facing API remains the boundary.

## 3. Responsibilities

MiniShell owns two major domains:

1. **Platform adaptation** — memory, storage, time/location, display, input, audio, and later Digital I/O/control/networking only when application requirements justify them.
2. **Application runtime** — discovery, foreground lifecycle, cleanup, and where practical runtime load/unload without rebuilding MiniShell.

MiniShell also owns application-visible resource policy.

## 4. Dependency direction

```text
application
    |
    v
include/minishell/api.h
    |
    v
portable MiniShell services/runtime
    |
    v
private backend hooks
    |
    v
platform implementation
```

No public handle is a POSIX, NuttX, ESP-IDF, FreeRTOS, or board-driver object.

The public API is still under architectural development. Backward source/binary compatibility is not yet promised; external applications may need rebuilding for a matching MiniShell API generation. A formal ABI may be introduced later when cross-release compatibility becomes a real requirement.

## 5. Current composition

Reference Linux composition:

```text
platform/linux/main.c            platform entry
core/minishell_runtime.c         portable runtime lifecycle
core/shell.c                     resident shell/control plane
core/app_manager.c               foreground app lifecycle
core/minishell_services/*        portable service semantics
platform/linux/*                 private Linux providers/backend
include/minishell/api.h          public application API
apps/*                           portable/domain applications
```

The portable runtime entry is:

```c
int minishell_run(void);
```

Linux calls it from ordinary `main()`. Cardputer ADV calls the same runtime from ESP-IDF `app_main()` below the platform boundary.

## 6. Ownership

Application-visible resource ownership is explicit:

```text
App lifecycle       app_manager
API table/policy    service composition
Memory allocations  Memory service
Files/dirs/quota    Filesystem service
UTC/location        Time/Location service
Text display        Display service
Logical key queue   Input service
Audio streams       Audio service
App packaging/load  private platform/runtime mechanism
```

A backend provides primitives; it does not redefine application semantics.

The resident shell console is a separate private boundary. Applications use public System/Console/Display/Input APIs according to intent rather than the private shell console.

## 7. Application lifecycle and runtime packaging

MiniShell keeps one foreground application active at a time:

```text
shell
  -> resolve application
  -> app_begin
  -> backend load/prepare or select compiled-in app
  -> application entry
  -> app uses MiniShell API
  -> app returns
  -> backend unload/release where applicable
  -> app_end / reclaim MiniShell-managed resources
  -> shell
```

Packaging is backend-specific:

```text
Linux/Mint          .so + dlopen()/dlsym()/dlclose()
Cardputer ADV V1    compiled-in registry
Cardputer ADV next  runtime external .elf
Tab5/NuttX          native loadable mechanism where practical
```

ADV application resolution is fixed:

```text
1. compiled-in application
2. /flash/<app>.elf
3. /sd/<app>.elf
```

The same external ELF must run unchanged from `/flash` or `/sd`. If both external copies exist, `/flash` wins.

For Keyer:

```text
/flash/keyer.elf
/sd/keyer.elf
```

are both valid installations.

Loader parsing, relocation, symbol resolution, execution-task setup, cleanup, and unloading remain resident/private. The application source does not know its container or installation location.

## 8. Current public services

Current public API:

```text
App
System
Console
Memory
Filesystem
Time/Location
Display
Input
Audio
```

Digital I/O and Control are not yet public services. Keyer is expected to provide the first concrete Digital I/O requirement.

Public contracts live under `docs/api/`.

## 9. Resource policy

Linux intentionally exposes a bounded MiniShell resource domain rather than raw host resources.

Default Linux policy:

```text
Memory budget    8 MiB
Storage budget  64 MiB
```

`free` and `df` report MiniShell-visible resources.

## 10. Filesystem and configuration model

Applications use one logical namespace:

```text
/
|-- flash
`-- sd
```

Typical paths:

```text
/flash/config.txt
/flash/keyer/setting.txt
/flash/ft8/station.txt      # current MiniFT8 implementation
/sd/keyer.elf
```

MiniShell Filesystem owns path/handle/storage semantics. Applications may own the meaning and policy of files they create.

Canonical configuration ownership is defined in `configuration.md`:

```text
/flash/config.txt
    MiniShell-owned resident/platform configuration

/flash/<app>/setting.txt
    application-owned configuration and deployment settings
```

Hardware-specific application settings are valid. For example, Keyer may own numeric GPIO assignments and ask MiniShell Digital I/O to configure/read/write those generic lines. MiniShell must not know that a line means `dit`, `dah`, paddle, or KeyOut.

This does not imply a generic public MiniShell Config service. MiniShell may parse its own system configuration internally; applications may use Filesystem and application-local configuration modules.

MiniFT8 currently uses `/flash/ft8/station.txt`. The canonical eventual application-settings path is `/flash/ft8/setting.txt`, but that migration is a separate MiniFT8 task and is not part of C4.

## 11. Time model

Time/Location owns monotonic and UTC semantics.

Linux anchors MiniShell UTC from host UTC and advances it from monotonic time. Manual MiniShell date correction does not modify host system time. Backends with RTC/GPS may implement equivalent policy below MiniShell.

Monotonic time is never changed by UTC correction.

## 12. Display and input

Display and Input are separate public services.

Linux maps logical text display operations to terminal behavior and normalizes terminal input bytes into MiniShell input events. Applications never embed terminal escape sequences.

MiniFT8 uses its own UI model/frame types above MiniShell; Cardputer ADV and Linux render the same application semantics through different backends/presentations.

## 13. Resident shell versus applications

Current resident shell:

```text
help
status
apps
run <app>
exit
```

Portable/domain applications include:

```text
ft8
hello cat cp date df free ls mkdir mv nano rm rmdir
```

Keyer is the next planned domain application and the first field-usable ADV runtime ELF.

Platform-dependent bootstrap/system commands may exist where useful without requiring fake parity across every backend.

## 14. Mocks and simulation

Mocks stay below MiniShell:

```text
application core
    |
MiniShell API
    +-- Linux provider
    +-- embedded provider
    +-- file/simulated provider
```

Mocks emulate resource providers, not application-domain outcomes.

## 15. Testing model

Linux CI is the reference regression gate. Service tests validate semantics independently; application tests validate public behavior; embedded builds/hardware validate backend-specific behavior.

Runtime ELF adds focused tests for:

```text
compiled-in > /flash > /sd resolution
discovery/load
MiniShell API call
return/unload
resource cleanup
invalid/missing ELF failure paths
```

Use a non-colliding app such as `elfhello` when proving the external loader so a compiled-in application cannot mask loader execution.

The architecture also uses platform-boundary and application dependency/no-side-talk checks.

## 16. Small-module rule

One owner does not mean one giant file. An ownership domain may use several private helper modules while exposing one coherent responsibility.

Split modules when responsibilities genuinely diverge; do not split only to meet a line-count target.

## 17. Cross-platform baseline and next milestone

The Linux/ADV MiniFT8 baseline is established:

```text
Linux backend + DESKTOP presentation   PASS
Linux backend + ADV presentation       PASS
ADV backend   + ADV presentation       PASS
MiniFT8 RX-7 production path           PASS on Linux
ADV firmware build                     PASS
```

The pre-Keyer architecture cleanup is complete:

```text
C0 dependency/no-side-talk enforcement   COMPLETE
C1 opaque AppController                  COMPLETE
C2 lifecycle-only ft8_main               COMPLETE
C3 runtime ELF direction/resolution      COMPLETE
C4 configuration ownership/namespace     COMPLETE
```

The next work is Keyer plan review/finalization followed by runtime ELF and Digital I/O implementation.

## 18. Reference-development rule

New portable behavior is normally developed on Linux first unless inherently target-specific. Embedded constraints remain visible throughout:

```text
bounded resources
explicit ownership
deterministic lifecycle
small interfaces
controlled allocation
no host-specific types in application code
```

See:

```text
design-principles.md
configuration.md
resident-vs-app.md
../project/architecture-cleanup.md
../keyer/README.md
```
