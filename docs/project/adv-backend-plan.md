# ADV Backend and MiniFT8 Profile Validation Plan

## Status

This is the current development priority.

MiniFT8 RX-1B is **paused, not abandoned**. RX-1A remains the frozen decoder/golden baseline. RX-1B resumes after the cross-platform checkpoint defined below is complete.

Stages **A0, A1, and A2 are complete**. The active implementation stage is **A3 — Filesystem and Time/Location**.

## Goal

Use two MiniShell backends and two MiniFT8 profiles to exercise the architecture from both directions:

```text
MiniShell backends
    Linux
    ADV

MiniFT8 profiles
    DESKTOP
    ADV
```

Required combinations:

```text
Linux backend + DESKTOP profile   normal pc-1 development
Linux backend + ADV profile       constrained/golden ADV behavior on pc-1
ADV backend   + ADV profile       real Cardputer ADV hardware
```

`ADV backend + DESKTOP profile` is not a required or supported configuration.

The most important comparison is:

```text
Linux backend + ADV profile
            versus
ADV backend + ADV profile
```

The MiniFT8 core and profile are the same. Only the MiniShell backend changes.

## Locked decisions

1. **MiniShell public API remains the platform boundary.** MiniFT8 never includes ESP-IDF, M5Cardputer, M5Unified, board-driver, POSIX, or Linux headers.
2. **MiniFT8 profile is application policy, not a backend identity.** Profile and backend are independent.
3. **Current inherited MiniFT8-V2/Cardputer behavior becomes the `ADV` profile.** Do not redesign that behavior merely to create the profile.
4. **`DESKTOP` is the first new MiniFT8 profile.** It may use a larger logical display/history/resource policy; initial target is approximately 20 RX text lines where useful.
5. **Cardputer ADV V1 uses static application composition.** MiniShell and selected applications, including runtime app `ft8`, are compiled into one ESP-IDF firmware image.
6. **Runtime ELF/application loading on ADV is deferred, not rejected.** It is not required for the first ADV backend and adds loader/linker/flash-mapping complexity. Future smaller applications may explore runtime loading when useful.
7. **The user-facing app lifecycle remains the same where practical:** `apps`, `run <app>`, direct `<app>`, application return, then shell. ADV V1 implements this with a compiled-in app registry.
8. **MiniFT8-V2 is reference material only.** Do not fix or refactor V2. Reuse proven hardware behavior by implementing new ADV backend/providers under MiniShell.
9. **Linux remains the reference behavior and full production target.** Portable-core changes must preserve Linux behavior and tests.
10. **Do not implement live ADV QMX audio merely to finish this checkpoint.** Add ADV Audio when the RX/TX vertical slice actually requires it; the API may report Audio unavailable before then.
11. **The MiniShell public API is not yet frozen for backward compatibility.** Breaking API changes are allowed when they improve clarity, ownership, portability, or real application fit. A formal binary ABI may be introduced later if independently built `.so` or `.elf` applications need cross-version compatibility.
12. **Code-facing names are lowercase.** Project names remain MiniShell/MiniFT8 in prose, while executables, runtime app names, source paths, and persistent filenames use forms such as `minishell`, `ft8`, `apps/ft8/`, and `/flash/ft8/station.txt`. Normal C macros remain uppercase.
13. **Protocol selection is application selection.** The current runtime app `ft8` is FT8-only. Future `ft4`, `cw`, `rtty`, `js8`, etc. are separate applications and are created only when their implementation begins.
14. **ADV persistent storage uses two filesystems and no NVS.** `/flash` is LittleFS on internal flash; `/sd` is FATFS on the removable SD card. Internal configuration and other persistent MiniShell/backend state use ordinary files under `/flash`; do not introduce a second NVS persistence model.
15. **ADV must operate without an SD card.** Compiled-in applications always have priority. When runtime file-based loading is later enabled, names not provided internally are searched in `/flash/apps` first and `/sd/apps` second. External applications are not expected to replace internal ones unless an explicit override mechanism is designed later.
16. **Initial ADV `/flash` size is 2 MiB LittleFS, provisional.** This is an initial partition choice, not a permanent API/property; change it later if measured firmware size, application storage, or field use justifies a different allocation.
17. **ADV resident-shell scrollback is deferred, not rejected.** The current shell reacquires a clean 7-row display after a foreground app exits. A later console enhancement should retain about 50 lines and allow scrolling the 7-row viewport. This belongs to private resident-console behavior and must not alter application Display semantics.

## Stage A0 — portable resident shell/startup boundary and terminology cleanup — COMPLETE

Goal: remove direct Linux terminal assumptions from the portable MiniShell control plane and keep the active code/documentation terminology consistent.

Completed one-time normalization:

```text
ABI-facing terminology -> API terminology
MINISHELL_ABI_VERSION   -> MINISHELL_API_VERSION
abi_version             -> api_version
docs/abi/               -> docs/api/
apps/MiniFT8/           -> apps/minift8/ -> apps/ft8/
MiniFT8 runtime name    -> minift8 -> ft8
/flash/MiniFT8/Station.txt -> /flash/minift8/station.txt -> /flash/ft8/station.txt
protocol Mode state     -> removed from ft8; protocol switching is app switching
```

Backward-compatibility/append-only promises were also removed while the API remains under active architectural development.

Portable shell/startup cleanup completed:

```text
core/minishell_runtime.c     portable MiniShell lifecycle via minishell_run()
platform/linux/main.c        Linux C entry point only
platform/linux/linux_console.c
                             Linux stdin/stdout + stdio buffering ownership
core/shell.c                 shell parsing/control only; no stdin/stdout access
core/platform_backend.h      private resident console read/write boundary
```

The private resident console is intentionally **not** part of the public MiniShell API. Applications continue to use Display, Input, System, and other public services.

Exit criteria — passed:

```text
Linux shell behavior unchanged
all Linux integration/unit tests green
portable shell/core has no direct dependency on POSIX terminal behavior
Linux main()/stdin/stdout details live under platform/linux/
active MiniShell application-facing terminology consistently says API
code-facing runtime/path names follow the lowercase naming rule
no accidental backward-compatibility promise remains in active architecture/docs
```

Reference commit:

```text
1cb44be4  refactor: isolate resident console and startup boundary
```

## Stage A1 — ADV ESP-IDF build skeleton — COMPLETE

Goal: create `platform/adv/` as the second real MiniShell backend and prove the portable runtime/application lifecycle can be composed as ESP32-S3 firmware.

A1 deliberately uses the ESP32-S3 USB Serial/JTAG console as a **temporary private resident-console provider**. This is bring-up infrastructure only. Cardputer display and keyboard become the real resident/UI providers in A2; applications never use the private console directly.

Implemented:

- ESP-IDF firmware composition for Cardputer ADV / ESP32-S3;
- portable MiniShell core/services compiled into the firmware;
- `app_main()` calling the same portable `minishell_run()` used by the Linux composition;
- ADV platform init/shutdown and platform identity (`adv`);
- temporary private USB Serial/JTAG resident console;
- explicit A1 resource policy with no additional global MiniShell memory/storage quota;
- compiled-in app registry;
- existing portable `hello` app packaged statically through an ADV-private entry wrapper;
- host-side static-registry unit test;
- GitHub ESP-IDF v5.5.1 firmware-build workflow;
- initial 8 MiB flash partition table reserving 2 MiB LittleFS for future `/flash` mounting.

A1 intentionally does **not** implement public Memory, Display, Input, Filesystem, Time/Location, or Audio providers merely to satisfy the build skeleton. `System.write` is provided because the portable A1 `hello` probe requires it.

Do not copy V2 structure. V2 may be consulted for proven board initialization and hardware behavior only.

Software/build validation — passed:

```text
Linux reference workflow remains green
ADV static app-registry unit test passes
ESP-IDF v5.5.1 esp32s3 firmware builds in GitHub CI
```

Real-device validation — passed on Cardputer ADV:

```text
Cardputer ADV boots MiniShell
USB Serial/JTAG shows M$>
status reports platform : adv
apps lists hello
hello returns cleanly to M$>
```

Reference commits:

```text
b795842e  feat: add ADV A1 ESP-IDF build skeleton
142dced2  fix: package ADV hello without CMake source mutation
```

A1 exit criteria are fully satisfied.

## Stage A2 — System, Memory, Display, and Input — COMPLETE

Goal: make the shell and portable UI usable on real Cardputer hardware.

Implemented:

- Cardputer-facing resident shell on the ADV display and keyboard;
- USB Serial/JTAG retained as diagnostic mirror/fallback;
- System diagnostic sink kept distinct from application Display;
- ESP-IDF heap-backed Memory provider with application accounting/free-block information;
- Cardputer 240x135 text Display provider reporting a 20-column x 7-row logical text surface;
- `MINI_TEXT_ATTR_INVERSE` rendering;
- Cardputer ADV TCA8418 keyboard Input provider normalized to logical MiniShell key events;
- standalone special-key meanings needed by Cardputer controls without exposing matrix/scancode details;
- shared portable Input queue/service semantics preserved;
- M5 dependencies kept below MiniShell and audio-sensitive initialization behavior preserved.

V2 was used only as the reference for known-good Cardputer display/keyboard pins, key behavior, and the critical display-only initialization pattern. A2 does not call `M5.begin()` and does not claim mic, speaker, codec, or I2S resources.

### ADV Display V1 decisions

The existing MiniShell text Display API is sufficient for ADV V1. No graphics extension is required for the initial backend.

```text
physical panel             240 x 135 pixels
MiniShell capability       TEXT only
logical text surface       20 columns x 7 rows
target fixed-width font    12 x 16 pixels
```

The physical row mapping is:

```text
logical row 0              19 px
physical gap                2 px
logical rows 1..6          19 px each
                           -----
total                      135 px
```

Pixel/font/controller details remain ADV-backend implementation details. Applications see only the logical 20x7 cell grid and never pixel coordinates or the 2-pixel physical gap.

Graphics remain deferred. ADV V1 does not require a graphical waterfall or graphical countdown. An application may render a countdown as ordinary text when useful. If a real future application requirement justifies graphics, extend Display separately rather than distorting the text API.

Meaning of logical rows belongs above MiniShell. For the `ft8` ADV profile, the intended policy is:

```text
row 0       contextual status / temporary help / countdown text
rows 1..6   main FT8 content
```

That is `ft8` application/profile policy, **not** an ADV backend or MiniShell Display rule. Other applications may use all seven rows differently.

The portable `hello` application now acts as a normal foreground application: it uses Display/Input, remains visible until explicit exit, and returns the screen to the shell afterward. The A2 public API probe verifies Memory, Display geometry, inverse text, and Input on real hardware.

Real-device validation — passed:

```text
MiniShell prompt visible on ADV display      PASS
Cardputer keyboard command entry             PASS
status System/Memory/Display/Input ready      PASS
hello foreground Display/Input lifecycle     PASS
A2 Memory accounting                         PASS
A2 Display 20x7                              PASS
A2 inverse text                              PASS
A2 Cardputer Input                           PASS
a2_probe: PASS diagnostic over USB           PASS
```

Current post-app shell behavior is intentionally simple: after a foreground app releases the display, the shell reacquires a clean 7-row screen and writes `M$>` at the top. A future private-console enhancement should maintain roughly 50 lines of shell history with scroll up/down; USB provides long history in the meantime.

Reference commits include:

```text
d1736367  feat: add ADV A2 display keyboard and memory providers
e7d40b8b  test: add ADV A2 public API probe
fea18d0f  docs: describe ADV A2 hardware providers
e6dc0dcc  refactor: make hello a foreground display app
5864a8e9  test: exercise hello as foreground app
d3ba0106  docs: describe hello as foreground app
```

A2 exit criteria are fully satisfied.

## Stage A3 — Filesystem and Time/Location

Goal: support the persistent application services needed by MiniFT8 configuration and normal utilities while preserving reliable SD-less operation.

### ADV storage V1 decisions

```text
/flash    LittleFS on internal flash, initially 2 MiB (provisional)
/sd       FATFS on removable SD card
NVS       not used
```

LittleFS is the sole internal persistent-storage mechanism. The initial 2 MiB allocation is subject to change after real firmware/storage measurements; it is not exposed as a fixed MiniShell API assumption. MiniShell/backend settings that need persistence are stored as ordinary files under `/flash` rather than in NVS. FATFS exists only for the removable SD namespace.

The SD card is optional. Failure to mount or absence of `/sd` must not prevent MiniShell from booting, using internal configuration, running compiled-in applications, or using `/flash`.

Application resolution on ADV follows a deliberately simple rule:

```text
compiled-in app exists?   -> use it
otherwise:
    1. /flash/apps
    2. /sd/apps
```

Compiled-in applications always take priority. External applications are intended to add applications, not silently replace internal ones. If replacement/override is ever needed, define that behavior explicitly later rather than deriving it from the normal search order. This external search policy becomes active only when runtime file loading is implemented; ADV V1 continues to use its compiled-in registry.

MiniFT8 owns its own file-placement policy. In particular, `RxTxLog` can be a significant storage consumer, so its destination is configurable in `/flash/ft8/station.txt`. It may therefore be directed to `/sd` when desired rather than forcing log growth into the internal LittleFS allocation.

Tasks:

- allocate/mount approximately 2 MiB LittleFS as `/flash` for the initial ADV partition layout, subject to later adjustment;
- mount/map optional `/sd` to FATFS on the removable SD card;
- do not add NVS persistence;
- implement file and directory primitives required by the existing Filesystem service;
- preserve MiniShell normalization, handle ownership, lifecycle, and quota semantics;
- provide monotonic time and sleep;
- provide UTC set/get/persistence through the MiniShell Time/Location ownership model using `/flash` when persistence is required;
- preserve support for configured default location using `/flash` persistence;
- add GPS/live-location provider later through the existing resident update hooks rather than exposing GPS directly to MiniFT8.

Exit criteria:

```text
MiniShell boots and operates with no SD card inserted
/flash is available through LittleFS
optional /sd is available through FATFS when inserted
ls/cat/basic filesystem behavior works through MiniShell API
/flash/ft8/station.txt can be read/written through storage_service
date/time baseline works
shared Filesystem and Time/Location contract probes pass
```

## Stage P1 — formalize MiniFT8 profiles on Linux

Goal: make profile selection explicit before relying on the ADV hardware port.

Initial profiles:

```text
ADV
    inherited Cardputer/V2 presentation and resource policy
    current 6-line RX behavior where applicable

DESKTOP
    pc-1 development policy
    larger display/history/resource allowances
    approximately 20 RX text lines where useful
```

Tasks:

- introduce a small MiniFT8-owned profile type/configuration;
- convert current hard-coded ADV-sized presentation/resource assumptions into `ADV` profile values;
- add `DESKTOP` values only for real differences we currently need;
- allow Linux to select profile at runtime, e.g. `ft8 --profile adv` and `ft8 --profile desktop` or an equivalent stable interface;
- keep AutoSeq, QSO policy, scheduler logic, FT8 protocol behavior, logging semantics, and other shared logic profile-independent.

Exit criteria:

```text
Linux + ADV profile preserves inherited behavior
Linux + DESKTOP profile exercises larger host presentation/resource policy
no platform/backend identity is tested inside shared MiniFT8 logic
```

## Stage P2 — MiniFT8 on ADV backend

Goal: compile the same MiniFT8 `ft8` application core into the ADV firmware and run the same `ADV` profile used on Linux.

Tasks:

- register runtime application `ft8` in the ADV static app registry;
- default `ft8` to the `ADV` profile on the Cardputer build;
- enter/exit `ft8` through the normal MiniShell foreground lifecycle;
- verify configuration persistence and UI navigation;
- do not add Cardputer-specific branches to MiniFT8 application modules.

Exit criteria:

```text
Linux + ADV profile works
ADV   + ADV profile works
same MiniFT8 core sources are used
apps/ft8/ contains no ADV/ESP-IDF/M5 hardware dependencies
```

## Stage V1 — cross-backend/profile validation checkpoint

This is the checkpoint that must pass before RX-1B resumes.

Required matrix:

| MiniShell backend | MiniFT8 profile | Required |
| --- | --- | --- |
| Linux | DESKTOP | yes |
| Linux | ADV | yes |
| ADV | ADV | yes |
| ADV | DESKTOP | no |

Validation should compare application-visible behavior rather than physical rendering details:

- app launch/exit lifecycle;
- MiniFT8 default/config state;
- same UI actions causing the same MiniFT8 state transitions;
- filesystem semantics and `/flash/ft8/station.txt` persistence;
- logical key meanings;
- resource-limit behavior where values are intentionally shared;
- absence of platform-specific logic above the MiniShell API.

A shared portable API/service probe should be compiled dynamically on Linux and statically on ADV wherever practical. Unit/service tests remain more important than one end-to-end demonstration.

## ADV Audio — deliberately later

ADV Audio is still part of the backend, but it is not required to prove the initial two-backend/two-profile architecture.

When the MiniFT8 RX/TX vertical slice needs real Cardputer audio, implement providers behind the existing Audio API using V2 as hardware reference:

```text
QMX/UAC
microphone where justified
future TX audio path
```

MiniShell continues to own transport/lifecycle/native format conversion. MiniFT8 continues to own ordinary-audio versus I/Q meaning and FT8 DSP policy.

## Resume point for RX

After Stage V1 passes:

```text
resume RX-1B
    -> top-down RX module/interface design
    -> RX-1C and later implementation
```

At that point MiniFT8 development has already demonstrated that:

```text
the MiniShell backend boundary survives a real embedded port
and
the MiniFT8 profile boundary survives two materially different environments
```

That is the architectural purpose of this detour.
