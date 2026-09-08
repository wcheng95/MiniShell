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

1. **Platform adaptation** — stable logical services such as memory, storage, time/location, display, input, and audio, with later services such as radio control or networking added only when justified by application requirements.
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

The private backend boundary may evolve freely; the public ABI is intentionally much more stable.

## 5. Current composition

The active Linux build is composed from:

```text
core/main.c                    composition/startup
core/shell.c                   resident shell control plane
core/app_manager.c             foreground app lifecycle
core/minishell_services/*      portable service semantics
platform/linux/*               Linux private backend/providers
include/minishell/api.h        public app ABI
apps/*                         portable/domain runtime applications
```

The earlier ESP-IDF/Tab5 implementation path has been removed from active `main`. Its complete pre-cleanup state is preserved in branch `archive/tab5-legacy`.

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
Native app loading  private platform loader behind app_manager
```

A backend/provider supplies primitives; it does not redefine application semantics.

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

MiniFT8 now exercises this lifecycle as the first substantial domain application:

```text
M$> MiniFT8
... application ...
q
M$>
```

## 8. Current public services

ABI generation 1 currently exposes:

```text
System
Memory
Filesystem
Time/Location
Display
Input
Audio
```

Notable application-driven behavior/extensions include:

```text
Filesystem     dir_open / dir_read / dir_close
Filesystem     space(path)
Filesystem     rename replaces an existing regular-file destination
Display text   optional write_at_attr(..., MINI_TEXT_ATTR_INVERSE)
Audio          format-described independent RX/TX streams
```

The Audio ABI transports ordered frames and does not assign application meaning such as stereo versus I/Q to channels. The current Linux WAV RX provider validates and streams exact-format PCM through the private provider boundary.

ABI growth remains append-only where compatible. Semantic clarification/growth must still be justified by a real application requirement. Control/Radio is not yet a public MiniShell service.

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
/flash/MiniFT8/Station.txt
```

Linux maps the namespace underneath a private host directory, by default `~/.local/share/minishell/fs`. Applications never see the host path.

Filesystem service owns normalization, logical file/directory handles, lifecycle cleanup, namespace semantics, replacement rename semantics, and quota behavior. The backend supplies native filesystem primitives.

A domain application may own file *policy* without owning the filesystem. For example, MiniFT8's `storage_service` owns its Station.txt naming and temporary-file save sequence, while all file handles and namespace semantics remain owned by MiniShell Filesystem.

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
    -> Linux terminal today
    -> future framebuffer/touch backend later
```

The Linux terminal backend owns ANSI/CSI and UTF-8 byte-stream reconstruction, including split-read state and the standalone-Escape ambiguity policy. Those details remain below the Input ABI.

MiniFT8's application UI therefore has no ncurses/Linux dependency.

## 13. Resident shell versus applications

Current Linux resident shell:

```text
help
status
apps
run <app>
exit
```

Portable/domain applications include:

```text
MiniFT8
hello cat cp date df free ls mkdir mv nano rm rmdir
```

`put/get`, `suspend`, `poweroff`, and similar operations are platform-dependent. They may exist on a target where useful and be absent elsewhere; no fake implementation is required.

## 14. Mocks and simulation

Mocks stay underneath MiniShell:

```text
MiniFT8 core
    |
MiniShell ABI
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
MiniFT8 pure UI state/action behavior
stateful Linux terminal parser split-boundary behavior
MiniFT8 runtime launch/navigation/persistence/relaunch/exit
```

CI also runs the retained platform-neutral MiniShell service/unit suite, including the Audio ABI/service tests.

Service/unit tests remain more important than merely proving that one native app can load.

## 16. Module-size and internal-debt policy

The architecture does not equate "one owner" with "one giant file." An owner may be implemented by several private helper modules while presenting one semantic service.

The H1-H5 architecture-audit debt is resolved. In particular:

```text
Linux backend      split by responsibility
portable core      no POSIX loader-result leakage
Filesystem owner   private path/handle/quota helpers split out
terminal input     stateful ANSI/CSI/UTF-8 parser isolated below Input
```

Future debt should be recorded when discovered rather than allowed to blur ownership boundaries. See `../project/consistency-check.md` for the audit record.

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

MiniFT8-V3 actively drives major service decisions. Audio V1 and deterministic WAV RX are now established. The next application slice consumes that Audio ABI inside MiniFT8 and feeds the FT8 DSP path. Control/Radio follows when its real application-facing contract is implemented and tested independently.
