# MiniShell Architecture Cleanup Gate

Status: **Draft for review**  
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
- `app_controller` currently performs configuration/storage/AutoSeq/RX/TX coordination;
- `config_service` and `storage_service` do not call each other directly;
- RX leaf modules such as `rx_frontend`, `rx_slot_framer`, and `rx_result_builder` are application-domain modules;
- `rx_slot_framer` emits events rather than directly invoking `ft8_engine`;
- `ui_shell`, `auto_seq`, `tx_lifecycle`, and `ft8_engine` remain independent of platform APIs;
- `rx_audio_adapter` is an intentional MiniShell edge adapter;
- `tests/ft8_platform_boundary.py` already prevents major platform leakage.

The remaining work is mainly enforcement and tightening rather than architectural redesign.

## 3. Cleanup tasks

### C0 — Add application dependency/no-side-talk enforcement

Add a CI/test checker for MiniFT8 logical-module dependencies, separate from the existing platform-boundary checker.

The checker should reject forbidden sibling-module includes/calls while allowing:

- `app_controller` to coordinate application modules;
- lifecycle/wiring code to call the controller and edge adapters;
- a logical module to use its explicitly owned private implementation submodules;
- MiniShell edge adapters to consume the public MiniShell API.

The goal is to turn the no-side-talk rule from prose into an executable architecture test.

The checker should be simple and explicit rather than a general dependency framework.

### C1 — Hide `AppController` implementation state

Current `app_controller.h` exposes internal state such as ConfigService, AutoSeq, StorageService, RX state, and TX state.

That makes accidental bypasses possible even though current code does not abuse them.

Target:

```text
outside app_controller/
        |
        `--> app_controller_*() public application interface only

inside app_controller/
        |-- configuration state
        |-- AutoSeq state
        |-- storage state
        |-- RX state
        `-- TX state
```

Preferred direction: make controller implementation state private/opaque. The exact C storage mechanism should be chosen during implementation with embedded memory cost and simplicity in mind; do not introduce a complex object framework merely to hide a struct.

### C2 — Keep `ft8_main` lifecycle-only

`ft8_main` should own foreground lifecycle and top-level wiring, not interpret internal UI state.

Current code knows that the V/Memory submenu requires a memory-model refresh and contains model-specific redraw comparisons.

Target shape:

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
                      UiModel
                         |
                         v
ui_shell -> UiFrame -> UI adapter -> MiniShell Display
```

Remove the need for `ft8_main` to understand a particular UIScreen/submenu meaning. Keep the replacement small and explicit; do not create a generic event bus.

### C3 — Make external application loading an active architecture target

Older documentation describes ADV runtime ELF loading as a future experiment. The active project direction is now:

```text
/sd/keyer.elf
```

as a real runtime-loaded application target on Cardputer ADV.

Update architecture/application documentation so that:

- application source remains independent of loader/container format;
- Linux continues to use runtime `.so` modules;
- ADV begins a real runtime `.elf` path;
- independently loaded apps still receive only the MiniShell public application interface;
- static ADV applications may remain during transition/testing, but runtime loading is no longer merely speculative.

A formal stable cross-release binary ABI is still not frozen. API/loader work may evolve together while the project is early.

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
2. new MiniFT8 dependency/no-side-talk test passes;
3. controller internals cannot be casually accessed outside the controller implementation;
4. `ft8_main` is lifecycle/wiring code and no longer interprets V/Memory or similar screen-specific policy;
5. documentation reflects active ADV runtime ELF direction and configuration ownership/naming;
6. Linux build/unit/integration tests pass;
7. ADV build/tests pass;
8. the existing RX7 golden WAV integration test still passes unchanged in behavior.

## 5. Non-goals

Do not use this cleanup to:

- redesign FT8 DSP;
- change AutoSeq policy;
- add Control/CAT implementation;
- add Digital I/O implementation;
- implement the Keyer;
- freeze a long-term binary ABI;
- create a generalized dependency injection/event framework.

Those are separate tasks.

## 6. Review questions

Before marking this document final, decide:

1. Is an opaque `AppController` worth the small C lifecycle/storage change, or is a narrower enforcement mechanism preferable?
2. What is the simplest way for `ft8_main` to request/render a complete model without knowing submenu semantics?
3. Should the dependency checker initially cover MiniFT8 only, then become a template for Keyer, or should we create a small reusable checker immediately?
4. Is `/flash/ft8/setting.txt` the desired eventual rename from the current `station.txt`, or should that migration remain a later application-specific decision?

Until those are reviewed, this remains a planning document rather than an implementation specification.
