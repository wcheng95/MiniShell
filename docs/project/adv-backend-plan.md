# ADV Backend and MiniFT8 Profile Validation Plan

## Status

This is the current development priority.

MiniFT8 RX-1B is **paused, not abandoned**. RX-1A remains the frozen decoder/golden baseline. RX-1B resumes after the cross-platform checkpoint defined below is complete.

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
5. **Cardputer ADV V1 uses static application composition.** MiniShell and selected applications, including MiniFT8, are compiled into one ESP-IDF firmware image.
6. **Runtime ELF/application loading on ADV is deferred, not rejected.** It is not required for the first ADV backend and adds loader/linker/flash-mapping complexity. Future smaller applications may explore runtime loading when useful.
7. **The user-facing app lifecycle remains the same where practical:** `apps`, `run <app>`, direct `<app>`, application return, then shell. ADV V1 implements this with a compiled-in app registry.
8. **MiniFT8-V2 is reference material only.** Do not fix or refactor V2. Reuse proven hardware behavior by implementing new ADV backend/providers under MiniShell.
9. **Linux remains the reference behavior and full production target.** Portable-core changes must preserve Linux behavior and tests.
10. **Do not implement live ADV QMX audio merely to finish this checkpoint.** Add ADV Audio when the RX/TX vertical slice actually requires it; the API may report Audio unavailable before then.
11. **The MiniShell public API is not yet frozen for backward compatibility.** Breaking API changes are allowed when they improve clarity, ownership, portability, or real application fit. A formal binary ABI may be introduced later if independently built `.so` or `.elf` applications need cross-version compatibility.
12. **Code-facing names are lowercase.** Project names remain MiniShell/MiniFT8 in prose, while executables, runtime app names, source paths, and persistent filenames use forms such as `minishell`, `minift8`, `apps/minift8/`, and `/flash/minift8/station.txt`. Normal C macros remain uppercase.

## Architectural issue found before the port

The application API is backend-neutral, but the resident MiniShell shell/startup still contains Linux-style assumptions:

```text
core/main.c     normal C main()/stdio startup assumptions
core/shell.c    fgets/printf/puts on stdin/stdout
```

These must be cleaned before ADV is treated as a true peer backend. This is a private MiniShell-core/backend issue; it must not change the public application API semantics unnecessarily.

## Stage A0 — portable resident shell/startup boundary and terminology cleanup

Goal: remove direct Linux terminal assumptions from the portable MiniShell control plane and keep the active code/documentation terminology consistent.

Completed one-time normalization:

```text
ABI-facing terminology -> API terminology
MINISHELL_ABI_VERSION   -> MINISHELL_API_VERSION
abi_version             -> api_version
docs/abi/               -> docs/api/
apps/MiniFT8/           -> apps/minift8/
MiniFT8 runtime name    -> minift8
/flash/MiniFT8/Station.txt -> /flash/minift8/station.txt
```

Backward-compatibility/append-only promises were also removed while the API remains under active architectural development.

Remaining A0 tasks:

- define a small private resident-console/startup boundary;
- keep Linux stdin/stdout behavior underneath the Linux backend;
- allow ADV to provide Cardputer display/keyboard shell I/O underneath the same private boundary;
- keep `app_manager` and the public MiniShell API behavior intact while doing the shell portability cleanup;
- keep all existing Linux CTest/unit coverage green.

Exit criteria:

```text
Linux shell behavior unchanged
all Linux tests green
portable shell/core has no direct dependency on POSIX terminal behavior
active MiniShell application-facing terminology consistently says API
code-facing runtime/path names follow the lowercase naming rule
no accidental backward-compatibility promise remains in active architecture/docs
```

## Stage A1 — ADV ESP-IDF build skeleton

Goal: create `platform/adv/` as the second real MiniShell backend.

Tasks:

- add an ESP-IDF firmware composition for Cardputer ADV;
- compile portable MiniShell core/services into the firmware;
- provide ADV platform init/shutdown and platform identity;
- define explicit resource limits for the ADV MiniShell application domain;
- add the compiled-in app-registry mechanism;
- initially register a tiny probe/hello app before MiniFT8.

Do not copy V2 structure. V2 may be consulted for proven board initialization and hardware behavior only.

Exit criteria:

```text
firmware builds
Cardputer ADV boots MiniShell
platform reports ADV
apps lists statically registered app(s)
run/return lifecycle works without dynamic loading
```

## Stage A2 — System, Memory, Display, and Input

Goal: make the shell and portable UI usable on real Cardputer hardware.

Tasks:

- System write/status primitives;
- ESP-IDF heap-backed Memory provider and useful free/largest-block reporting;
- Cardputer 240x135 text Display provider;
- Cardputer keyboard Input provider using logical MiniShell key events;
- preserve Display/Input separation;
- reuse portable Input queue/service semantics rather than exposing keyboard-driver state to applications.

V2 is the reference for known-good Cardputer display/keyboard initialization and key behavior.

Exit criteria:

```text
MiniShell prompt visible on ADV
keyboard command entry works
status works
shared service/input probes pass on hardware
```

## Stage A3 — Filesystem and Time/Location

Goal: support the persistent application services needed by MiniFT8 configuration and normal utilities.

Tasks:

- map the MiniShell logical `/flash` and `/sd` namespace to ADV storage;
- implement file and directory primitives required by the existing Filesystem service;
- preserve MiniShell normalization, handle ownership, lifecycle, and quota semantics;
- provide monotonic time and sleep;
- provide UTC get/set/persistence through the MiniShell Time/Location ownership model;
- preserve support for configured default location;
- add GPS/live-location provider later through the existing resident update hooks rather than exposing GPS directly to MiniFT8.

Exit criteria:

```text
ls/cat/basic filesystem behavior works through MiniShell API
/flash/minift8/station.txt can be read/written through storage_service
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
- allow Linux to select profile at runtime, e.g. `minift8 --profile adv` and `minift8 --profile desktop` or an equivalent stable interface;
- keep AutoSeq, QSO policy, scheduler logic, FT8 protocol behavior, logging semantics, and other shared logic profile-independent.

Exit criteria:

```text
Linux + ADV profile preserves inherited behavior
Linux + DESKTOP profile exercises larger host presentation/resource policy
no platform/backend identity is tested inside shared MiniFT8 logic
```

## Stage P2 — MiniFT8 on ADV backend

Goal: compile the same MiniFT8 application core into the ADV firmware and run the same `ADV` profile used on Linux.

Tasks:

- register runtime application `minift8` in the ADV static app registry;
- default MiniFT8 to `ADV` profile on the Cardputer build;
- enter/exit MiniFT8 through the normal MiniShell foreground lifecycle;
- verify configuration persistence and UI navigation;
- do not add Cardputer-specific branches to MiniFT8 application modules.

Exit criteria:

```text
Linux + ADV profile works
ADV   + ADV profile works
same MiniFT8 core sources are used
apps/minift8/ contains no ADV/ESP-IDF/M5 hardware dependencies
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
- filesystem semantics and `/flash/minift8/station.txt` persistence;
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
