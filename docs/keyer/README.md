# Keyer on MiniShell

Status: **Draft for review**  
Date: 2026-09-10

## Purpose

Port the useful Keyer portion of Mini-CW into MiniShell as a small, field-usable application and use it as the first practical stress test of runtime application loading on Cardputer ADV.

Target runtime artifact:

```text
/sd/keyer.elf
```

The Keyer application should be portable at the source/API level. Board-specific deployment details may live in its application settings.

The prerequisite architecture cleanup is documented in `../project/architecture-cleanup.md`.

## 1. Goals

The first useful Keyer should exercise real MiniShell services while remaining much smaller than MiniFT8.

It should eventually cover:

```text
paddle / straight-key input
        |
        v
portable keyer engine
        |
        +--> KeyOut: GPIO or radio Control/CAT
        |
        `--> sidetone/audio: Speaker or UAC
```

This makes Keyer a useful validation application for:

- runtime app loading;
- timing;
- Digital I/O;
- Audio TX;
- future Control/CAT;
- Display/Input;
- Filesystem/application settings;
- clean application ownership and no-side-talk rules.

## 2. Hard boundaries

### 2.1 MiniShell must not know Keyer concepts

MiniShell must not contain knowledge of:

```text
dit
dah
paddle
straight key
iambic A/B
CW KeyOut
Morse timing
Keyer settings
```

MiniShell provides generic services only.

Examples:

```text
Digital I/O
Audio
Time
Display
Input
Filesystem
future Control
```

### 2.2 Application source must not know the platform

`keyer.elf` source must not include or directly call:

- ESP-IDF;
- FreeRTOS;
- M5/Cardputer APIs;
- board GPIO drivers;
- USB/UAC implementation APIs;
- device-specific CAT syntax;
- Linux/POSIX APIs.

The application sees MiniShell public interfaces only.

### 2.3 No side talk

`app_controller` coordinates Keyer modules.

```text
config_service ----X----> keyout
config_service ----X----> keyin
ui_shell       ----X----> config_service
keyer_engine   ----X----> MiniShell GPIO/CAT directly

config_service
      |
      v
app_controller
   /    |     \
  v     v      v
keyin keyout keyer_engine
```

A pure Keyer state machine should not fetch settings or operate platform resources on its own.

## 3. Configuration ownership

### 3.1 MiniShell configuration

Reserved system file:

```text
/flash/config.txt
```

This is MiniShell-owned and contains only resident/platform configuration MiniShell itself needs, for example:

- debug UART GPIO selection;
- RTC/GPS GPIO selection;
- GPS baud/detection policy;
- resident platform resource-sharing policy.

It must not contain `keyer_*`, dit, dah, paddle, KeyOut, or other application-domain settings.

### 3.2 Keyer configuration

Keyer owns:

```text
/flash/keyer/setting.txt
```

This may contain both portable Keyer behavior and deployment-specific hardware assignments.

Examples of settings to define later:

```text
KeyIn type
Dit input GPIO
Dah input GPIO
KeyOut backend: GPIO | CAT | OFF
KeyOut GPIO assignment/mode when GPIO is selected
Control endpoint when CAT is selected
WPM
Iambic mode
tone frequency
volume
Audio Out endpoint: Speaker | UAC
TX delay
Tune timeout
```

Exact key names and file syntax are intentionally not frozen yet.

The important rule is:

> Keyer interprets Keyer settings. MiniShell only receives generic service requests.

For example:

```text
/flash/keyer/setting.txt
    dit_gpio = 13
        |
        v
config_service
        |
        v
app_controller
        |
        v
keyin
        |
        v
MiniShell Digital I/O
    open line 13 as input/pull-up
```

MiniShell does not know that line 13 represents a dit paddle contact.

This allows the same application binary to run with different hardware-specific settings on different MiniShell targets.

## 4. Initial module model

Keep the first version small. Suggested logical modules:

```text
keyer_main
    foreground lifecycle/wiring only

app_controller
    cross-module coordination and settings distribution

config_service
    parse/default/serialize /flash/keyer/setting.txt

keyer_engine
    portable CW timing/state machine

keyin
    configured paddle/SK sampling through MiniShell Digital I/O

keyout
    configured key-down/key-up realization through
        Digital I/O
        or future Control/CAT

sidetone
    portable tone generation/state driven through MiniShell Audio TX

ui_shell
    Keyer UI/navigation using Keyer-owned UiModel/AppAction types

ui_adapter
    MiniShell Display/Input edge adapter
```

`morse_decoder` may be kept as a private submodule of `keyer_engine` if separating it improves readability and testing. Do not split merely to create more files.

## 5. KeyOut model

KeyOut is a Keyer application abstraction, not a MiniShell service name.

The Keyer engine should express only logical key state:

```text
key_down
key_up
```

`app_controller` configures `keyout` from `config_service`.

```text
config_service
      |
      v
app_controller
      |
      v
keyout
   |        \
   v         v
GPIO      Control/CAT
   |         |
   v         v
MiniShell generic services
```

### GPIO KeyOut

The Keyer setting supplies the required line assignment/electrical mode. `keyout` asks MiniShell Digital I/O to configure and drive those generic lines.

MiniShell does not know that the lines key a transmitter.

### CAT KeyOut

When a future MiniShell Control service exists, `keyout` may realize key-down/key-up through generic Control operations.

Device-specific CAT syntax, transport, inversion, retry, and device sequencing remain below the MiniShell Control boundary.

Do not hide CAT inside Digital I/O.

## 6. KeyIn model

KeyIn is an application interpretation of generic digital lines.

The first implementation should support the useful Mini-CW input modes without importing board-specific code:

```text
Paddle
Paddle-Reverse
Straight Key on first configured input
Straight Key on second configured input
```

Exact display names may remain compatible with Mini-CW later, but internal design should use clear application-owned enums.

KeyIn reads generic MiniShell Digital I/O states and provides normalized paddle/SK state to `keyer_engine`.

For V1, polling is preferred over callbacks/interrupt APIs unless measurement proves polling inadequate.

## 7. Timing model

CW timing belongs to `keyer_engine`.

Use MiniShell monotonic time for deadlines and elapsed-time measurement.

Do not port FreeRTOS tick types or `xTaskGetTickCount()` into Keyer.

Initial design target:

```text
monotonic_us()
    -> engine timing/deadlines

short periodic application loop
    -> sample KeyIn
    -> advance keyer engine
    -> apply KeyOut state
    -> update sidetone state
```

At 60 WPM a dit is about 20 ms, so approximately 1 ms loop granularity should be a reasonable initial target on ADV. Measure before adding lower-level scheduled-edge mechanisms.

Do not add interrupt-safe/reentrant/async machinery speculatively.

## 8. Audio model

KeyOut and Audio Out are independent.

```text
keyer_engine
    |
    +--> keyout      transmitter key state
    |
    `--> sidetone    audible CW state
```

MiniShell Audio output may be backed by:

```text
Speaker
UAC
```

Keyer should select/configure an Audio endpoint through its application settings rather than hardcode Cardputer speaker behavior.

MiniShell transports PCM and owns stream lifecycle/backend conversion. Keyer owns tone frequency, tone generation, CW timing, volume policy, and when the tone should be on/off.

## 9. Runtime ELF target

The desired ADV user experience is:

```text
MiniShell> apps
...
keyer

MiniShell> run keyer
```

with the application stored as:

```text
/sd/keyer.elf
```

This is intended to be a real runtime-loaded application, not a statically linked app renamed `.elf`.

The loader/container implementation is platform-specific and belongs below the application source boundary.

The application binary may initially require the matching MiniShell build/API version. Long-term cross-release ABI compatibility is not a requirement for this first implementation.

## 10. Development stages

### K0 — Architecture gate

Complete `../project/architecture-cleanup.md` and freeze the no-side-talk/dependency rules before new Keyer code is added.

Exit criteria:

- MiniShell/MiniFT8 tests green;
- architecture dependency checker in place;
- cleanup review finalized.

### K1 — ADV runtime ELF proof

Prove the runtime-loading mechanism with a minimal external ELF before depending on Keyer complexity.

Target:

```text
/sd/hello.elf
    discover
    load
    call MiniShell public API
    return
    unload
```

This is loader validation only.

### K2 — MiniShell Digital I/O V1

Add the smallest generic Digital I/O API demonstrated by Keyer needs.

Likely V1 operations:

```text
open/configure generic line
read
write
close
```

Required semantics should cover input pull-up and output modes needed by the existing Mini-CW adapter, including active-low/open-drain output where supported.

Do not expose Keyer-specific endpoint names or hard-coded board pins.

Develop/test service semantics on Linux/mock first, then implement ADV provider.

### K3 — Minimal portable Keyer engine

Port the useful timing/state-machine behavior from Mini-CW into a platform-independent engine.

Initial behavior:

- paddle input;
- Iambic A/B behavior as already validated by Mini-CW;
- straight key;
- WPM;
- key-down/key-up state;
- basic decoded character output if the existing decoder can be cleanly reused.

Preserve known Mini-CW behavior during the first port, including any deliberate compatibility quirks, unless a regression test supports changing it.

### K4 — GPIO KeyIn/KeyOut on ADV

Wire:

```text
setting.txt
    -> config_service
    -> app_controller
    -> keyin/keyout
    -> MiniShell Digital I/O
    -> ADV GPIO provider
```

Hardware deployment pins come from Keyer settings, not MiniShell source and not `/flash/config.txt`.

This stage should permit real paddle-to-radio GPIO keying.

### K5 — Sidetone through MiniShell Audio TX

Add portable tone generation and an ADV Audio TX provider suitable for the selected endpoint.

First useful target may be Speaker; UAC output remains part of the same generic Audio API and should be added/validated without changing Keyer semantics.

Do not create `speaker_*` Keyer APIs.

### K6 — Minimal field UI and settings

Implement the smallest practical display/input UI required for field use.

Reuse proven Mini-CW behavior where useful, but do not mechanically port its old architecture.

Hold-Backspace and other input gestures that require press/release semantics may remain deferred until MiniShell Input has a justified event model for them.

### K7 — `keyer.elf` field milestone

Build and run the actual application from SD:

```text
/sd/keyer.elf
```

Field validation should include at least:

- repeated load/run/exit/unload;
- paddle at representative slow/normal/fast WPM;
- Iambic A and B;
- straight key;
- KeyOut cancellation/release safety;
- GPIO output electrical behavior;
- sidetone timing relative to KeyOut;
- configuration persistence/reload;
- resource cleanup after application exit.

### K8 — Control/CAT KeyOut

After a generic MiniShell Control service is defined and tested, add the alternate KeyOut realization:

```text
KeyOut = CAT
```

`keyer_engine` must not change for this stage. Only controller/configuration and `keyout` realization should change.

This is an important architecture test: GPIO and CAT keying should be interchangeable beneath the same logical KeyOut state machine.

## 11. What not to port initially

Do not make the first Keyer milestone depend on the entire Mini-CW application.

Defer unless needed for field usefulness:

- Lessons mode;
- Words mode;
- Calls mode;
- Plain mode;
- OP lookup/database;
- elaborate memory/message overlay;
- every historical settings page;
- advanced long-press keyboard gestures;
- unrelated system settings.

The first target is a small, reliable CW keyer that validates MiniShell in real use.

## 12. Test strategy

Use three layers.

### Pure module tests

Test without MiniShell/platform behavior:

- Morse timing/state transitions;
- Iambic modes;
- straight-key timing/decoder;
- TX FIFO if included;
- cancellation;
- setting parsing/serialization.

### MiniShell boundary tests

Use mocks/providers to test:

- Digital I/O requests and cleanup;
- Audio TX lifecycle;
- application resource cleanup;
- KeyOut GPIO realization;
- later KeyOut Control realization.

### Hardware/field tests

ADV hardware confirms:

- actual paddle input;
- actual radio keying;
- active-low/open-drain behavior where configured;
- Speaker/UAC audio behavior;
- USB-C OTG operation with alternate debug UART;
- runtime load/exit/reload stability.

## 13. Review questions

Before implementation, finalize:

1. Exact scope of the first field-usable Keyer milestone: paddle + SK + GPIO KeyOut + Speaker sidetone is the proposed minimum.
2. Exact Digital I/O V1 semantics and whether generic numeric line IDs are sufficient across the initial targets.
3. Exact `/flash/keyer/setting.txt` syntax and names.
4. Whether UAC sidetone/output is required before the first field milestone or immediately after Speaker.
5. Whether CAT KeyOut waits until after the first GPIO field milestone; current recommendation is yes.
6. Whether the existing Mini-CW decoded-text/FIFO behavior is part of K3/K7 or deferred to a later Keyer increment.
7. ELF loader constraints for ADV: relocation types, symbol resolution, memory ownership, failure cleanup, and API-version matching.

Until those are reviewed, this document is the porting plan rather than the final implementation specification.
