# MiniShell Architecture

## 1. Purpose

MiniShell is a platform-adaptive application runtime. Its invariant is:

> Application cores depend on MiniShell, never directly on the host OS, RTOS, SDK, board support package, or test/mock implementation.

Linux Mint on `pc-1` is the reference behavior and a full production target.

## 2. System view

```text
+------------------------------------------------------+
|                   Applications                       |
|          ft8 / future ft4/cw/rtty/js8 / tools       |
+---------------------- MiniShell API -----------------+
|                  Portable MiniShell                  |
| shell / app lifecycle / service semantics / policy   |
+---------------- private backend boundary ------------+
| Linux/POSIX | NuttX | ESP-IDF/HW | mocks/simulation |
+------------------------------------------------------+
```

MiniShell may be thin or thick depending on the target. The application-facing API remains the boundary.

## 3. Responsibilities

MiniShell has two primary responsibilities:

1. **Platform adaptation** — logical services such as memory, storage, time/location, display, input, and audio, with later services such as control or networking added only when justified by application requirements.
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

The private backend boundary may evolve freely. The public API is also still under active architectural development: backward source and binary compatibility are **not yet promised**. In-tree applications are rebuilt when the API changes. A formal binary ABI may be introduced later if independently built `.so` or `.elf` applications need compatibility across MiniShell releases.

## 5. Current composition

The active Linux build is composed from:

```text
platform/linux/main.c            Linux C entry point only
core/minishell_runtime.c         portable MiniShell lifecycle/startup
core/shell.c                     resident shell parsing/control plane
core/app_manager.c               foreground app lifecycle
core/minishell_services/*        portable service semantics
platform/linux/linux_console.c   resident stdin/stdout console provider
platform/linux/*                 Linux private backend/providers
include/minishell/api.h          public application API
apps/*                           portable/domain applications
```

The portable runtime entry is:

```c
int minishell_run(void);
```

A platform-specific executable entry point calls that function. Linux uses ordinary C `main()`. A future ESP-IDF backend can call the same portable runtime from `app_main()` without introducing ESP-IDF concepts into the portable core.

A Cardputer ADV backend will be added under `platform/adv/`. The earlier ESP-IDF/Tab5 implementation path is preserved in branch `archive/tab5-legacy` as historical reference, not as the new ADV architecture.

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
Audio streams       Audio service
App packaging/load  private platform mechanism behind app_manager
```

Resident-shell console ownership is separate and private:

```text
portable shell
    -> minishell_platform_console_write/read_line
    -> platform-private console provider

Linux
    -> stdin/stdout

ADV later
    -> Cardputer display/keyboard shell interaction
```

This console interface is **not** a public application service. Applications continue to use MiniShell Display, Input, System, and other public APIs.

A backend/provider supplies primitives; it does not redefine application semantics.

On Linux the OS physically owns files, memory, terminal devices, etc. MiniShell remains the single application-facing gateway. On a thick embedded target MiniShell may also directly own the hardware driver.

## 7. Application lifecycle

V1 keeps one foreground application active at a time:

```text
shell
  -> resolve application
  -> app_begin
  -> backend load/prepare or select compiled-in app
  -> main(argc, argv)
  -> app uses MiniShell API
  -> app returns
  -> backend unload/release where applicable
  -> app_end / reclaim MiniShell-managed resources
  -> shell
```

Packaging/loading is private:

```text
Linux/Mint      .so + dlopen()/dlsym()/dlclose()
Cardputer ADV   V1 compiled-in registry
Tab5/NuttX      loadable-app mechanism where practical
future ADV      runtime .elf loading may be explored later
```

ADV V1 defers runtime ELF loading because it is not needed for the first backend and adds loader/linker/flash-mapping complexity. It is not rejected on the assumption that all executable text must live in RAM.

User-visible behavior remains `apps`, `run <app>`, direct `<app>`, return to `M$>` where practical. Runtime application names are lowercase.

## 8. Current public services

The current public API exposes:

```text
System
Memory
Filesystem
Time/Location
Display
Input
Audio
```

`MINISHELL_API_VERSION` identifies the API generation expected by the current source/build. `struct_size`, capabilities, validity bits, fixed-width types, and opaque handles remain useful design mechanisms, but they are **not a backward-compatibility promise** during this phase.

Notable application-driven behavior includes:

```text
Filesystem     dir_open / dir_read / dir_close
Filesystem     space(path)
Filesystem     rename replaces an existing regular-file destination
Display text   optional write_at_attr(..., MINI_TEXT_ATTR_INVERSE)
Audio          format-described independent RX/TX streams
```

The Audio API transports ordered frames and does not assign application meaning such as stereo versus I/Q to channels. The current Linux WAV RX provider validates and streams exact-format PCM through the private provider boundary.

API changes remain justified by real application requirements, but incompatible changes are allowed when they improve clarity, ownership, or portability. Control is not yet a public MiniShell service.

Canonical public contracts live under `docs/api/`.

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
/flash/ft8/station.txt
```

Linux maps the namespace underneath a private host directory, by default `~/.local/share/minishell/fs`. Applications never see the host path.

Filesystem service owns normalization, logical file/directory handles, lifecycle cleanup, namespace semantics, replacement rename semantics, and quota behavior. The backend supplies native filesystem primitives.

A domain application may own file *policy* without owning the filesystem. For example, MiniFT8's `storage_service` owns its `station.txt` naming and temporary-file save sequence, while all file handles and namespace semantics remain owned by MiniShell Filesystem.

## 11. Time model

Time/Location owns monotonic and UTC semantics.

Linux startup reads host UTC and anchors MiniShell UTC to monotonic time. `date` may re-anchor MiniShell UTC for the current session without changing Linux system time or persisting an offset. A backend with a writable RTC may persist an equivalent UTC correction.

Monotonic time is never changed by UTC correction.

Configured default location is MiniShell-owned persistent state; live location may later come from GPS or another provider.

## 12. Display and input

Display and Input remain separate logical services.

Linux maps text Display calls to terminal output/ANSI behavior and normalizes terminal bytes into logical Input events. Applications never embed terminal escape sequences.

Examples:

```text
nano cursor
    -> MINI_TEXT_ATTR_INVERSE
    -> Linux reverse video below MiniShell

MiniFT8 UiFrame / UiInput
    -> MiniShell Display / Input
    -> Linux terminal
    -> ADV Cardputer display/keyboard
```

The Linux terminal backend owns ANSI/CSI and UTF-8 byte-stream reconstruction, including split-read state and the standalone-Escape ambiguity policy. Those details remain below the Input API.

The resident shell's line-oriented console is a different private boundary. On Linux it deliberately disables stdio input buffering so shell reads do not consume bytes intended for a foreground application's raw Input service.

MiniFT8's application UI therefore has no ncurses/Linux dependency.

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

Future protocol applications such as `ft4`, `cw`, `rtty`, and `js8` are separate applications rather than protocol modes inside `ft8`.

`put/get`, `suspend`, `poweroff`, and similar operations are platform-dependent. They may exist on a target where useful and be absent elsewhere; no fake implementation is required.

## 14. Mocks and simulation

Mocks stay underneath MiniShell:

```text
MiniFT8 core
    |
MiniShell API
    +-- Linux provider
    +-- file-audio provider
    +-- future QMX/live-radio provider
    +-- simulated providers
```

Mocks emulate service providers, not application-domain outcomes. Do not bypass the MiniFT8 decoder/scheduler with fake decoded QSOs when testing those modules.

## 15. Testing model

The Linux CTest suite currently has eleven tests covering:

```text
shell/app loading
portable service semantics and lifecycle
terminal Input through a PTY
portable utility applications + replacement mv
nano PTY edit/save/exit + inverse cursor
directory iteration + ls
resource quota + free/df/date + rename accounting
Audio/WAV RX transport
ft8 pure UI state/action behavior
stateful Linux terminal parser split-boundary behavior
ft8 runtime launch/navigation/persistence/relaunch/exit
```

CI also runs the platform-neutral MiniShell service/unit suite, including Audio API/service tests.

The A0 console/startup refactor passed this entire suite unchanged, which is the regression evidence that the new private boundary preserves Linux behavior.

Service/unit tests remain more important than merely proving that one native app can load.

## 16. Module-size and internal-debt policy

The architecture does not equate "one owner" with "one giant file." An owner may be implemented by several private helper modules while presenting one semantic service.

The H1-H5 architecture-audit debt is resolved. In particular:

```text
Linux backend      split by responsibility
portable core      no POSIX loader-result leakage
Filesystem owner   private path/handle/quota helpers split out
terminal input     stateful ANSI/CSI/UTF-8 parser isolated below Input
resident console   stdin/stdout isolated below private backend boundary
startup            platform entry point separated from portable runtime
```

Future debt should be recorded when discovered rather than allowed to blur ownership boundaries. See `../project/consistency-check.md` for the audit record.

## 17. Current cross-platform validation milestone

MiniFT8 RX-1B is paused while MiniShell is exercised across a second real backend and MiniFT8 across a second profile.

Required matrix:

```text
Linux backend + DESKTOP profile
Linux backend + ADV profile
ADV backend   + ADV profile
```

The key comparison is:

```text
Linux backend + ADV profile
            versus
ADV backend + ADV profile
```

The MiniFT8 core/profile stay the same; only the MiniShell backend changes. This tests whether platform details truly remain below the public API.

Canonical plan: `../project/adv-backend-plan.md`.

## 18. Reference-development rule

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

Stage A0 is complete. The immediate next work is **A1**: create `platform/adv/`, add the ESP-IDF/Cardputer ADV build skeleton, implement the private shell console on Cardputer display/keyboard, and prove static app list/run/return with a tiny app before bringing `ft8` across.
