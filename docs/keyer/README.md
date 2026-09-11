# Keyer on MiniShell

Status: **K0/K1/K2/K3 complete; K4 implemented, hardware validation pending**  
Date: 2026-09-10

## Purpose

Port the useful Keyer portion of Mini-CW into MiniShell as a small, field-usable application and use it as the first practical external application on Cardputer ADV.

Target runtime artifact:

```text
keyer.elf
```

Valid ADV installations:

```text
/flash/apps/keyer.elf
/sd/apps/keyer.elf
```

ADV resolution:

```text
1. compiled-in application
2. /flash/apps/<app>.elf
3. /sd/apps/<app>.elf
```

The same external ELF must run unchanged from flash or SD.

## Hard boundaries

MiniShell must not know Keyer concepts such as dit, dah, paddle, straight key, Iambic mode, KeyOut, or Morse timing. MiniShell exposes only generic services.

Keyer source must not call ESP-IDF, FreeRTOS, board drivers, POSIX, or the ELF loader directly.

Cross-module orchestration belongs only in `app_controller`:

```text
config_service
      |
      v
app_controller
   /      |       \
  v       v        v
keyin  keyer_engine keyout
  |                  |
  v                  v
MiniShell Digital I/O
```

The dependency checker now enforces this Keyer module boundary in CI.

## Configuration ownership

MiniShell owns:

```text
/flash/config.txt
```

Keyer owns:

```text
/flash/keyer/setting.txt
```

K4 reads the Keyer file if present and otherwise uses safe defaults. K6 will add the settings UI/persistence workflow.

K4 accepted fields:

```text
wpm=20
key_in=Paddle|PaddleR|SK-T|SK-R
paddle=IambicA|IambicB|Bug
key_out=Paddle|PaddleR|SK|SK-M|Off
key_in_tip_gpio=13
key_in_ring_gpio=15
key_out_tip_gpio=3
key_out_ring_gpio=6
```

## Module model

```text
keyer_main
    lifecycle only

app_controller
    cross-module coordination

config_service
    defaults + /flash/keyer/setting.txt parsing

keyer_engine
    portable CW timing/state machine

keyin
    MiniShell Digital I/O -> logical dit/dah/straight

keyout
    logical key state -> MiniShell Digital I/O

sidetone
    K5

ui_shell / ui_adapter
    K6
```

## Timing model

`keyer_engine` accepts explicit monotonic `now_us`; it owns no clock.

Runtime:

```text
MiniShell monotonic_us()
    -> app_controller
    -> keyer_engine_step()
    -> logical key state/events
```

The K4 loop samples and advances at approximately 1 ms granularity.

## Runtime ELF policy

ADV runtime apps are trusted code. ESP-IDF 5.5.1 uses `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE`; MiniShell disables it so the ELF loader can allocate executable SRAM.

The resident loader intentionally exports only:

```text
mini_api_get
```

K4's Keyer ELF is therefore self-contained for C-runtime helpers. CI requires exactly one external `R_XTENSA_JMP_SLOT` import: `mini_api_get`. Do not broaden the resident export table merely to satisfy application libc calls.

## Development stages

### K0 — COMPLETE — architecture gate

Established the dependency/no-side-talk model, lifecycle-only main pattern, runtime-ELF direction, application resolution, and configuration ownership.

### K1 — COMPLETE — ADV runtime ELF proof

Hardware validated `elfhello.elf` from both:

```text
/sd/apps/elfhello.elf
/flash/apps/elfhello.elf
```

Proved discovery, relocation, `mini_api_get()`, MiniShell Console API use, clean return/unload, repeated execution, and flash-over-SD precedence.

### K2 — COMPLETE — MiniShell Digital I/O V1

Canonical contract: `docs/api/digital-io-api.md`.

V1:

```text
open / read / write / close
input
input + pull-up
output
open-drain output
numeric line IDs
opaque handles
initial output level at open
duplicate-open rejection
app-exit cleanup
```

ADV application-facing GPIO allow-list:

```text
G1 G2 G3 G4 G5 G6 G13 G15
```

### K3 — COMPLETE — portable Keyer engine

Implemented under:

```text
apps/keyer/src/keyer_engine/
```

Pure C behavior includes:

```text
5-60 WPM
1-unit dit / 3-unit dah
1-unit inter-element gap
3-unit character completion after element end
7-unit word completion after element end
opposite-paddle memory
held-squeeze alternation
Bug automatic dit + manual held dah
straight-key duration classification
Morse decode + Mini-CW special gestures
invalid Morse '~'
```

Mini-CW compatibility: the field-validated June 2026 A/B swap is preserved; the enum/display selection named `Iambic A` carries the squeeze-release extra-element behavior.

Regression: `tests/keyer_engine_k3_test.c`.

### K4 — IN PROGRESS — GPIO KeyIn/KeyOut

Software implementation is present under:

```text
apps/keyer/main/
apps/keyer/include/
apps/keyer/src/app_controller/
apps/keyer/src/config_service/
apps/keyer/src/keyin/
apps/keyer/src/keyout/
platform/adv/elf_apps/keyer/
```

Default deployment:

```text
G13   KeyIn tip   active-low input + pull-up
G15   KeyIn ring  active-low input + pull-up
G3    KeyOut tip  active-low open-drain
G6    KeyOut ring active-low open-drain

WPM        20
Paddle     IambicA
KeyIn      Paddle
KeyOut     SK
```

KeyIn mappings:

```text
Paddle     tip=dit, ring=dah
PaddleR    tip=dah, ring=dit
SK-T       tip straight key
SK-R       ring straight key
```

KeyOut mappings:

```text
Paddle     dit->tip, dah->ring
PaddleR    dit->ring, dah->tip
SK         both active during key-down
SK-M       both active during key-down; ring grounded while idle
Off        no output lines opened
```

Safety rule: G3/G6 are opened with initial level `1` so open-drain outputs begin released/high-Z. Normal shutdown explicitly releases both lines before close. `SK-M` also releases both on application exit.

Host tests:

```text
tests/keyer_k4_io_test.c
    KeyIn/KeyOut electrical/logical mapping

tests/keyer_k4_controller_test.c
    default config -> G13 simulated press -> engine -> G3/G6 -> decoded E -> q exit
```

The external build project produces:

```text
platform/adv/elf_apps/keyer/build/keyer.app.elf
```

K4 is **not complete** until real Cardputer ADV hardware validates:

```text
G13/G15 physical paddle input
representative CW/decoded output
G3/G6 active-low keying
G3/G6 released on q/ESC exit
clean return to M$>
```

### K5 — sidetone through MiniShell Audio TX

Add portable tone generation and ADV Audio TX realization without changing Keyer timing ownership.

### K6 — minimal field UI and settings

Add the smallest practical field UI and settings editing/persistence.

### K7 — `keyer.elf` field milestone

Validate one identical `keyer.elf` from `/sd/apps` and `/flash/apps`, including repeated load/run/exit, WPM/modes, GPIO behavior, sidetone, configuration reload, and resource cleanup.

### K8 — Control/CAT KeyOut

After a generic MiniShell Control service exists, add CAT KeyOut without changing `keyer_engine`.

## Current status

```text
K0 architecture gate                 COMPLETE
K1 ADV runtime ELF proof             COMPLETE
K2 MiniShell Digital I/O V1          COMPLETE
K3 portable Keyer engine             COMPLETE
K4 GPIO KeyIn/KeyOut                 HARDWARE VALIDATION PENDING
K5 sidetone                          NEXT AFTER K4

MiniShell knows Keyer semantics      NO
Keyer config path                    /flash/keyer/setting.txt
app resolution                       compiled-in -> /flash/apps -> /sd/apps
ADV application GPIO allow-list      1,2,3,4,5,6,13,15
CAT KeyOut                           after GPIO field milestone
ADV ELF resident import policy       mini_api_get only
```
