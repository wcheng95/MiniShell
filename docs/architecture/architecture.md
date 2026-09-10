# MiniShell Architecture

## 1. Purpose

MiniShell is a platform-adaptive application runtime. Its invariant is:

> Application cores depend on MiniShell, never directly on the host OS, RTOS, SDK, board support package, or test/mock implementation.

The maintained targets are Linux Mint on `pc-1` and Cardputer ADV. Linux is the reference behavior and full production target.

## 2. System view

```text
+------------------------------------------------------+
|                   Applications                       |
|          ft8 / future keyer/ft4/rtty/js8 / tools    |
+---------------------- MiniShell API -----------------+
|                  Portable MiniShell                  |
| shell / app lifecycle / service semantics / policy   |
+---------------- private backend boundary ------------+
|      Linux/POSIX      |      ADV/ESP-IDF/HW          |
|                    mocks/simulation                  |
+------------------------------------------------------+
```

MiniShell may be thin or thick depending on the target. The application-facing API remains the boundary.

## 3. Responsibilities

MiniShell has two primary responsibilities:

1. **Platform adaptation** — logical services such as memory, storage, time/location, display, input, and audio, with later services such as Digital I/O, control, or networking added only when justified by application requirements.
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
Linux/POSIX or ADV/ESP-IDF/hardware
```

No application-visible handle is a POSIX descriptor, `DIR *`, ESP-IDF object, or board-driver object.

The private backend boundary may evolve freely. The public API is also still under active architectural development: backward source and binary compatibility are **not yet promised**. In-tree applications are rebuilt when the API changes. External applications may likewise need to be rebuilt for the matching MiniShell API generation. A formal binary ABI may be introduced later if cross-release compatibility becomes a real distribution requirement.

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

A platform-specific entry point calls that function. Linux uses ordinary C `main()`. Cardputer ADV uses ESP-IDF `app_main()` below the same portable runtime boundary.

The active ADV backend lives under:

```text
platform/adv/
```

It provides the real embedded backend with Cardputer display/keyboard, memory, filesystem, time/location, and application-composition/runtime support.

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

ADV
    -> Cardputer display/keyboard shell interaction
       plus backend-owned diagnostic/USB paths where available
```

This console interface is **not** a public application service. Applications continue to use MiniShell Display, Input, System, Console, and other public APIs according to intent.

A backend/provider supplies primitives; it does not redefine application semantics.

On Linux the OS physically owns files, memory, terminal devices, etc. MiniShell remains the single application-facing gateway. On ADV MiniShell also directly owns platform hardware through the backend.

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

Packaging/loading is private:

```text
Linux/Mint          .so + dlopen()/dlsym()/dlclose()
Cardputer ADV V1    compiled-in registry
Cardputer ADV next  runtime external .elf
```

ADV V1 intentionally used static composition to prove the backend and application boundary first. That historical decision remains valid, but runtime ELF is now an **active architecture milestone**.

The established ADV application resolution order is:

```text
1. compiled-in application
2. /flash/<app>.elf
3. /sd/<app>.elf
```

For external applications, `/flash` is searched before `/sd`. The same external ELF may be installed in either location. If both external copies exist, the `/flash` copy wins. For Keyer, both are valid:

```text
/flash/keyer.elf
/sd/keyer.elf
```

`/sd/keyer.elf` is convenient during development and for removable distribution. Copying the exact same file to `/flash/keyer.elf` must make it runnable from flash without changing the application binary.

The loader is a resident/private MiniShell mechanism. Path discovery, ELF parsing, relocation, symbol resolution, execution-task setup, cleanup, and unloading must not leak into application source. The resolution order above is a MiniShell runtime rule, not application logic.

User-visible behavior remains `apps`, `run <app>`, direct `<app>`, and return to `M$>` where practical. Runtime application names are lowercase. A user should eventually be able to add a supported application file without rebuilding MiniShell.

## 8. Current public services

The current public API exposes:

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

API changes remain justified by real application requirements, but incompatible changes are allowed when they improve clarity, ownership, or portability. Digital I/O and Control are not yet public MiniShell services; Keyer is expected to provide the first concrete requirement for Digital I/O.

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
/flash/config.txt
/flash/ft8/station.txt
```

Linux maps the namespace underneath a private host directory, by default `~/.local/share/minishell/fs`. Applications never see the host path.

Filesystem service owns normalization, logical file/directory handles, lifecycle cleanup, namespace semantics, replacement rename semantics, and quota behavior. The backend supplies native filesystem primitives.

A domain application may own file *policy* without owning the filesystem. For example, MiniFT8's `storage_service` owns its application file naming and temporary-file save sequence, while all file handles and namespace semantics remain owned by MiniShell Filesystem.

Configuration ownership is canonicalized in `configuration.md`: `/flash/config.txt` is reserved for MiniShell-owned resident/platform configuration, while `/flash/<app>/setting.txt` is reserved for application-owned settings and deployment configuration. Existing application filenames such as MiniFT8 `station.txt` may migrate separately and are not automatically renamed by that architecture rule.

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

MiniFT8's application UI therefore has no terminal-platform dependency.

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

Keyer is the next planned domain application and the first planned field-usable ADV runtime ELF. Future protocol applications such as `ft4`, `rtty`, and `js8` are separate applications rather than protocol modes inside `ft8`.

`put/get`, `suspend`, `poweroff`, and similar operations are platform-dependent. They may exist on a target where useful and be absent elsewhere; no fake implementation is required.

## 14. Mocks and simulation

Mocks stay underneath MiniShell:

```text
MiniFT8 core
    |
MiniShell API
    +-- Linux provider
    +-- ADV provider
    +-- file-audio provider
    `-- simulated providers
```

Mocks emulate service providers, not application-domain outcomes. Do not bypass the MiniFT8 decoder/scheduler with fake decoded QSOs when testing those modules.

## 15. Testing model

Linux CTest/CI covers shell/app loading, portable service semantics and lifecycle, terminal Input, utility applications, filesystem behavior, resource policy, Audio/WAV transport, MiniFT8 UI/runtime behavior, and focused FT8 unit/reference tests.

The architecture also uses dedicated dependency checks so application source cannot silently acquire platform dependencies or forbidden sibling-module coupling.

Runtime ELF adds another boundary that must be tested independently:

```text
compiled-in > /flash > /sd resolution order
load
entry/start
MiniShell API use
return
unload/release
MiniShell-managed resource cleanup
invalid/missing ELF failure paths
```

Tests must prove that the same ELF works from either external location, that `/flash/<app>.elf` wins when both external copies exist, and that a compiled-in application wins over a same-name external copy. A non-colliding external test app should be used to prove the loader path itself.

Those loader tests do not replace service/API tests. Service/unit tests remain more important than merely proving that one executable file can load.

## 16. Module-size and internal-debt policy

The architecture does not equate "one owner" with "one giant file." An owner may be implemented by several private helper modules while presenting one semantic service.

Resolved housekeeping includes:

```text
Linux backend      split by responsibility
portable core      no POSIX loader-result leakage
Filesystem owner   private path/handle/quota helpers split out
terminal input     stateful ANSI/CSI/UTF-8 parser isolated below Input
resident console   stdin/stdout isolated below private backend boundary
startup            platform entry point separated from portable runtime
```

Future debt should be recorded when discovered rather than allowed to blur ownership boundaries. See `../project/consistency-check.md` for the audit record.

## 17. Cross-platform baseline and current milestone

The Linux/ADV cross-backend presentation checkpoint is complete:

```text
Linux backend + DESKTOP presentation   PASS
Linux backend + ADV presentation       PASS
ADV backend   + ADV presentation       PASS
```

MiniFT8 RX integration has advanced through RX-7 on Linux while preserving the platform boundary, and Cardputer ADV continues to pass the firmware build gate.

The next runtime milestone is:

```text
ADV application resolution
    1. compiled-in
    2. /flash/<app>.elf
    3. /sd/<app>.elf

first field external app:
    keyer.elf
```

The purpose is not merely to prove ELF parsing. A field-usable Keyer should exercise the external-app lifecycle plus real MiniShell services strongly enough to validate the runtime architecture in practical use.

## 18. Reference-development rule

New portable behavior is normally developed on Linux first unless inherently ADV-specific. Linux establishes golden observable behavior for the ADV port.

Embedded constraints remain design inputs even on Mint:

```text
bounded resources
explicit ownership
deterministic lifecycle
small interfaces
controlled allocation
no host-specific types in application code
```

The architecture cleanup C0-C4 is complete. Current development is intentionally focused on Linux and Cardputer ADV.
