# MiniShell Architecture Cleanup Gate

Status: **Draft for review — C0/C1/C2/C3 complete**  
Date: 2026-09-10

## Purpose

Before adding the first field-usable external application, review and tighten the MiniShell/MiniFT8-V3 boundaries so the architecture is not only documented but also mechanically difficult to violate.

The immediate successor to this cleanup is the Keyer application described in `../keyer/README.md`.

This task is architectural cleanup only. It should not change FT8 behavior.

## 1. Invariants to enforce

### 1.1 Application/platform boundary

Portable application source depends on the MiniShell public API and application-owned modules only.

```text
application
    |
    v
MiniShell public API
    |
    v
resident MiniShell services
    |
    v
private backend/provider boundary
    |
    v
Linux / NuttX / ESP-IDF / hardware / mocks
```

Application source must not include or depend directly on Linux/POSIX, NuttX, ESP-IDF, FreeRTOS, M5, board-driver, USB, UART, GPIO, I2S, ALSA, or other platform implementation interfaces.

### 1.2 No-side-talk rule

For an application with several logical modules, `app_controller` is the cross-module coordinator.

Sibling logical modules do not call one another opportunistically behind the controller.

```text
config_service ----X----> keyout
ui_shell       ----X----> config_service
rx_frontend    ----X----> ft8_engine

config_service
      |
      v
app_controller
      |
      v
other application module
```

A module may call its own private implementation submodules. For example, `ft8_engine` may own and call decoder/monitor/codec implementation pieces. That is vertical decomposition inside one ownership domain, not side talk between independent application domains.

### 1.3 One owner per mutable state/resource domain

Examples:

```text
MiniShell App manager       foreground app lifecycle
MiniShell Filesystem        app-visible files/handles/quota
MiniShell Audio             app-visible audio stream lifecycle
MiniShell Digital I/O       future app-visible digital-line lifecycle
MiniShell Control           future generic radio/device control lifecycle

MiniFT8 app_controller      FT8 cross-module sequencing
MiniFT8 config_service      FT8 parsed configuration state
MiniFT8 auto_seq            QSO/autoseq policy state
MiniFT8 ui_shell            UI-local navigation state
```

No second module should silently become a competing owner.

### 1.4 Edge adapters may use MiniShell; pure domain modules should not

MiniShell is the platform abstraction and should not be hidden behind a duplicate generic HAL.

Application edge modules may consume the appropriate MiniShell service directly when that is their responsibility. Pure domain/state-machine modules should operate on application-owned types and data.

## 2. Current audit result

The current MiniShell and MiniFT8-V3 structure is substantially clean.

In particular:

- MiniShell public/private service boundaries are separated.
- application lifecycle cleanup is centralized through the app manager/service layer;
- MiniFT8 has no direct platform implementation dependency;
- `app_controller` performs configuration/storage/AutoSeq/RX/TX coordination;
- `config_service` and `storage_service` do not call each other directly;
- RX leaf modules such as `rx_frontend`, `rx_slot_framer`, and `rx_result_builder` are application-domain modules;
- `rx_slot_framer` emits events rather than directly invoking `ft8_engine`;
- `ui_shell`, `auto_seq`, `tx_lifecycle`, and `ft8_engine` remain independent of platform APIs;
- `rx_audio_adapter` is an intentional MiniShell edge adapter;
- `tests/ft8_platform_boundary.py` prevents major platform leakage;
- `tests/app_dependency_boundary.py` enforces application-local module dependencies, private-header ownership, and the C2 lifecycle/UI boundary;
- `AppController` state is opaque outside the controller ownership domain;
- `app_controller` produces one complete `UiModel` snapshot while `ui_shell` alone decides which fields are visible;
- `ft8_main` performs lifecycle/wiring only and compares final `UiFrame` output rather than interpreting UIScreen/submenu/model fields;
- runtime ADV ELF loading is now an active architecture target rather than a deferred experiment.

The remaining cleanup work is configuration ownership/file naming before Keyer starts.

## 3. Cleanup tasks

### C0 — COMPLETE — Application dependency/no-side-talk enforcement

Implemented a small reusable checker at:

```text
tests/app_dependency_boundary.py
```

The FT8 rule set explicitly declares logical module ownership and allowed dependencies. The checker rejects:

- forbidden sibling-module includes;
- source/header files under enforced application roots that have no declared module owner;
- access to declared private headers from outside their owning module.

Linux CI runs both the checker self-test and the real MiniFT8 rule set before compiling.

C0 immediately found and removed one unnecessary coupling: `config_service.h` had included `ft8/app_types.h` only to obtain `uint8_t`; it now includes `<stdint.h>` directly.

The checker is intentionally reusable: future applications such as Keyer can add their own small rule map without creating another dependency framework.

### C1 — COMPLETE — Hide `AppController` implementation state

`AppController` is now an opaque public C type:

```text
outside app_controller/
        |
        `--> AppController * + app_controller_*() only

inside app_controller/
        |-- configuration state
        |-- AutoSeq state
        |-- storage state
        |-- RX state
        `-- TX state
```

The concrete structure lives in:

```text
apps/ft8/src/app_controller/app_controller_internal.h
```

Public lifetime is through:

```text
app_controller_create()
app_controller_destroy()
```

The controller object is allocated/freed through MiniShell Memory, so the implementation remains platform-independent and participates in normal per-application resource accounting.

Production `ft8_main` holds only `AppController *`; it cannot access `config`, `auto_seq`, `storage`, RX, or TX state directly. The C0 checker marks `app_controller_internal.h` private to the controller module and rejects access from other production modules.

The AS-7 TX lifecycle test remains an intentional white-box test and explicitly includes the private header. Tests may inspect implementation state when that is the purpose of the test; production module boundaries remain strict.

Verification completed:

- application dependency checker passes;
- Linux build/CTest/AS-8/strict unit suite passes;
- the full FT8 reference suite RX-1C through RX-7 passes, including the RX-7 production decoded-UI golden test;
- Cardputer ADV ESP-IDF v5.5.1 firmware build passes.

During C1 the ADV gate caught an ESP-IDF build-system difference: component CMake files are also evaluated in script mode, where `set_source_files_properties()` is unavailable. The per-source private-controller compile definition is now guarded so it is applied only during the real configure/build phase.

### C2 — COMPLETE — Keep `ft8_main` lifecycle-only

`ft8_main` now owns foreground lifecycle and top-level wiring without interpreting UIScreen/submenu policy or individual `UiModel` fields.

The implemented flow is:

```text
MiniShell input
      |
      v
UI adapter -> ui_shell -> AppAction
                         |
                         v
                  app_controller
                         |
                         v
                 complete UiModel
                         |
                         v
ui_shell -> UiFrame -> UI adapter -> MiniShell Display
```

The public controller now exposes one model facade:

```text
app_controller_build_model()
```

It builds a complete application snapshot, including diagnostics such as memory state. The former component builders remain private to `app_controller`. `ui_shell` decides which model fields belong on the current screen.

The main loop does not compare model internals. It builds the complete model, renders a candidate `UiFrame` in memory, and sends the frame to the display adapter only when the final rendered frame differs from the previously displayed frame.

This keeps redraw policy generic:

```text
state changes
    |
    v
complete UiModel
    |
    v
ui_shell renders candidate UiFrame
    |
    +-- same frame ----> no display write
    |
    `-- changed frame -> display write
```

As a consequence, a no-op input no longer produces a redundant repaint. Visible UI semantics are unchanged; only duplicate terminal/display output is removed. The Linux integration test was corrected so it no longer requires a second identical frame after the O-screen `Protocol: FT8` no-op selection.

The C0 architecture checker now also rejects C2 regressions in `ft8_main.c`, including:

- inspection of `ui.screen` or `ui.submenu`;
- hard-coded `SCREEN_*` or `UI_SUBMENU_*` policy;
- inspection of individual `model.*` fields.

The FT8 reference workflow gate was also strengthened: changes to the FT8 main/UI/model boundary now automatically run the reference suite rather than depending on RX-specific symbol matching.

Verification completed:

- application dependency/lifecycle checker passes;
- Linux build and all CTests pass;
- AS-8 equivalence and strict unit tests pass;
- full FT8 reference suite RX-1C through RX-7 passes, including RX-7 production decoded UI;
- Cardputer ADV ESP-IDF v5.5.1 firmware build passes.

No generic event bus or object framework was introduced.

### C3 — COMPLETE — External application loading is an active architecture target

The current packaging model is now documented as:

```text
Linux/Mint          runtime .so
Cardputer ADV V1    compiled-in registry baseline
Cardputer ADV next  runtime /sd/<app>.elf
Tab5/NuttX          native loadable mechanism where practical
```

The first field-usable ADV external application target is:

```text
/sd/keyer.elf
```

C3 updated the canonical application/runtime documentation so that:

- application source remains independent of loader/container format;
- Linux continues to use runtime `.so` modules;
- ADV runtime ELF is active work rather than a speculative future possibility;
- the initial ADV external-app path convention is `/sd/<app>.elf`;
- `keyer.elf` remains a portable MiniShell application and must not include ESP-IDF, FreeRTOS, M5/Cardputer, FATFS, or ELF-loader interfaces;
- discovery, ELF parsing, relocation, symbol resolution, execution setup, unloading, and cleanup stay resident/private to MiniShell and the ADV backend;
- static ADV applications remain valid during transition/testing;
- collision/precedence between a static and external app with the same name is deliberately left for loader implementation;
- a formal stable cross-release binary ABI is still not frozen.

Updated documents include:

```text
apps/README.md
docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/resident-vs-app.md
docs/project/adv-backend-plan.md
docs/project/progress.md
platform/adv/README.md
```

The completed V1 ADV plan remains historical evidence. It explicitly records that static composition was a V1 choice and that C3 supersedes the old "ELF later" wording for current development.

C3 is documentation/architecture only. It does not implement an ELF loader.

### C4 — Record configuration ownership and file naming

Reserve these names:

```text
/flash/config.txt
    MiniShell-owned resident/platform configuration

/flash/<app>/setting.txt
    application-owned configuration
```

Examples:

```text
/flash/keyer/setting.txt
/flash/ft8/setting.txt
```

#### MiniShell configuration

`/flash/config.txt` contains only configuration for resources MiniShell itself owns or requires to operate the platform, for example:

- debug UART GPIO assignment, especially when USB-C OTG is occupied by a radio;
- RTC/GPS GPIO assignment;
- GPS baud-rate policy/autodetection;
- platform resource sharing/detection needed by MiniShell.

If RTC and GPS share a physical connection, detection/arbitration belongs to MiniShell because MiniShell owns those resident resources.

#### Application configuration

`/flash/<app>/setting.txt` contains application-specific behavior and deployment configuration.

A hardware-specific app setting is allowed. Portability applies to the application code/API boundary, not to one universal configuration file for every board.

For example, a Keyer deployment may define dit/dah input GPIOs and KeyOut GPIOs. Keyer interprets those values and asks generic MiniShell Digital I/O to configure/use the requested lines. MiniShell does **not** know that a line is `dit`, `dah`, `paddle`, or `KeyOut`.

```text
Keyer setting
    dit_gpio = <deployment-specific line>
        |
        v
Keyer app_controller
        |
        v
KeyIn module
        |
        v
MiniShell Digital I/O: open/read/close generic line
```

This distinction is deliberate:

> Portable application binary; deployment-specific application settings.

Do not create MiniShell configuration entries named for Keyer or another domain application.

## 4. Verification gate

Before Keyer implementation starts, the cleanup is complete when:

1. MiniFT8 platform-boundary test passes;
2. MiniFT8 dependency/no-side-talk test passes;
3. controller internals cannot be casually accessed outside the controller implementation;
4. `ft8_main` is lifecycle/wiring code and no longer interprets V/Memory or similar screen-specific policy;
5. documentation reflects active ADV runtime ELF direction and configuration ownership/naming;
6. Linux build/unit/integration tests pass;
7. ADV build/tests pass;
8. the existing RX7 golden WAV integration test still passes unchanged in visible behavior.

Items 1-4 and 6-8 are satisfied after C0-C2. The runtime-ELF half of item 5 is satisfied by C3; the configuration half remains for C4.

## 5. Non-goals

Do not use this cleanup to:

- redesign FT8 DSP;
- change AutoSeq policy;
- add Control/CAT implementation;
- add Digital I/O implementation;
- implement the Keyer;
- implement the ELF loader during C3 documentation cleanup;
- freeze a long-term binary ABI;
- create a generalized dependency injection/event framework.

Those are separate tasks.

## 6. Review questions

Resolved:

1. **Opaque `AppController`: yes.** C1 uses a small ordinary-C opaque-pointer pattern with MiniShell Memory ownership; no object framework was introduced.
2. **Dependency checker scope: reusable immediately.** C0 keeps one generic checker with a small per-application rule map; Keyer will add another map later.
3. **Complete-model rendering: build one complete `UiModel`, then compare final `UiFrame`s.** C2 keeps screen-specific visibility in `ui_shell` and prevents `ft8_main` from learning submenu semantics.
4. **ADV external app path: `/sd/<app>.elf` initially.** The first field application is `/sd/keyer.elf`; static/external name precedence remains a loader-implementation decision.

Still open:

1. Is `/flash/ft8/setting.txt` the desired eventual rename from the current `station.txt`, or should that migration remain a later application-specific decision? This does not block the ownership rule itself.

Until C4 is reviewed/completed, this remains a cleanup plan rather than the final architecture-cleanup record.
