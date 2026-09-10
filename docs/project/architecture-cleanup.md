# MiniShell Architecture Cleanup Gate

Status: **COMPLETE — C0/C1/C2/C3/C4**  
Date: 2026-09-10

## Purpose

This gate tightened the MiniShell/MiniFT8-V3 architecture before beginning the first field-usable external application, Keyer.

The completed work preserves these rules:

```text
portable application
        |
        v
MiniShell public API
        |
        v
MiniShell services/runtime
        |
        v
private platform backend
```

and, inside a structured application:

```text
sibling module ----X----> sibling module

module
   |
   v
app_controller
   |
   v
other module
```

The Keyer follow-on plan is `../keyer/README.md`.

## 1. Architecture invariants

### Application/platform boundary

Application source depends on the MiniShell public API and application-owned modules only. Platform implementation interfaces such as POSIX, NuttX, ESP-IDF, FreeRTOS, M5, board drivers, GPIO drivers, FATFS, and loader internals stay below MiniShell.

### No side talk

`app_controller` is the cross-module coordinator. A module may call private implementation helpers inside its own ownership domain, but independent sibling domains do not coordinate behind the controller.

### One owner per mutable state/resource domain

Each application-visible MiniShell resource has one MiniShell owner. Each application state/policy domain likewise has one application owner.

### Edge modules may consume MiniShell services

MiniShell is the platform abstraction. Application edge modules may use the relevant MiniShell service directly when that is their responsibility. Pure domain/state-machine modules remain independent of platform/resource APIs.

## 2. C0 — COMPLETE — dependency/no-side-talk enforcement

Added:

```text
tests/app_dependency_boundary.py
```

The checker uses a small per-application rule map and rejects:

- forbidden sibling-module includes;
- unowned source/header directories under enforced application roots;
- access to declared private headers from outside their owning module.

Linux CI runs both its self-test and the MiniFT8 dependency graph check.

C0 immediately removed one accidental coupling: `config_service.h` had depended on `ft8/app_types.h` only to obtain `uint8_t`; it now includes `<stdint.h>` directly.

The mechanism is reusable for Keyer and future applications.

## 3. C1 — COMPLETE — opaque `AppController`

`AppController` is now opaque outside its ownership domain.

```text
outside app_controller/
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

Lifetime is through `app_controller_create()` / `app_controller_destroy()` using MiniShell Memory. The dependency checker protects the private header.

White-box tests may explicitly inspect private state when that is the test's purpose; production modules may not.

## 4. C2 — COMPLETE — lifecycle-only `ft8_main`

`ft8_main` no longer knows V/Memory screen policy, `SCREEN_*`, `UI_SUBMENU_*`, or individual `UiModel` fields.

The current flow is:

```text
MiniShell input
      |
      v
ui_shell -> AppAction
              |
              v
        app_controller
              |
              v
        complete UiModel
              |
              v
ui_shell -> UiFrame
              |
              v
MiniShell Display
```

`ft8_main` compares final rendered `UiFrame`s and writes the display only when the frame changes. A no-op input therefore no longer causes a redundant repaint.

The dependency checker includes guards against reintroducing screen/model policy into `ft8_main`.

## 5. C3 — COMPLETE — ADV runtime external applications

Runtime external ELF loading is an active architecture target, not a future experiment.

ADV application resolution is fixed as:

```text
1. compiled-in application
2. /flash/<app>.elf
3. /sd/<app>.elf
```

The same external ELF must work unchanged from `/flash` or `/sd`. If both external copies exist, `/flash` wins.

For Keyer:

```text
/flash/keyer.elf
/sd/keyer.elf
```

are both valid installations.

`/sd/keyer.elf` is convenient for development/removable distribution; copying that exact binary to `/flash/keyer.elf` must work without rebuilding it.

Loader details remain resident/private to MiniShell and the ADV backend. External ELF work does not freeze a long-term cross-release binary ABI yet.

The loader proof uses a non-colliding name such as `elfhello.elf` because compiled-in `hello` intentionally has higher resolution priority.

## 6. C4 — COMPLETE — configuration ownership and namespace

Canonical rule:

```text
/flash/config.txt
    MiniShell-owned resident/platform configuration

/flash/<app>/setting.txt
    application-owned configuration and deployment settings
```

The full rule is recorded in:

```text
docs/architecture/configuration.md
```

### MiniShell configuration

`/flash/config.txt` is only for things MiniShell itself needs to operate/adapt the platform, for example:

```text
debug UART GPIO assignment
RTC/GPS physical assignment
GPS baud/detection policy
resident resource sharing/detection/arbitration
```

MiniShell configuration must not contain Keyer/FT8/domain meaning.

### Application configuration

Applications own the meaning and policy of `/flash/<app>/setting.txt`.

Application settings may be hardware/deployment-specific. This does not violate application portability.

For example Keyer may store:

```text
Dit GPIO
Dah GPIO
KeyOut GPIO(s)
KeyIn/KeyOut mode
WPM
tone
volume
```

Keyer interprets those values and requests generic MiniShell Digital I/O operations. MiniShell may configure/read/write GPIO 13, for example, but it must not know that GPIO 13 is a `dit` input.

```text
Keyer setting
      |
      v
config_service
      |
      v
app_controller
      |
      v
keyin/keyout edge module
      |
      v
MiniShell Digital I/O
      |
      v
generic platform GPIO
```

This is the intended distinction:

> Application code/API boundary is portable; deployment settings may be hardware-specific.

C4 does not add a generic public MiniShell Config service. MiniShell may read its own `/flash/config.txt` internally; applications may use MiniShell Filesystem plus their own configuration modules.

### MiniFT8 transition

The canonical application settings namespace is now `/flash/ft8/setting.txt`, but MiniFT8 currently uses `/flash/ft8/station.txt`.

C4 does not rename that file or change FT8 behavior. The `station.txt` -> `setting.txt` migration is a separate MiniFT8 task. Until then, `station.txt` remains the current implementation path and MiniFT8 still owns its contents.

Keyer will use `/flash/keyer/setting.txt` from its first MiniShell implementation.

## 7. Verification gate — SATISFIED

The code-bearing cleanup C0-C2 passed:

```text
MiniFT8 platform-boundary test          PASS
MiniFT8 dependency/no-side-talk test    PASS
opaque AppController boundary           PASS
ft8_main lifecycle/UI boundary          PASS
Linux build + CTest                     PASS
AS-8 + strict unit tests                PASS
RX-1C through RX-7 reference suite      PASS
RX-7 production decoded UI              PASS
Cardputer ADV ESP-IDF build             PASS
```

C3-C4 are architecture/documentation changes and do not alter runtime behavior.

## 8. Result

The pre-Keyer cleanup gate is closed.

```text
C0 dependency enforcement      COMPLETE
C1 opaque controller           COMPLETE
C2 lifecycle-only main         COMPLETE
C3 runtime ELF direction       COMPLETE
C4 configuration ownership     COMPLETE
```

Next work may proceed into the Keyer plan, beginning with review/finalization of its remaining design questions and then the external-ELF/Digital-I/O implementation stages.
