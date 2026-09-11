# Keyer on MiniShell

Status: **K0/K1/K2 complete; K3 next**  
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

`morse_decoder` may remain a private `keyer_engine` submodule if that improves readability/testing. Do not split merely to create files.

## 4. KeyIn / KeyOut model

KeyIn interprets generic digital lines as application input. Initial modes:

```text
Paddle
Paddle-Reverse
Straight Key on first configured input
Straight Key on second configured input
```

KeyOut is a Keyer application abstraction, not a MiniShell service. The engine emits logical:

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

Use MiniShell monotonic time for deadlines and elapsed-time measurement. Do not port FreeRTOS tick types or `xTaskGetTickCount()` into Keyer.

Initial target:

```text
monotonic_us()
    -> engine timing/deadlines

short periodic application loop
    -> sample KeyIn
    -> advance keyer_engine
    -> apply KeyOut state
    -> update sidetone state
```

At 60 WPM a dit is about 20 ms, so approximately 1 ms loop granularity is a reasonable initial target. Measure before adding interrupt or scheduled-edge mechanisms.

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

Linux supplies a deterministic virtual provider for portable testing. ADV supplies a generic ESP32-S3 GPIO provider and reserves resident MiniShell-owned shared I2C/keyboard and SD-bus lines.

Verification:

```text
Linux build + CTest                 PASS
Digital I/O integration/lifecycle  PASS
strict unit suite                   PASS
AS-8                               PASS
ADV registry                       PASS
ADV firmware build                 PASS
ADV runtime-ELF build              PASS
```

Physical paddle and radio-keying behavior is intentionally deferred to K4, where Keyer supplies real deployment GPIO assignments.

### K3 — NEXT — minimal portable Keyer engine

Port useful Mini-CW timing/state-machine behavior into a platform-independent engine:

- paddle input state;
- Iambic A/B behavior already validated by Mini-CW;
- straight key;
- WPM;
- logical key-down/key-up state;
- basic decoded output only if it can be reused cleanly without complicating the engine.

K3 must run as pure host tests with no Digital I/O, Audio, Display, Input, Filesystem, ESP-IDF, or FreeRTOS dependency.

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

K4 is where actual paddle electrical behavior and KeyOut safety/release behavior are validated on hardware.

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
K3 portable Keyer engine                          NEXT
MiniShell must not know Keyer semantics           YES
Keyer GPIO assignments may be app settings        YES
canonical Keyer settings path                     /flash/keyer/setting.txt
app resolution                                    compiled-in -> /flash/apps -> /sd/apps
same ELF binary from /flash/apps or /sd/apps      REQUIRED
Digital I/O IDs                                   generic numeric line IDs
Digital I/O initial modes                         input / pullup / output / open-drain
Digital I/O V1 IRQ/callback                       NO
CAT KeyOut                                        after GPIO milestone
ADV ELF executable-memory requirement             MEMPROT_FEATURE off on IDF 5.5.1
```
