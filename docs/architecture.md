# MiniShell Architecture

## 1. Purpose

MiniShell is a platform-adaptive application runtime. Its invariant is:

> Application cores depend on MiniShell, never directly on the host OS, RTOS, SDK, board support package, or test/mock implementation.

Linux Mint on `pc-1` is the reference behavior and a full production target.

## 2. System view

```text
+------------------------------------------------------+
|                   Applications                       |
|          MiniFT8 / MiniCW / MiniRTTY / tools         |
+---------------------- MiniShell ABI -----------------+
|                  Portable MiniShell                  |
| shell / app lifecycle / service semantics / policy   |
+---------------- private backend boundary ------------+
| Linux/POSIX | NuttX | ESP-IDF/HW | mocks/simulation |
+------------------------------------------------------+
```

MiniShell may be thin or thick depending on the target. The application-facing contract remains the boundary.

## 3. Responsibilities

MiniShell has two primary responsibilities:

1. **Platform adaptation** — stable logical services such as memory, storage, time/location, display, input, and future audio/radio/network services.
2. **Application runtime** — discovery, foreground lifecycle, cleanup, and where practical load/unload without rebuilding MiniShell.

A third cross-cutting responsibility is **resource policy**: MiniShell defines the resource domain applications may consume and enforces it through the owning services.

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

No application-visible handle is a POSIX descriptor, `DIR *`, NuttX object, ESP-IDF object, or board-driver object.

The private backend boundary is allowed to evolve as implementation structure improves; the public ABI is intentionally much more stable.

## 5. Current composition

The active Linux root build is composed from:

```text
core/main.c                    composition/startup
core/shell.c                   resident shell control plane
core/app_manager.c             foreground app lifecycle
core/minishell_services/*      portable service semantics
platform/linux/*               Linux private backend
include/minishell/api.h        public app ABI
apps/*                         portable runtime applications
```

Earlier ESP-IDF/Tab5 source trees still exist in the repository but are not part of the current root Linux build. They are historical/pre-pivot code until deliberately reused or removed.

## 6. Ownership

Application-visible ownership is explicit:

```text
App lifecycle       app_manager
API table/policy    service composition
Memory allocations  Memory service
Files/dirs/quota    Filesystem service
UTC/location        Time/Location service
Text display        Display service
Logical key queue   Input service
Native app loading  private platform loader behind app_manager
```

A backend supplies primitives; it does not redefine application semantics.

On Linux the OS physically owns files, memory, terminal devices, etc. MiniShell remains the single application-facing gateway. On a thick embedded target MiniShell may also directly own the hardware driver.

## 7. Application lifecycle

V1 keeps one foreground application active at a time:

```text
shell
  -> resolve application
  -> app_begin
  -> backend load/prepare
  -> main(argc, argv)
  -> app uses MiniShell ABI
  -> app returns
  -> backend unload/release
  -> app_end / reclaim MiniShell-managed resources
  -> shell
```

The loader/container is private:

```text
Linux/Mint      .so + dlopen()/dlsym()/dlclose()
Tab5/NuttX      loadable-app mechanism where practical
Cardputer ADV   compiled-in registry acceptable when loading costs too much RAM
```

User-visible behavior remains `apps`, `run <app>`, direct `<app>`, return to `M$>`.

## 8. Current public services

ABI generation 1 currently exposes:

```text
System
Memory
Filesystem
Time/Location
Display
Input
```

Current notable extensions include:

```text
Filesystem     dir_open / dir_read / dir_close
Filesystem     space(path)
Display text   optional write_at_attr(..., MINI_TEXT_ATTR_INVERSE)
```

ABI growth remains append-only where compatible.

## 9. Resource policy

Linux intentionally does not expose the PC's unconstrained resources as MiniShell resources.

Default Linux policy:

```text
Memory budget    8 MiB
Storage budget  64 MiB
```

The limits are configurable and enforced. `free` and `df` report this MiniShell-visible domain.

This lets Linux remain a powerful development host while applications are exercised under embedded-like constraints.

## 10. Filesystem model

Applications use one logical namespace:

```text
/
|-- sd
`-- flash
```

Typical paths:

```text
/sd/log.txt
/flash/config.ini
```

Linux maps the namespace underneath a private host directory, by default `~/.local/share/minishell/fs`. Applications never see the host path.

Filesystem service owns normalization, logical file/directory handles, lifecycle cleanup, namespace semantics, and quota behavior. The backend supplies POSIX primitives.

## 11. Time model

Time/Location owns monotonic and UTC semantics.

Linux startup reads host UTC and anchors MiniShell UTC to monotonic time. `date` may re-anchor MiniShell UTC for the current session without changing Linux system time or persisting an offset. A backend with a writable RTC may persist an equivalent UTC correction.

Monotonic time is never changed by UTC correction.

Configured default location is MiniShell-owned persistent state; live location may later come from GPS or another provider.

## 12. Display and input

Display and Input remain separate logical services.

Linux maps text Display calls to terminal output/ANSI behavior and normalizes terminal bytes into logical Input events. Applications such as `nano` never embed terminal escape sequences.

The optional inverse text attribute demonstrates the intended direction:

```text
nano cursor requirement
    -> MINI_TEXT_ATTR_INVERSE
    -> Linux reverse-video ANSI below MiniShell
    -> future framebuffer backend renders inverse cells natively
```

## 13. Resident shell versus applications

Current Linux resident shell:

```text
help
status
apps
run <app>
exit
```

Ordinary utilities are portable apps:

```text
hello cat cp date df free ls mkdir mv nano rm rmdir
```

`put/get`, `suspend`, `poweroff`, and similar operations are platform-dependent. They may exist on a target where useful and be absent elsewhere; no fake implementation is required.

## 14. Mocks and simulation

Mocks stay underneath MiniShell:

```text
MiniFT8 core
    |
MiniShell ABI
    +-- Linux/QMX provider
    +-- file-audio provider
    +-- simulated-radio provider
```

Mocks emulate service providers, not application-domain outcomes.

## 15. Testing model

Linux CI currently runs seven integration/service tests covering:

```text
shell/app loading
portable service semantics and lifecycle
terminal input handoff
portable utility applications
nano PTY edit/save/exit + inverse cursor
directory iteration + ls
resource quota + free/df/date
```

Service unit/integration tests remain more important than merely proving that one native app can load.

## 16. Module-size and internal-debt policy

The architecture does not equate "one owner" with "one giant file." An owner may be implemented by several private helper modules while presenting one semantic service.

The current audit identifies internal cleanup needs in `platform/linux/linux_backend.c`, `core/minishell_services/filesystem_service.c`, and the shell/private-loader boundary. These do not require a public ABI redesign.

See `docs/consistency-check.md` for the current audit and prioritized housekeeping list.

## 17. Reference-development rule

New portable behavior is normally developed on Linux first unless inherently target-specific. Linux establishes golden observable behavior for later ports.

Embedded constraints remain design inputs even on Mint:

```text
bounded resources
explicit ownership
deterministic lifecycle
small interfaces
controlled allocation
no host-specific types in application code
```

New service groups are added only when a real application requirement justifies them. MiniFT8-V3 is expected to drive the next major service decisions.
