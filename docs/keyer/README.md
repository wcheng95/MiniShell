# Keyer on MiniShell

Status: **K0/K1/K2/K3 complete; K4 next**  
Date: 2026-09-10

## Purpose

Port the useful Keyer portion of Mini-CW into MiniShell as a small, field-usable application and use it as the first practical external application on Cardputer ADV.

Target runtime artifact:

```text
keyer.elf
```

Valid ADV installations are:

```text
/flash/apps/keyer.elf
/sd/apps/keyer.elf
```

ADV application resolution is:

```text
1. compiled-in application
2. /flash/apps/<app>.elf
3. /sd/apps/<app>.elf
```

The same `keyer.elf` binary must run unchanged from either external location. If both copies exist, `/flash/apps/keyer.elf` wins.

## 1. Hard boundaries

### MiniShell must not know Keyer concepts

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

### Application source must not know the platform

`keyer.elf` source must not include or directly call ESP-IDF, FreeRTOS, M5/Cardputer APIs, board GPIO drivers, USB/UAC implementation APIs, device-specific CAT syntax, Linux/POSIX APIs, or ELF-loader interfaces.

### No side talk

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

Pure domain/state-machine modules do not fetch settings or operate platform resources directly.

## 2. Configuration ownership

Canonical rule: `../architecture/configuration.md`.

MiniShell owns:

```text
/flash/config.txt
```

Keyer owns:

```text
/flash/keyer/setting.txt
```

Keyer settings may include deployment-specific GPIO assignments. This does not make MiniShell aware of their meaning.

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
    line_id = 13
    input + pull-up
```

MiniShell knows only a generic line request; Keyer knows that the line represents `dit`.

## 3. Initial module model

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

The Morse decoder is currently a private `keyer_engine` submodule. It remains platform-independent and does not talk to sibling application modules.

## 4. KeyIn / KeyOut model

KeyIn interprets generic digital lines as application input. Initial modes:

```text
Paddle
Paddle-Reverse
Straight Key on first configured input
Straight Key on second configured input
```

Paddle reversal is deliberately outside `keyer_engine`: `keyin` maps physical inputs to logical `dit` and `dah` before stepping the engine.

KeyOut is a Keyer application abstraction, not a MiniShell service. The engine exposes logical:

```text
key_down
key_up
```

and `app_controller` sends that state through the configured KeyOut edge module.

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
Digital I/O  Control/CAT
```

GPIO KeyOut comes first. CAT waits until after the GPIO field milestone.

## 5. Timing model

CW timing belongs to `keyer_engine`.

The pure engine accepts an explicit monotonic `now_us` value. It does not call MiniShell, FreeRTOS, ESP-IDF, or a host clock directly. The application controller will obtain MiniShell monotonic time and pass it into the engine.

Runtime model:

```text
MiniShell monotonic_us()
    -> app_controller
    -> keyer_engine_step(now_us, logical inputs)
    -> logical key state/events
```

At 60 WPM a dit is about 20 ms, so approximately 1 ms application-loop granularity remains a reasonable initial ADV target. Measure before adding interrupt or scheduled-edge mechanisms.

## 6. Audio model

KeyOut and Audio Out are independent:

```text
keyer_engine
    |
    +--> keyout      transmitter key state
    |
    `--> sidetone    audible CW state
```

MiniShell Audio transports streams. Keyer owns tone frequency, volume policy, tone generation, and CW timing.

## 7. Runtime ELF target

Desired user experience:

```text
M$> apps
...
keyer

M$> keyer
```

Keyer is not planned as a compiled-in ADV app; normal deployment therefore exercises the runtime ELF path.

The public MiniShell API is still evolving. K2 advanced API generation v2 -> v3, so external applications must currently be rebuilt against the matching MiniShell API generation.

On Cardputer ADV, Espressif's ELF loader relocates executable sections into executable internal SRAM. ESP-IDF 5.5.1 therefore builds MiniShell with `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE` disabled. External ADV apps are trusted code, not sandboxed code.

## 8. Development stages

### K0 — COMPLETE — architecture gate

Established:

```text
dependency/no-side-talk enforcement
opaque-controller pattern
lifecycle-only main pattern
ADV runtime external-ELF direction
compiled-in -> /flash/apps -> /sd/apps resolution
configuration ownership/namespace
```

### K1 — COMPLETE — ADV runtime ELF proof

Hardware validated with `elfhello.elf`:

```text
/sd/apps/elfhello.elf
    discover -> load -> relocate -> mini_api_get()
    -> MiniShell Console API -> return -> unload

same binary:
/flash/apps/elfhello.elf

precedence:
compiled-in > /flash/apps > /sd/apps
```

Repeated execution showed stable executable heap and no observed loader leak.

### K2 — COMPLETE — MiniShell Digital I/O V1

Canonical contract:

```text
docs/api/digital-io-api.md
```

Public operations:

```text
open
read
write
close
```

V1 modes:

```text
input
input + pull-up
output
open-drain output
```

Additional semantics:

```text
numeric generic line IDs
opaque public handles
initial output level supplied at open
duplicate-open rejection
automatic line cleanup at app exit
backend may reject reserved/unavailable lines
no interrupt/callback API in V1
```

Linux supplies a deterministic virtual provider for portable testing. ADV supplies a generic ESP32-S3 GPIO provider with an application-facing allow-list:

```text
G1 G2 G3 G4 G5 G6 G13 G15
```

This keeps resident LCD, keyboard/I2C, SD, audio, and other built-in resources outside application Digital I/O ownership.

Physical paddle and radio-keying behavior is intentionally deferred to K4, where Keyer supplies real deployment GPIO assignments.

### K3 — COMPLETE — portable Keyer engine

Implemented under:

```text
apps/keyer/src/keyer_engine/
    keyer_engine.c/.h
    keyer_decoder.c/.h
```

The engine is pure C domain logic. It has no MiniShell, Digital I/O, Audio, Display, Filesystem, ESP-IDF, FreeRTOS, POSIX, or board dependency.

Current behavior:

```text
WPM range                     5-60
CW unit                       1200 / WPM ms
Dit                           1 unit
Dah                           3 units
Inter-element gap             1 unit
Character completion          3 units after element end
Word-space completion         7 units after element end
Paddle memory                 opposite paddle remembered
Held squeeze                  alternating dit/dah
Bug                           automatic dit + manual held dah
Straight key                  <=2 units dit, >2 units dah
Logical output                key_down/key_up state
Decoder                       letters/digits/basic punctuation
Invalid Morse                 '~'
```

The Mini-CW decoder's existing special input gestures are preserved:

```text
6-12 consecutive dits   backspace
.-..-.                  enter
----                    explicit space
```

Mini-CW compatibility note: its June 2026 hardware fix intentionally made the enum/display selection named `Iambic A` carry the squeeze-release extra-element behavior. K3 preserves that observed field behavior rather than silently renaming or reversing the modes during the first port. A future semantic cleanup would require an explicit migration and regression change.

K3 regression coverage in `tests/keyer_engine_k3_test.c` includes:

```text
WPM clamp and timing
single-dit key state and decode timing
held-dit repeat
held-squeeze alternation
opposite-paddle memory
Mini-CW Iambic A/B compatibility
straight-key dit/dah classification
Bug manual dah behavior
Morse decode + Mini-CW special gestures
invalid-Morse behavior
```

Verification:

```text
Linux normal build/CTest        PASS
AS-8                            PASS
strict unit build               PASS
keyer_engine_k3_unit            PASS
FT8 reference regression        PASS
```

K3 deliberately does not connect to physical GPIO. That boundary belongs to K4.

### K4 — NEXT — GPIO KeyIn/KeyOut on ADV

```text
setting.txt
    -> config_service
    -> app_controller
    -> keyin/keyout
    -> MiniShell Digital I/O
    -> ADV generic GPIO provider
```

Hardware deployment pins come from Keyer settings, not MiniShell source and not `/flash/config.txt`.

K4 is where actual paddle electrical behavior and KeyOut safety/release behavior are validated on hardware.

Initial ADV deployment may use:

```text
Dit       G13  input + pull-up
Dah       G15  input + pull-up
KeyOut    G3   open-drain, released at open
KeyOut 2  G6   open-drain, released at open
```

These meanings remain Keyer-owned settings; MiniShell sees only generic line IDs/modes/levels.

### K5 — sidetone through MiniShell Audio TX

Add portable tone generation and an ADV Audio TX provider. Speaker may be first; UAC remains an alternate endpoint under the same MiniShell Audio API.

### K6 — minimal field UI and settings

Implement the smallest practical field UI and `/flash/keyer/setting.txt` handling. Reuse proven Mini-CW behavior where useful without mechanically porting its old architecture.

### K7 — `keyer.elf` field milestone

Build one `keyer.elf` and validate the exact same binary from:

```text
/sd/apps/keyer.elf
/flash/apps/keyer.elf
```

Validate repeated load/run/exit/unload, discovery order, representative WPM, Iambic A/B, straight key, KeyOut cancellation/release safety, GPIO electrical behavior, sidetone timing, configuration persistence/reload, and resource cleanup.

### K8 — Control/CAT KeyOut

After a generic MiniShell Control service is defined/tested, add `KeyOut = CAT` without changing `keyer_engine`.

## 9. Deferred scope

Do not initially port:

- Lessons mode;
- Words mode;
- Calls mode;
- Plain mode;
- OP lookup/database;
- elaborate memory/message overlay;
- every historical settings page;
- advanced long-press keyboard gestures;
- unrelated system settings.

The first target is a small, reliable field CW keyer that validates MiniShell in real use.

## 10. Current resolved decisions

```text
K0 architecture gate                              COMPLETE
K1 ADV runtime ELF proof                          COMPLETE
K2 MiniShell Digital I/O V1                       COMPLETE
K3 portable Keyer engine                          COMPLETE
K4 GPIO KeyIn/KeyOut                              NEXT
MiniShell must not know Keyer semantics           YES
Keyer GPIO assignments may be app settings        YES
canonical Keyer settings path                     /flash/keyer/setting.txt
app resolution                                    compiled-in -> /flash/apps -> /sd/apps
same ELF binary from /flash/apps or /sd/apps      REQUIRED
Digital I/O IDs                                   generic numeric line IDs
Digital I/O initial modes                         input / pullup / output / open-drain
ADV application GPIO allow-list                   1,2,3,4,5,6,13,15
Digital I/O V1 IRQ/callback                       NO
CAT KeyOut                                        after GPIO milestone
ADV ELF executable-memory requirement             MEMPROT_FEATURE off on IDF 5.5.1
```
