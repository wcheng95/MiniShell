# Keyer on MiniShell

Status: **Draft for review — K0 architecture gate complete**  
Date: 2026-09-10

## Purpose

Port the useful Keyer portion of Mini-CW into MiniShell as a small, field-usable application and use it as the first practical stress test of runtime application loading on Cardputer ADV.

Target runtime artifact:

```text
keyer.elf
```

Valid ADV installations are:

```text
/flash/keyer.elf
/sd/keyer.elf
```

ADV application resolution is:

```text
1. compiled-in application
2. /flash/<app>.elf
3. /sd/<app>.elf
```

Therefore the same `keyer.elf` binary may be copied from SD to flash and must run unchanged; if both external copies exist, `/flash/keyer.elf` wins.

The Keyer application is portable at the source/API boundary. Board-specific deployment details may live in its application settings.

The prerequisite architecture cleanup is complete in `../project/architecture-cleanup.md`. Canonical configuration ownership is defined in `../architecture/configuration.md`.

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

This validates runtime app loading, timing, Digital I/O, Audio TX, Display/Input, Filesystem/application settings, clean ownership, and later Control/CAT.

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

MiniShell provides generic services only: Digital I/O, Audio, Time, Display, Input, Filesystem, and later generic Control where justified.

### 2.2 Application source must not know the platform

`keyer.elf` source must not include or directly call ESP-IDF, FreeRTOS, M5/Cardputer APIs, board GPIO drivers, USB/UAC implementation APIs, device-specific CAT syntax, Linux/POSIX APIs, or ELF-loader interfaces.

Its installation path is not application logic. The same ELF must work from `/flash/keyer.elf` or `/sd/keyer.elf`.

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

A pure Keyer state machine does not fetch settings or operate platform resources on its own.

## 3. Configuration ownership

Canonical rule: `../architecture/configuration.md`.

### 3.1 MiniShell configuration

Reserved system file:

```text
/flash/config.txt
```

This is MiniShell-owned and contains only resident/platform configuration MiniShell itself needs, for example debug UART GPIO selection, RTC/GPS physical assignment, GPS baud/detection policy, and resident platform resource sharing/detection.

It must not contain `keyer_*`, dit, dah, paddle, KeyOut, WPM, tone, or other application-domain settings.

### 3.2 Keyer configuration

Keyer owns:

```text
/flash/keyer/setting.txt
```

This may contain both portable Keyer behavior and deployment-specific hardware assignments.

Settings to define during review/implementation include:

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

Exact key names and file syntax are not frozen yet.

The important rule is:

> Keyer interprets Keyer settings. MiniShell only receives generic service requests.

Example:

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
    configure/read generic line 13
```

MiniShell does not know that line 13 represents a dit paddle contact.

This allows the same application binary to run with different hardware-specific settings on different MiniShell targets.

## 4. Initial module model

Keep the first version small:

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
    configured key-down/key-up realization through Digital I/O
    or later Control/CAT

sidetone
    portable tone generation/state through MiniShell Audio TX

ui_shell
    Keyer UI/navigation using Keyer-owned types

ui_adapter
    MiniShell Display/Input edge adapter
```

`morse_decoder` may remain a private `keyer_engine` submodule if that improves readability/testing. Do not split merely to create files.

## 5. KeyOut model

KeyOut is a Keyer application abstraction, not a MiniShell service name.

The engine expresses logical key state:

```text
key_down
key_up
```

`app_controller` configures `keyout` from application configuration.

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

For GPIO KeyOut, Keyer settings supply line assignments/electrical mode and `keyout` asks generic MiniShell Digital I/O to configure/drive those lines. MiniShell does not know they key a transmitter.

For later CAT KeyOut, device-specific CAT syntax, transport, inversion, retry, and sequencing remain below a generic MiniShell Control boundary. Do not hide CAT inside Digital I/O.

## 6. KeyIn model

KeyIn interprets generic digital lines as application input.

Initial modes:

```text
Paddle
Paddle-Reverse
Straight Key on first configured input
Straight Key on second configured input
```

KeyIn reads MiniShell Digital I/O states and provides normalized paddle/SK state to `keyer_engine`.

V1 should prefer polling over callbacks/interrupt APIs unless measurement proves polling inadequate.

## 7. Timing model

CW timing belongs to `keyer_engine`.

Use MiniShell monotonic time for deadlines and elapsed-time measurement. Do not port FreeRTOS tick types or `xTaskGetTickCount()` into Keyer.

Initial target:

```text
monotonic_us()
    -> engine timing/deadlines

short periodic application loop
    -> sample KeyIn
    -> advance keyer engine
    -> apply KeyOut state
    -> update sidetone state
```

At 60 WPM a dit is about 20 ms, so approximately 1 ms loop granularity is a reasonable initial ADV target. Measure before adding lower-level scheduled-edge mechanisms.

## 8. Audio model

KeyOut and Audio Out are independent.

```text
keyer_engine
    |
    +--> keyout      transmitter key state
    |
    `--> sidetone    audible CW state
```

MiniShell Audio output may be Speaker or UAC. Keyer selects its endpoint through application settings rather than hardcoding Cardputer speaker behavior.

MiniShell transports PCM and owns stream/backend lifecycle. Keyer owns tone frequency, tone generation, CW timing, volume policy, and tone on/off state.

## 9. Runtime ELF target

Desired ADV user experience:

```text
MiniShell> apps
...
keyer

MiniShell> run keyer
```

Resolution:

```text
1. compiled-in keyer, if one exists
2. /flash/keyer.elf
3. /sd/keyer.elf
```

The current plan does not compile Keyer into ADV, so normal Keyer deployment exercises the external tiers.

This must be a real runtime-loaded application, not a statically linked app renamed `.elf`. The application binary may initially require the matching MiniShell API generation; long-term cross-release ABI compatibility is not required yet.

## 10. Development stages

### K0 — COMPLETE — architecture gate

`../project/architecture-cleanup.md` C0-C4 is complete.

Established before Keyer code:

```text
dependency/no-side-talk enforcement
opaque-controller pattern
lifecycle-only main pattern
ADV runtime external-ELF direction
compiled-in -> /flash -> /sd resolution
configuration ownership/namespace
```

### K1 — ADV runtime ELF proof

Prove the loader with a minimal non-colliding external ELF, for example `elfhello`:

```text
/sd/elfhello.elf
    discover -> load -> call MiniShell API -> return -> unload

copy the exact same binary to:
/flash/elfhello.elf
    discover -> load -> call MiniShell API -> return -> unload

when both external copies exist:
    /flash/elfhello.elf wins
```

Do not use `hello.elf` while `hello` remains compiled in, because compiled-in priority would mask the ELF path.

K1 also verifies:

```text
compiled-in > /flash external > /sd external
```

### K2 — MiniShell Digital I/O V1

Add the smallest generic Digital I/O API demonstrated by Keyer needs.

Likely operations:

```text
open/configure generic line
read
write
close
```

Semantics must cover input pull-up and output modes required by the existing Mini-CW adapter, including active-low/open-drain output where supported.

Do not expose Keyer-specific endpoint names or hard-coded board pins in MiniShell. Develop/test service semantics on Linux/mock first, then implement the ADV provider.

### K3 — minimal portable Keyer engine

Port useful Mini-CW timing/state-machine behavior into a platform-independent engine:

- paddle input;
- Iambic A/B behavior as already validated by Mini-CW;
- straight key;
- WPM;
- key-down/key-up state;
- basic decoded output if the existing decoder can be cleanly reused.

Preserve known Mini-CW behavior during the first port unless regression tests support a deliberate change.

### K4 — GPIO KeyIn/KeyOut on ADV

```text
setting.txt
    -> config_service
    -> app_controller
    -> keyin/keyout
    -> MiniShell Digital I/O
    -> ADV generic GPIO provider
```

Hardware deployment pins come from Keyer settings, not MiniShell source and not `/flash/config.txt`.

### K5 — sidetone through MiniShell Audio TX

Add portable tone generation and an ADV Audio TX provider. Speaker may be first; UAC remains the alternate endpoint under the same MiniShell Audio API.

### K6 — minimal field UI and settings

Implement the smallest practical field UI. Reuse proven Mini-CW behavior where useful without mechanically porting its old architecture.

Hold-Backspace and gestures requiring press/release semantics may remain deferred until MiniShell Input has a justified event model.

### K7 — `keyer.elf` field milestone

Build one `keyer.elf` and validate the exact same binary from:

```text
/sd/keyer.elf
/flash/keyer.elf
```

Validate repeated load/run/exit/unload, external discovery order, paddle at representative WPM, Iambic A/B, straight key, KeyOut cancellation/release safety, GPIO electrical behavior, sidetone timing, configuration persistence/reload, and resource cleanup.

### K8 — Control/CAT KeyOut

After a generic MiniShell Control service is defined/tested, add `KeyOut = CAT` without changing `keyer_engine`. GPIO and CAT keying should be interchangeable below the same logical KeyOut state machine.

## 11. What not to port initially

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

Pure module tests cover Morse timing/state transitions, Iambic modes, straight-key timing/decoder, TX FIFO if included, cancellation, and setting parsing/serialization.

MiniShell boundary tests cover Digital I/O requests/cleanup, Audio TX lifecycle, application resource cleanup, GPIO KeyOut, and later Control KeyOut.

ADV hardware tests cover the full app resolution order, the same ELF from `/flash` and `/sd`, actual paddle input/radio keying, configured electrical behavior, Speaker/UAC behavior, alternate debug UART when USB-C OTG is occupied, and load/exit/reload stability.

## 13. Review questions

Before implementation, finalize:

1. Exact first field milestone: proposed minimum is paddle + SK + GPIO KeyOut + Speaker sidetone.
2. Exact Digital I/O V1 semantics and whether generic numeric line IDs are sufficient across initial targets.
3. Exact `/flash/keyer/setting.txt` syntax and key names.
4. Whether UAC sidetone/output is required before the first field milestone or immediately after Speaker.
5. Whether CAT KeyOut waits until after the first GPIO field milestone; current recommendation is yes.
6. Whether Mini-CW decoded-text/TX-FIFO behavior belongs in K3/K7 or a later increment.
7. ADV ELF loader constraints: relocation types, symbol resolution, memory ownership, failure cleanup, and API-version matching.

Already resolved:

```text
K0 architecture gate                              COMPLETE
MiniShell must not know Keyer semantics           YES
Keyer GPIO assignments may be app settings        YES
canonical Keyer settings path                     /flash/keyer/setting.txt
app resolution                                    compiled-in -> /flash -> /sd
same ELF binary from /flash or /sd                REQUIRED
```

Until the remaining questions are reviewed, this document remains the Keyer porting plan rather than the final implementation specification.
