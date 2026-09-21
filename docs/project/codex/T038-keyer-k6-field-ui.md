# T038 — Keyer K6 field UI, keyboard TX, memories, and persistence

Status: TESTING

## Architect intent

Advance the external MiniShell Keyer from the working K5 console/engine state to
the first practical field UI.

Use Mini-CW's Keyer operating screen and settings concepts as the behavioral
reference, but simplify them for MiniShell:

- one normal 20x7 Keyer operating screen;
- one Operation screen entered with the physical Opt key;
- three Operation pages;
- ordinary printable keyboard characters remain available for CW text entry;
- Ctrl+C quits the Keyer;
- all exposed settings persist in `/flash/keyer/setting.txt`;
- keyboard/M1-M5 transmission uses a separate portable TX state machine rather
  than changing the proven K5 paddle/straight-key engine.

This task also corrects the earlier KeyOut model. KeyOut is straight-key only:

```text
SKS   straight-key stereo
SKM   straight-key mono
OFF   disabled
```

There are no Paddle/PaddleR KeyOut modes in K6.

## Objective

Deliver one ADV external `keyer.elf` that preserves the validated K5 physical
keying/sidetone behavior and adds:

1. a dedicated 20x7 Keyer UI;
2. UTC clock/status top row;
3. decoded-history display;
4. keyboard CW transmit FIFO;
5. M1-M5 message memories;
6. Tune and cancel controls;
7. three-page Operation/settings UI;
8. immediate persistent settings;
9. renamed/corrected KeyOut modes SKS/SKM/OFF.

K7 remains the final broad field-acceptance milestone after K6 hardware review.

## Current context

Current MiniShell `main` baseline when this task is created:

```text
eb7ef34af93ec7141eaf9abddabee185d596978c
```

Current Keyer status:

```text
K0 architecture gate          COMPLETE
K1 ADV runtime ELF proof      COMPLETE
K2 MiniShell Digital I/O      COMPLETE
K3 portable Keyer engine      COMPLETE
K4 GPIO KeyIn/KeyOut          COMPLETE
K5 sidetone                   IMPLEMENTED / TRANSPORT HARDWARE-VALIDATED
K6 field UI/settings          THIS TASK
```

Real ADV hardware already validates:

- runtime external `keyer.elf`;
- G13/G15 KeyIn;
- G3/G6 KeyOut;
- active-low/pull-up/open-drain behavior;
- K3 Morse timing/decoding;
- K5 sidetone transport;
- decoded text through Console;
- clean exit and output release.

Do not re-solve those layers.

## Source of truth

Follow, in priority order:

1. this T038 task for K6 decisions;
2. current MiniShell Keyer architecture:
   - `docs/keyer/README.md`
   - `apps/keyer/**`
3. Mini-CW Keyer behavior/reference implementation:
   - repository `wcheng95/Mini-CW`
   - `components/ui_service/ui_service.c`
   - `components/keyer_service/keyer_service.c`
   - `components/keyer_service/include/keyer_service.h`
   - Mini-CW README Keyer user manual;
4. MiniShell public API v3 in `include/minishell/api.h`.

Reuse Mini-CW behavior where this task does not override it, but do not import
Mini-CW platform/HAL dependencies.

## Architectural constraints

### Existing boundaries remain

MiniShell resident code must still know nothing about Keyer concepts.

Keyer uses only public MiniShell APIs:

```text
Display
Input
Filesystem
Time/Location
Audio TX
Digital I/O
Console/System only for diagnostics
```

No ESP-IDF, FreeRTOS, Cardputer, GPIO driver, or board-specific calls in
`apps/keyer/**`.

No public MiniShell API change is allowed.

The external ELF must retain the existing import policy: only `mini_api_get`
from the resident image.

### Preserve K5 engine ownership

The existing `keyer_engine` remains the physical paddle/straight-key timing and
decode engine. Do not fold keyboard TX into it.

Add a separate portable keyboard/message TX state machine, conceptually:

```text
physical paddle/SK
    -> keyin
    -> keyer_engine
    -> arbitration
    -> keyout + sidetone
    -> decoded history

keyboard / M1-M5
    -> tx_fifo / Morse TX state machine
    -> arbitration
    -> keyout + sidetone
```

Physical KeyIn always has priority. A physical paddle or straight-key press
cancels active/queued keyboard/message TX and returns control immediately to the
manual keyer path.

### Nonblocking timing

Keyboard/message TX must be driven by explicit MiniShell monotonic time. It must
not block the controller loop for dit/dah/gap durations.

Use standard timing:

```text
dit               1 unit
dah               3 units
inter-element gap 1 unit
inter-character   3 units
word gap          7 units
```

Use current WPM (5..60) for keyboard/message TX.

Conceptually port Mini-CW's `IDLE/GAP/ELEMENT` scheduler, but keep the MiniShell
implementation portable and separated from physical-input decoding.

## K6 normal screen

The Keyer takes MiniShell Display ownership and renders exactly 20 columns x 7
rows.

### Top row

Fixed exact 20-character format:

```text
HH:MM KIN KOUT WW Vnn
```

UTC only.

Examples:

```text
19:33 Pdl SKS 20 V80
07:15 PdR SKM 25 V65
12:04 SkT OFF 18 V40
```

Field widths:

```text
HH:MM   5
space   1
KIN     3
space   1
KOUT    3
space   1
WW      2
space   1
Vnn     3
total  20
```

KeyIn labels:

```text
Pdl   Paddle
PdR   Paddle reverse
SkT   straight key on tip
SkR   straight key on ring
```

KeyOut labels:

```text
SKS   straight-key stereo
SKM   straight-key mono
OFF   disabled
```

WPM is always two decimal digits in the supported 5..60 range.

Volume is `V00`..`V99`.

If UTC is temporarily unavailable, render a stable 5-character placeholder such
as `--:--`; do not fail the Keyer.

### Rows 1-5

Five rows of decoded physical KeyIn history.

Follow the latest decoded text automatically. Use a bounded history buffer.
Mini-CW's 64 x 20 history is an acceptable reference size.

No decoded-history manual scrolling is required in first K6.

Decoded events retain K3 semantics:

- characters;
- word space;
- Backspace gesture;
- Enter gesture;
- invalid Morse `~`.

Do not print decoded text to the normal MiniShell Console while the dedicated
screen owns Display; Console/System output is reserved for diagnostics/errors.

### Row 6

Show the remaining unsent keyboard/message TX FIFO tail.

Temporary operating status may replace row 6 briefly, for example:

```text
Mute:ON
Tune
Tune:Hold
Saved
TX full
Unsupported char
```

After temporary status expires, return to the TX FIFO tail.

## KeyOut correction

Replace the old K4/K5 KeyOut model:

```text
Paddle
PaddleR
SK
SK-M
Off
```

with only:

```text
SKS
SKM
OFF
```

Electrical behavior:

### SKS

```text
idle:
  G3 tip   released/high-Z
  G6 ring  released/high-Z

key down:
  G3 tip   low
  G6 ring  low
```

### SKM

Mono means tip is the key and ring is ground.

```text
while Keyer app is running:
  G6 ring  low continuously

idle:
  G3 tip   released/high-Z

key down:
  G3 tip   low
```

On application shutdown/error cleanup, both G3 and G6 must be released/high-Z.

### OFF

No KeyOut lines are driven/opened for application keying.

### Configuration migration

Canonical new persisted values:

```text
key_out=SKS
key_out=SKM
key_out=Off
```

For compatibility with existing deployed files, accept legacy:

```text
key_out=SK    -> SKS
key_out=SK-M  -> SKM
```

Do not expose or persist legacy Paddle/PaddleR KeyOut modes. A legacy file that
uses those mistaken modes may be treated as invalid rather than silently changing
its meaning.

Default KeyOut becomes SKS.

## Keyboard event policy

Ordinary characters are CW text, not UI commands.

### Global operating shortcuts

```text
Opt          enter/leave Operation
Ctrl+C       quit Keyer

[            WPM -1
]            WPM +1
{            volume -5
}            volume +5

Alt+1..5     queue/send M1..M5
Tab          Tune mode
`            cancel active/queued keyboard/message TX
Enter        start pending TX immediately
Backspace    remove one editable unsent FIFO character
```

WPM shortcut limits: 5..60.

Volume shortcut limits: 0..99. Clamp, do not wrap.

WPM/volume changes made by shortcuts persist immediately.

Bare printable `q`, `o`, digits, letters and punctuation remain available to
the TX text path. Do not use Q/O for UI or exit.

Ctrl+C means character `c`/`C` with `MINI_MOD_CTRL`; it must not append C to
the TX FIFO.

### Operation page navigation

The ADV keyboard generates arrow special events with `MINI_MOD_FN` for
Fn+arrow combinations. In Operation top level:

```text
Fn+Up      previous Operation page
Fn+Down    next Operation page
```

Wrap:

```text
O1 <-> O2 <-> O3
```

Do not rely on public PageUp/PageDown key codes for Cardputer ADV K6.

Fn+` produces Escape and is available as Back/cancel inside Operation/editing.

Opt toggles between the normal Keyer screen and Operation top level.

While editing an item, Escape cancels the edit. Enter commits it. Opt may also
leave Operation only after safely resolving/cancelling the active edit; do not
partially persist edits.

## Operation pages

All pages are six 20-character content rows. The screen may use a compact top
indicator or first row consistent with MiniFT8's paged settings style; content
must remain readable at 20 columns.

### Operation 1 — operating configuration

Required six items:

```text
1 KeyIn:  Pdl
2 KeyOut: SKS
3 Speed:  20
4 Volume: 80
5 Tone:   700
6 Paddle: IambicA
```

Ranges/options:

```text
KeyIn:
  Paddle
  PaddleR
  SK-T
  SK-R

KeyOut:
  SKS
  SKM
  Off

Speed:
  5..60 WPM

Volume:
  0..99

Tone:
  300..999 Hz

Paddle:
  IambicA
  IambicB
  Bug
```

Volume is application-owned sidetone PCM scaling. Do not add a MiniShell Audio
volume API and do not change ADV speaker hardware volume policy.

### Operation 2 — message memories

```text
1 M1: CQ POTA
2 M2:
3 M3:
4 M4:
5 M5:
6 Repeat: 10s
```

M1-M5 maximum stored length: 95 characters each, matching Mini-CW's field
reference.

Text editor:

- printable text;
- Backspace deletes;
- Enter commits;
- Escape cancels;
- never overflow storage;
- committed value persists immediately.

`Repeat` range: 1..99 seconds.

Behavior reference:

- M2-M5 are one-shot;
- M1 may repeat after the configured interval after its TX FIFO drains;
- any ordinary keyboard entry, M2-M5 selection, explicit TX cancel, or physical
  KeyIn cancels pending M1 repeat;
- Alt+1..5 outside Operation queues the selected memory;
- if a memory cannot fit in the TX FIFO, leave the FIFO unchanged and show a
  brief `TX full` status.

### Operation 3 — transmission behavior

```text
1 TxDelay:      1s
2 TuneTimeout: 10s
3 Mute:        OFF
4
5
6
```

Rows 4-6 are intentionally reserved for future K6/K7 growth. Do not fill them
with unrelated options.

Ranges:

```text
TxDelay      0..99 seconds
TuneTimeout  0..20 seconds
Mute         ON/OFF
```

All three persist.

## Keyboard/message TX behavior

### TX FIFO

Use a bounded 511-character FIFO/reference capacity, matching Mini-CW.

Ordinary supported printable characters append to the unsent tail.

Normalize lowercase ASCII letters to uppercase for transmission.

Spaces are valid.

Unsupported characters must not corrupt or wedge the FIFO. Either reject them
at append with a brief status or safely skip them at transmission with a brief
status; test the chosen behavior explicitly.

Use one canonical portable Morse encode table. Reuse/share existing Keyer decoder
knowledge where clean, or port the equivalent Mini-CW pattern table without
platform dependencies.

### TxDelay

When text first becomes pending and TX is idle, begin transmission after
`TxDelay` seconds of keyboard-input inactivity.

Each newly appended ordinary keyboard character resets the pending TxDelay.

Enter bypasses the remaining delay and starts immediately.

`TxDelay=0` starts as soon as text is queued.

Message memories follow the same TX path; Alt+M selection may start according to
the same configured delay unless Mini-CW's existing M1-repeat behavior requires
immediate continuation. Keep one deterministic policy and test it.

### Backspace

Backspace may remove only characters that have not begun transmission.

Never edit the currently keyed character or already-sent prefix.

### Cancel

Bare backtick `````` cancels active and queued keyboard/message TX:

- release KeyOut;
- stop sidetone for TX;
- clear TX FIFO/state;
- cancel M1 repeat;
- return to manual KeyIn operation.

### Physical KeyIn priority

A physical paddle/straight-key press while keyboard/message TX is pending or
active performs the same TX cancellation first, then services the physical input.

Manual keying must not be delayed by TxDelay, memory repeat, Tune, or queued text.

## Tune

Tab toggles Tune mode.

Tune uses KeyOut + sidetone ownership without changing the K3 physical decoder.

Reference Mini-CW behavior:

- Tune enters a dedicated operating state;
- output may be held/latched according to the chosen simple implementation;
- `TuneTimeout=0` means no automatic timeout;
- otherwise automatically leave Tune after 1..20 seconds;
- Tab exits Tune;
- a physical KeyIn press exits/cancels Tune and returns control to manual keying;
- Ctrl+C must still safely quit;
- output cleanup must always release KeyOut.

Do not add radio/CAT semantics in K6.

## Sidetone volume and mute

Current K5 sidetone generation remains the source.

Add portable 0..99 volume scaling to the generated S16 PCM.

Requirements:

- `volume=0` produces silence but does not disable KeyOut;
- `mute=On` also suppresses sidetone only, never KeyOut;
- changing volume while the app runs takes effect without reopening Audio TX;
- no overflow/clipping from the scaling arithmetic;
- retain the existing short ramp/click reduction;
- current K5 Audio TX endpoint/lifecycle remains unchanged.

## Persistence

Canonical file remains:

```text
/flash/keyer/setting.txt
```

Every committed setting change persists immediately, including shortcut changes.

K6 known persisted fields:

```text
wpm=20
volume=80
sidetone=On
sidetone_hz=700

key_in=Paddle
key_out=SKS
paddle=IambicA

key_in_tip_gpio=13
key_in_ring_gpio=15
key_out_tip_gpio=3
key_out_ring_gpio=6

m1=CQ POTA
m2=
m3=
m4=
m5=
repeat_s=10

tx_delay_s=1
tune_timeout_s=10
mute=Off
```

Defaults:

```text
wpm=20
volume=80
sidetone=On
sidetone_hz=700
key_in=Paddle
key_out=SKS
paddle=IambicA
M1="CQ POTA"
M2-M5=""
repeat_s=10
tx_delay_s=1
tune_timeout_s=10
mute=Off
GPIO lines unchanged from K5
```

The writer must serialize all current known Keyer settings, including GPIO line
IDs, so saving one UI setting cannot lose deployment wiring.

Use a safe temp-file commit through MiniShell Filesystem APIs. Follow the existing
repository patterns for write -> sync -> close -> rename/commit. A failed save:

- must not corrupt the previous valid settings file;
- must leave the runtime setting applied or explicitly roll it back according to
  one documented/tested policy;
- must show/report the failure;
- must never silently claim persistence.

Unknown/comment preservation is not required for K6; Keyer owns this file and may
rewrite it in canonical form.

On startup:

- missing file -> defaults;
- legacy SK/SK-M aliases migrate in memory;
- valid file -> load all known values;
- invalid/unreadable file retains the existing explicit startup failure behavior
  unless the implementation can preserve a stronger already-tested rule without
  hiding corruption.

## UI module structure

Add portable modules consistent with MiniFT8 boundaries, for example:

```text
apps/keyer/src/ui_shell/
apps/keyer/main/keyer_ui_adapter.c
apps/keyer/src/tx_engine/
```

Exact names may vary, but responsibilities must remain:

```text
ui_shell
    pure screen/input/menu state
    no MiniShell API calls

ui_adapter
    MiniShell Display/Input translation only

tx_engine
    pure monotonic Morse keyboard/message TX scheduling
    no MiniShell API/platform calls

app_controller
    coordinates config/keyin/keyer_engine/tx_engine/keyout/sidetone/ui
```

Do not copy Mini-CW's monolithic UI/service architecture into MiniShell.

## Main-loop responsiveness

The dedicated screen must remain responsive while:

- idle;
- paddle keying;
- keyboard TX;
- M1 repeat;
- Tune;
- editing Operation settings.

No new background task is required. Prefer one deterministic foreground controller
loop driven by monotonic time and nonblocking input polling.

K5 sidetone writes are finite and already transport-tested; do not redesign Audio
TX in this task.

## Non-goals

Do not add:

- trainer modes;
- OP/callsign lookup;
- QSO logging;
- GPS/location UI;
- CAT/Control KeyOut;
- Wi-Fi/BLE;
- WebFS integration;
- a new MiniShell public service;
- adaptive straight-key WPM persistence beyond current K3/K5 behavior unless
  already present and required for correctness;
- decoded-history manual scrolling;
- new KeyOut paddle modes;
- K7 broad field validation inside the implementation task.

Do not change FT8 behavior.

## Acceptance criteria

- [x] dedicated 20x7 Keyer screen owns Display during app execution;
- [x] top row is exactly `HH:MM KIN KOUT WW Vnn` using UTC;
- [x] KeyIn labels are Pdl/PdR/SkT/SkR;
- [x] KeyOut is only SKS/SKM/OFF;
- [x] SKS/SKM electrical behavior matches this task;
- [x] shutdown always releases both KeyOut lines;
- [x] rows 1-5 show bounded decoded history and follow tail;
- [x] row 6 shows unsent TX tail or temporary status;
- [x] ordinary printable keys are available for CW TX;
- [x] Ctrl+C exits safely;
- [x] Opt toggles Operation;
- [x] Fn+Up/Down wraps O1/O2/O3;
- [x] [ and ] adjust WPM and persist;
- [x] { and } adjust volume and persist;
- [x] Alt+1..5 selects M1-M5;
- [x] Tab Tune works with timeout;
- [x] backtick cancels keyboard/message TX;
- [x] Enter bypasses TxDelay;
- [x] Backspace edits only unsent TX tail;
- [x] physical KeyIn cancels queued/active automatic TX and wins immediately;
- [x] keyboard/message Morse timing is nonblocking and 1/3/1/3/7 correct;
- [x] M1 repeat behavior works and cancellation rules are tested;
- [x] O1/O2/O3 editing works at 20 columns;
- [x] all exposed settings persist immediately;
- [x] persistence includes GPIO line IDs and does not destroy wiring settings;
- [x] safe save failure cannot corrupt prior valid settings;
- [x] legacy key_out=SK and SK-M load as SKS/SKM;
- [x] volume is portable PCM scaling, no public Audio API change;
- [x] K3 engine timing/decoder regressions remain unchanged;
- [x] K4/K5 GPIO/sidetone tests remain passing;
- [x] external ADV ELF retains only `mini_api_get` resident import;
- [x] no MiniShell resident Keyer semantics added;
- [x] real ADV `keyer.elf` builds.

## Automated tests

Add focused pure/host tests for at least:

### UI

- exact 20-char top row for every KeyIn/KeyOut label combination;
- UTC unavailable placeholder;
- decoded history fill/wrap/follow-tail;
- TX/status bottom row;
- Opt Operation entry/exit;
- Fn+Up/Down page wrap;
- editor commit/cancel;
- Ctrl+C distinct from plain `c`;
- ordinary Q/O/digits remain TX text;
- shortcut modifier discrimination.

### KeyOut

- SKS idle/keyed levels;
- SKM ring grounded while active app, tip keyed;
- OFF no drive;
- close/error cleanup releases both;
- legacy SK/SK-M config migration.

### TX engine

Use deterministic monotonic timestamps to prove:

- dit/dah durations;
- element/character/word gaps;
- WPM changes;
- FIFO append/compact/backspace;
- 511-char capacity;
- unsupported input handling;
- TxDelay and reset-on-type;
- Enter immediate start;
- cancel;
- physical-input preemption;
- M1-M5 queueing;
- M1 repeat timing and cancellation.

### Persistence

- full default canonical file;
- load/save round trip for every K6 setting;
- shortcut persistence;
- M1-M5 95-char boundaries;
- invalid ranges;
- missing file defaults;
- safe write/sync/close/rename error paths;
- existing file remains intact on failed save;
- GPIO lines survive unrelated setting edits.

### Sidetone

- volume 0/1/50/99 scaling;
- mute suppresses audio but not logical KeyOut;
- ramp behavior remains bounded;
- no overflow.

Run the full repository gates:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T038-build-unit
cmake --build /tmp/T038-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T038-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . keyer
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . keyer

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

cd platform/adv/elf_apps/keyer
idf.py fullclean
idf.py elf

git diff --check
```

Also run all existing Keyer K3/K4/K5 and Audio TX regressions explicitly.

Record the external ELF size and resident firmware/static SRAM deltas. K6 should
not require a meaningful resident firmware/SRAM increase because it is an external
application and uses existing public services.

## Manual / hardware validation

After supervisor review, install the exact built `keyer.app.elf` as
`/flash/apps/keyer.elf` or `/sd/apps/keyer.elf`.

### H1 — normal screen

- run `keyer`;
- dedicated screen appears;
- top row shows UTC/KeyIn/KeyOut/WPM/volume;
- paddle decodes into rows 1-5;
- sidetone and G3/G6 KeyOut remain correct;
- Ctrl+C exits and restores MiniShell.

### H2 — shortcuts

Verify:

```text
[ ]          WPM
{ }          volume
Opt          Operation
Fn+Up/Down   O-page wrap
Alt+1..5     memories
Tab          Tune
`            TX cancel
Enter        immediate TX
Backspace    unsent edit
Ctrl+C       exit
```

Verify plain `q`, `o`, digits and ordinary text enter the TX FIFO rather than
triggering UI commands.

### H3 — Operation and persistence

Change every O1/O2/O3 setting, exit/relaunch Keyer, and verify values persist.

Power-cycle ADV and verify again.

Check `/flash/keyer/setting.txt` contains canonical settings and retained GPIO
line IDs.

### H4 — keyboard/message TX

With safe test load/logic analyzer or suitable radio input:

- type text and observe TxDelay;
- Enter starts immediately;
- Backspace edits unsent tail;
- verify Morse timing/ordering;
- send M1-M5;
- verify M1 repeat;
- press paddle during automatic TX and verify immediate cancel/manual takeover;
- cancel with backtick;
- verify KeyOut released afterward.

### H5 — KeyOut modes

Verify electrically:

```text
SKS: idle tip/ring high-Z, keyed both low
SKM: ring low while app runs, tip low only key-down
OFF: no KeyOut drive
exit: both released
```

### H6 — Tune/mute/volume

Verify:

- volume changes audible level;
- mute silences sidetone only;
- KeyOut still keys while muted;
- Tune enters/exits;
- TuneTimeout works;
- Ctrl+C during Tune cleans up.

### H7 — lifecycle

Same boot:

```text
keyer -> exercise -> Ctrl+C
keyer -> exercise -> Ctrl+C
ft8 -> quit
keyer -> Ctrl+C
```

No stuck GPIO, speaker ownership, display ownership, or observable leak.

## Codex implementation notes

Implemented against architect baseline `b93463a077f7b9fb7e844cd6280e1321e4d86749`
on `codex/T038-keyer-k6-field-ui`. Local software validation completed 2026-09-20.
No hardware testing was performed.

### Implementation summary

- Added a bounded, pure 20x7 UI with UTC/placeholder header, 64-row decoded
  history, FIFO/status tail, Opt Operation pages, numeric/choice/message editors,
  modifier-aware shortcuts, and Ctrl+C exit. The Display/Input adapter translates
  public events and updates only changed display rows; the controller owns all
  orchestration. Ordinary Q/O/digits remain TX input.
- Added an independent monotonic automatic TX scheduler: 511 editable queued
  characters, uppercase normalization, Morse 1/3/1/3/7 timing, inactivity TxDelay,
  Enter bypass, atomic M1–M5 append, M1 repeat, and latched Tune/timeout. A started
  character leaves the editable FIFO; Backspace cannot alter it. Physical input
  cancels automatic TX/repeat/Tune before manual output arbitration.
- Only SKS/SKM/OFF output policies remain. SK/SK-M configuration aliases migrate
  in memory; old Paddle/PaddleR KeyOut settings fail validation. Shutdown attempts
  both line releases even when one reports an error.
- Added volume/mute PCM scaling without Audio TX reopen or hardware-volume changes.
- Added full canonical settings serialization through MiniShell Filesystem:
  `setting.tmp` write -> sync -> close -> rename to `setting.txt`. Every committed
  menu/shortcut setting calls the writer, preserving GPIO IDs and all other fields.
  Memory values preserve printable text, including leading/trailing spaces and `=`.
- Mini-CW reference inspected at `3bfbf169b7c2d49a1be3e9a4c80f945edb32033e`:
  README Keyer manual, UI history/settings, keyer-service interface and TX scheduler.
  T038 overrides its monolithic/platform-dependent design and old output/UI policies.

### Files changed

- `apps/keyer/include/keyer_types.h`, `keyer_text.h`: K6 fields and bounded text helpers.
- `apps/keyer/src/ui_shell/*`, `ui_adapter/*`, `tx_engine/*`: new portable UI,
  Display/Input adapter and independent automatic scheduler.
- `apps/keyer/src/app_controller/app_controller.c`: foreground coordination,
  arbitration, UTC/display, settings commits and cleanup.
- `apps/keyer/src/config_service/*`, `keyout/keyout.c`, `sidetone/*`: persistence,
  output correction and application PCM scaling.
- `apps/keyer/main/keyer_util.c`: self-contained compiler copy/zero helpers for ELF.
- `platform/adv/elf_apps/keyer/main/CMakeLists.txt`: external application sources only.
- `tests/keyer_k6_{tx,ui,config}_test.c`, existing K4/K5 tests,
  `tests/unit/CMakeLists.txt`, `tests/architecture_rules.py`: focused regressions and
  explicit module ownership/purity enforcement.
- `apps/keyer/README.md` and this packet: usage and handoff evidence.

### Invariants preserved

- No public API/version change; no resident Keyer concepts or resident production
  source changes. FT8, USB ownership/LEVEL1/FIFO, UAC buffers, WebFS and Wi-Fi untouched.
- `keyer_engine/*`, K3 tests and `keyin/*` unchanged. K3 remains the physical timing
  and gesture decoder. Automatic TX does not feed or mutate it.
- Existing 48-frame/48 kHz sidetone transport, 20 ms write timeout, sine DDS and
  short envelope remain. Volume/mute never gate logical KeyOut.
- One existing foreground task; no platform calls or heap allocation added to Keyer.
  Existing 16 KiB ADV application stack unchanged.
- Only `mini_api_get` remains a resident ELF import. No new export table entries.
- K4 tests were narrowly migrated from removed Paddle KeyOut/Q-exit/Console decode
  expectations to the architect's SKS/SKM/OFF/Ctrl+C/Display contract. Physical
  input mapping, output release and K5 transport checks remain covered.

### Memory / size evidence

Real ESP-IDF ADV builds, same SDK/toolchain/configuration before and after.
Baseline ELF confirmed to contain the original K5 controller, before edits.
Measurements: `wc -c`, `xtensa-esp32s3-elf-size -A`, and
`xtensa-esp32s3-elf-readelf -rW`.

| Artifact / section | Baseline bytes | K6 bytes | Delta |
| --- | ---: | ---: | ---: |
| Resident `minishell_adv.bin` | 1,375,776 | 1,375,776 | 0 |
| Resident `.iram0.text` | 63,959 | 63,959 | 0 |
| Resident `.dram0.data` | 27,000 | 27,000 | 0 |
| Resident `.dram0.bss` | 38,864 | 38,864 | 0 |
| Sum of those static internal-SRAM sections | 129,823 | 129,823 | 0 |
| External `keyer.app.elf` file | 11,940 | 22,800 | +10,860 |
| External `.text` | 7,233 | 15,238 | +8,005 |
| External `.rodata` | 1,028 | 1,429 | +401 |
| External `.data.rel.ro` | 328 | 676 | +348 |
| External `.bss` | 444 | 3,628 | +3,184 |

Resident heap-start address is unchanged at `1070219088`; all resident allocated
section sizes are unchanged. External application sections are loaded only for its
lifetime; these are static ELF measurements, not hardware heap measurements.
Final relocation table contains exactly one `R_XTENSA_JMP_SLOT`, `mini_api_get`.
The expected readelf warning about the loader-stripped `.dynamic` section does
not affect the relocation-table check.

### Local tests run

All final gates passed:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 74/74 PASS

cmake -S tests/unit -B /tmp/T038-build-unit
cmake --build /tmp/T038-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T038-build-unit --output-on-failure
# 18/18 PASS

ctest --test-dir /tmp/T038-build-unit -R 'keyer|api_audio' --output-on-failure
# 8/8 PASS: K3, K4 I/O/controller, K5, three K6 suites, Audio API
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux -R 'audio|Audio' --output-on-failure
# 5/5 PASS: Linux audio, ADV TX timeout, Linux discontinuity, RX6 adapter, UAC buffer

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . keyer
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . keyer
# all PASS

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
idf.py -C platform/adv/elf_apps/keyer fullclean
idf.py -C platform/adv/elf_apps/keyer elf
# resident build and clean external ELF build PASS

xtensa-esp32s3-elf-readelf -rW platform/adv/elf_apps/keyer/build/keyer.app.elf
# asserted exactly one JMP_SLOT, mini_api_get

git diff --check
# PASS
```

Focused tests cover exact headers/all mode labels, history wrap/gestures, status
expiry, modifiers/page navigation/edit commit/cancel, FIFO capacity and unsupported
atomic rejection, Morse durations/gaps/WPM, TxDelay reset/bypass, memory repeat and
all cancellation sources, Tune/timeout, safe persistence failures at open/write/
zero-progress/sync/close/rename, full round-trip/max messages/wiring, legacy aliases,
and volume 0/1/50/99/mute/envelope bounds. Controller scenarios verify physical
preemption of active keyboard TX and Tune, Ctrl+C during Tune, ordinary Q text,
actual shortcut persistence, visible failed-save status, and repeated cleanup.

### Manual/hardware validation still required

Supervisor review first, then all H1–H7 in this packet using the exact reviewed
ELF. No installation, radio keying, electrical measurement, or hardware acceptance
was attempted. Display/settings I/O responsiveness, CW timing under UI activity,
audible envelope/volume, power-cycle persistence, and flash/SD repeated ELF
lifecycle remain hardware checks.

### Known limitations / risks

- Save failure intentionally retains the runtime setting, reports `Save failed`
  plus a diagnostic, and keeps the prior file. It does not claim persistence or
  retry in the background; a later commit retries the full current configuration.
- Unsupported Morse characters reject the whole append. Memory editing permits
  printable ASCII, so a stored unsupported character produces `Unsupported char`
  when that memory is selected rather than silently changing its text.
- Tune is the task-permitted latched implementation. M1's repeat interval starts
  when its last element completes; repeats bypass a second TxDelay. Initial memory
  selection uses the same inactivity delay as ordinary typing.
- Filesystem sync/rename and Display presentation use existing synchronous public
  APIs in the foreground loop. Hardware responsiveness/timing and power-loss
  behavior remain unmeasured; software failure tests do not establish crash atomicity.
- No architectural deviations or K7 features added.

### Commit

One implementation commit on `codex/T038-keyer-k6-field-ui`, based on
`b93463a077f7b9fb7e844cd6280e1321e4d86749`. The exact SHA is supplied in the
engineer handoff; no PR. T038 remains REVIEW pending supervisor review.

## Supervisor review — implementation

Reviewed implementation commit:

```text
009049221b27e830df73cf8a080aea09ae0f3cad
```

Result: **PASS — ready for ADV hardware validation.**

Reviewed the production diff, not only the handoff. The implementation matches the
K6 architecture and preserves the accepted K3/K4/K5 boundaries:

- `keyer_engine/**` remains unchanged and owns physical paddle/straight-key timing and decode;
- new `tx_engine` is a separate pure monotonic automatic-TX state machine;
- physical KeyIn is sampled every foreground loop and cancels automatic TX/Tune/repeat before output arbitration;
- keyboard TX uses tested 1/3/1/3/7 timing, bounded 511-character storage, TxDelay, Enter bypass, unsent-tail Backspace, M1-M5 and M1 repeat;
- Ctrl+C is distinguished from ordinary C, while Q/O/digits remain text;
- Opt and Fn+Up/Down implement the three Operation pages;
- the normal top row is exactly 20 characters with UTC/KeyIn/KeyOut/WPM/volume;
- decoded history and TX/status rows are bounded and Display-owned;
- KeyOut is reduced to SKS/SKM/OFF; SKM holds ring low only while the app is active and cleanup attempts release of both lines;
- legacy `SK`/`SK-M` settings map to SKS/SKM; legacy Paddle/PaddleR KeyOut is rejected;
- volume/mute are portable PCM scaling and do not alter the public Audio API or logical KeyOut;
- canonical settings include all K6 fields plus GPIO IDs;
- persistence uses temp write -> sync -> close -> rename replacement; failure removes only the temp file and preserves the previous committed file;
- runtime setting retention on save failure is explicit and visible as `Save failed`;
- no resident MiniShell production code, public API, FT8, USB, WebFS or Wi-Fi behavior changed;
- external ELF relocation evidence shows only `mini_api_get` as a resident import.

Software evidence is sufficient for hardware testing:

- Linux CTest 74/74;
- portable units 18/18;
- focused Keyer/Audio regressions pass;
- architecture/dependency/platform checks pass;
- real ADV resident firmware build passes;
- clean external Keyer ELF build passes;
- `git diff --check` passes.

Resource impact is appropriate for an external application:

- resident firmware delta: 0;
- resident static SRAM delta: 0;
- external `keyer.app.elf`: 22,800 bytes;
- external BSS: 3,628 bytes;
- no new task or resident stack.

One minor behavioral note for hardware use: normal-screen TX shortcuts are not
processed while inside Operation/editing; Ctrl+C and physical KeyIn preemption
remain available. This is not a blocker for K6 acceptance.

Proceed with H1-H7 using the exact reviewed ELF.

## Supervisor review

Review the exact implementation diff, with special attention to:

- no modification of proven K3 physical timing semantics;
- TX scheduler separation;
- UI/input modifier correctness;
- SKS/SKM electrical safety;
- immediate physical-input preemption;
- persistence safety;
- no public API/resident Keyer semantics;
- external ELF import policy;
- runtime/output cleanup.

No PR required.

## Hardware finding — R1

Initial ADV hardware testing of K6 is broadly good. The user reports the other
tested functions are working, but choice editing in Operation has one input bug:

```text
Opt -> O1
6   -> edit Paddle
Fn+Right
```

does not advance `IambicA -> IambicB`.

Root cause from supervisor review: ADV arrows are delivered as arrow special
events carrying `MINI_MOD_FN`. In `ui_shell_input()`, Operation page navigation
correctly consumes Fn+Up/Down when not editing, but the generic modifier rejection
also discards Fn+arrow while an item is being edited before the editor's
Left/Right/Up/Down adjustment logic sees it.

R1 scope is deliberately narrow:

- while `u->operation && u->editing`, accept `UI_FN + UI_LEFT/RIGHT/UP/DOWN`
  as the corresponding editor direction;
- keep Fn+Up/Down page navigation only at Operation top level;
- restore the Mini-CW-style normal-screen `\\` shortcut as a global sidetone
  mute toggle; it must toggle `mute`, persist immediately, and must not enter
  the TX FIFO or report `Unsupported char`;
- do not change keyboard TX, K3 engine, KeyOut, persistence, sidetone, or public APIs;
- add/adjust a focused UI regression proving a choice item such as Paddle changes
  through the actual ADV-style Fn+arrow event and commits/persists normally;
- rerun the focused Keyer tests, full Linux/unit gates, architecture checks, ADV
  firmware build and Keyer ELF build.

Expected hardware flows after R1:

```text
Opt
6
Fn+Right    IambicA -> IambicB
Enter       commit/save
Opt         return

\\           toggle Mute ON/OFF and save immediately
```

All other currently tested K6 functions remain accepted pending completion of
H1-H7 after this R1 correction.

## Codex R1 implementation / validation

R1 implemented from `9c1f7c5554c8c50f0faf408956debe6fe5e96a9b` on
`codex/T038-keyer-k6-field-ui`. Ready for supervisor re-review; no additional
hardware testing performed.

### Implementation summary and files changed

- `apps/keyer/src/ui_shell/ui_shell.c`: accept exact Fn+arrow direction events
  inside an active Operation editor, after the existing top-level page navigation
  branch. Other disallowed modifiers remain rejected. Bare normal-screen `\`
  toggles mute and returns the existing save action instead of a TX-text action.
- `apps/keyer/src/app_controller/app_controller.c`: the existing apply/save path
  displays `Mute:ON` / `Mute:OFF` for a successful normal-screen mute change.
  Failed saves still display `Save failed` and retain the runtime setting under
  the established policy. Sidetone settings are applied before saving as before.
- `tests/keyer_k6_ui_test.c`: all four ADV-style Fn+arrow editor directions,
  unchanged page navigation outside editing, commit/cancel and modifier rejection;
  both mute-toggle directions and normal-screen-only shortcut discrimination.
- `tests/keyer_k4_controller_test.c`: actual MiniShell input events through the
  adapter/controller for Opt -> 6 -> Fn+Right -> Enter -> Opt; verifies persisted
  `paddle=IambicB`. Backslash scenarios verify immediate `mute=On`/`mute=Off`
  saves, both displayed statuses, no unsupported-character status, physical KeyOut
  operation while muted, and preservation of the old file/status on save failure.
- `apps/keyer/README.md`: document the restored shortcut and ADV editor arrows.
- This packet: R1 evidence and REVIEW status.

### Preserved behavior / limitations

K3 engine, automatic TX scheduler, KeyOut implementation, configuration writer,
sidetone transport, public APIs and resident firmware sources are unchanged.
No new action type, persistence mechanism, task or allocation was introduced.
Top-level Fn+Up/Down still wraps Operation pages. Operation message editing still
accepts printable backslash text; only the normal screen treats it as mute.
No scope deviations. Hardware confirmation remains with the architect after
supervisor review; this amendment contains software/build evidence only.

### Tests and builds

All commands passed:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 74/74 PASS
cmake -S tests/unit -B /tmp/T038-R1-unit
cmake --build /tmp/T038-R1-unit -j"$(nproc)"
ctest --test-dir /tmp/T038-R1-unit --output-on-failure
# 18/18 PASS
ctest --test-dir /tmp/T038-R1-unit -R 'keyer|api_audio' --output-on-failure
# 8/8 PASS, including K3/K4/K5 and K6 regressions
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux -R 'audio|Audio' --output-on-failure
# 5/5 PASS
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . keyer
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . keyer
source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
idf.py -C platform/adv/elf_apps/keyer fullclean
idf.py -C platform/adv/elf_apps/keyer elf
xtensa-esp32s3-elf-readelf -rW platform/adv/elf_apps/keyer/build/keyer.app.elf
# Exactly one R_XTENSA_JMP_SLOT: mini_api_get
# Expected warning: loader-stripped .dynamic section, as in initial K6 build.
git diff --check
```

### Firmware / memory evidence

Measured with `wc -c` and `xtensa-esp32s3-elf-size -A` against pre-amendment
artifacts (the reviewed K6 ELF was 22,800 bytes):

| Artifact / section | Before bytes | R1 bytes | Delta |
| --- | ---: | ---: | ---: |
| Resident firmware BIN | 1,375,776 | 1,375,776 | 0 |
| Resident `.iram0.text` | 63,959 | 63,959 | 0 |
| Resident `.dram0.data` | 27,000 | 27,000 | 0 |
| Resident `.dram0.bss` | 38,864 | 38,864 | 0 |
| Static internal SRAM, sum of above sections | 129,823 | 129,823 | 0 |
| External `keyer.app.elf` | 22,800 | 22,952 | +152 |
| External `.text` | 15,238 | 15,346 | +108 |
| External `.rodata` | 1,429 | 1,449 | +20 |
| External `.data.rel.ro` | 676 | 676 | 0 |
| External `.bss` | 3,628 | 3,628 | 0 |

### Commit reference

One R1 amendment commit on `codex/T038-keyer-k6-field-ui`; its exact SHA is
returned in the engineer handoff. No PR. T038 is REVIEW.

## Supervisor review — R1

Reviewed R1 commit:

```text
70595b011cb97f5fe9a025a4d07e9d96cca78605
```

Result: **PASS — return to ADV hardware TESTING.**

The R1 production delta is narrow and matches the two hardware findings:

- inside an active Operation editor, an arrow event carrying exactly `UI_FN`
  is normalized to the existing editor direction path; top-level Fn+Up/Down page
  navigation remains unchanged because that branch executes first;
- extra modifier combinations such as Fn+Ctrl remain rejected;
- bare normal-screen `\\` toggles `mute` and returns the existing SAVE action;
- the controller applies sidetone settings immediately, persists through the
  existing safe settings writer, and reports `Mute:ON` / `Mute:OFF` only after
  a successful save;
- save failure still reports `Save failed`, retains the runtime setting under
  the established K6 policy, and preserves the previous file;
- `\\` remains ordinary printable text inside a message-memory editor;
- K3 engine, automatic TX scheduler, KeyOut, persistence implementation,
  sidetone transport and public APIs are unchanged.

Focused tests exercise the actual ADV-style `MINI_MOD_FN + arrow` event through
both UiShell and controller/adapter paths, confirm Paddle persists as IambicB,
verify both mute-toggle directions and statuses, confirm no `Unsupported char`,
and cover save failure. Full Linux/unit/architecture/ADV firmware/ELF gates pass.

Resource impact remains external-app only:

```text
keyer.app.elf  22,952 bytes  (+152 from initial K6)
resident firmware/SRAM delta 0
resident import             mini_api_get only
```

Hardware retest only needs to reconfirm these R1 fixes plus a quick regression
of the already-good K6 controls before final T038 acceptance.


## Hardware finding — R2

R1 hardware retest passed:

- `\\` mute toggle works;
- ADV Fn+arrow choice editing works.

One minor usability issue remains. Once an Operation item is selected for editing,
the current convenient exit path is only Enter to commit; Fn+` produces Escape
and already cancels correctly, but that chord is not convenient or discoverable.

R2 behavior:

```text
Operation top level:
  bare `        -> leave Operation and return to Keyer
  Escape/Fn+`   -> same

Operation editing:
  bare `        -> cancel edit, discard draft, return to Operation top level
  Escape/Fn+`   -> same

Normal Keyer screen:
  bare `        -> unchanged: cancel active/queued automatic TX
```

Backtick is no longer required as a literal M1-M5 message-memory character. Inside
Operation message editing, bare backtick should therefore act as Back/Cancel
rather than be inserted into the message.

R2 scope is deliberately narrow:

- implement the above Operation-only bare-backtick Back/Cancel behavior;
- preserve Enter commit/save behavior;
- preserve normal-screen backtick automatic-TX cancel behavior;
- preserve Fn+` / Escape behavior;
- do not change any other K6 shortcut, TX scheduler, KeyOut, sidetone,
  persistence architecture, public API, or K3 engine behavior;
- add focused UI/controller regression tests for top-level Operation exit,
  edit cancel without persistence, message-edit cancel, and unchanged
  normal-screen TX cancel;
- rerun focused Keyer tests plus full T038 software/build gates.

## Codex R2 implementation / validation

Implemented from `01abbc4df0024c65d4c5f9b52744a6f93f0dec21` on
`codex/T038-keyer-k6-field-ui`. R2 is ready for supervisor review.

### Implementation and files changed

- `apps/keyer/src/ui_shell/ui_shell.c`: extend the existing Operation Escape
  condition to also accept a bare backtick character with no modifiers. Top-level
  Operation exits; an active editor cancels to Operation top level without a save
  action. This branch precedes message text insertion, so M1–M5 also cancel.
- `tests/keyer_k6_ui_test.c`: top-level exit; numeric, choice and all five message
  cancellations returning no save action; unchanged committed settings and fresh
  values on reopening; Enter commit, Fn/Escape, modifier discrimination and
  normal-screen TX-cancel action remain covered.
- `tests/keyer_k4_controller_test.c`: MiniShell events through the adapter and
  controller cancel numeric/choice/message edits, return to the normal screen,
  and cause zero saves with unchanged file contents. A normal-screen backtick
  during an active dah releases output at the event timestamp and clears TX.
- `apps/keyer/README.md`: document bare backtick as Operation Back/Cancel.
- This packet: R2 evidence and REVIEW status.

### Invariants and remaining validation

Only one production condition changed. Escape/Fn+backtick, Enter commit/save,
normal-screen automatic-TX cancellation, all other shortcuts, the controller,
K3 engine, TX scheduler, KeyOut, sidetone, persistence architecture and public APIs
are unchanged. Cancelled drafts are discarded using the existing Escape behavior;
reopening an editor copies the committed configuration again. No scope deviation
or new known limitation. Hardware confirmation remains with the architect after
supervisor review; no additional hardware testing or PR was performed.

### Software/build gates

All passed:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 74/74 PASS
cmake -S tests/unit -B /tmp/T038-R2-unit
cmake --build /tmp/T038-R2-unit -j"$(nproc)"
ctest --test-dir /tmp/T038-R2-unit --output-on-failure
# 18/18 PASS
ctest --test-dir /tmp/T038-R2-unit -R 'keyer|api_audio' --output-on-failure
# 8/8 PASS, including K3/K4/K5 and K6
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux -R 'audio|Audio' --output-on-failure
# 5/5 PASS
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . keyer
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . keyer
source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
idf.py -C platform/adv/elf_apps/keyer fullclean
idf.py -C platform/adv/elf_apps/keyer elf
xtensa-esp32s3-elf-readelf -rW platform/adv/elf_apps/keyer/build/keyer.app.elf
# Exactly one R_XTENSA_JMP_SLOT: mini_api_get
# Expected loader-stripped .dynamic warning, unchanged from R1.
git diff --check
```

### Size / SRAM evidence

Measured with `wc -c` and `xtensa-esp32s3-elf-size -A` against pre-R2 artifacts:

| Artifact / section | Before bytes | R2 bytes | Delta |
| --- | ---: | ---: | ---: |
| Resident firmware BIN | 1,375,776 | 1,375,776 | 0 |
| Resident `.iram0.text` | 63,959 | 63,959 | 0 |
| Resident `.dram0.data` | 27,000 | 27,000 | 0 |
| Resident `.dram0.bss` | 38,864 | 38,864 | 0 |
| Static internal SRAM, sum above | 129,823 | 129,823 | 0 |
| External `keyer.app.elf` | 22,952 | 22,972 | +20 |
| External `.text` | 15,346 | 15,366 | +20 |
| External `.rodata` | 1,449 | 1,449 | 0 |
| External `.data.rel.ro` | 676 | 676 | 0 |
| External `.bss` | 3,628 | 3,628 | 0 |

### Commit reference

One R2 amendment on `codex/T038-keyer-k6-field-ui`; the exact SHA is returned in
the engineer handoff. T038 is REVIEW. No PR or additional hardware testing.

## Supervisor review — R2

Reviewed R2 commit:

```text
38dd1298d911e92c51494de3928fa188d94bff7a
```

Result: **PASS — return to ADV hardware TESTING.**

R2 changes only the Operation Back/Cancel condition:

- bare backtick with no modifiers now follows the same path as Escape while in
  Operation;
- at Operation top level it returns to the normal Keyer screen;
- inside numeric, choice, or M1-M5 editing it cancels the draft without SAVE;
- Enter commit/save is unchanged;
- Fn+` / Escape behavior is unchanged;
- on the normal Keyer screen bare backtick still returns the existing automatic
  TX-cancel action.

Focused UiShell and controller tests cover top-level exit, numeric/choice/all-five
message edit cancellation, no persistence on cancelled drafts, modifier
discrimination, and active automatic-TX cancellation/release on the normal screen.
Full Linux/unit/architecture/ADV firmware/ELF gates pass.

Resource impact remains external-app only:

```text
keyer.app.elf  22,972 bytes  (+20 from R1)
resident firmware/SRAM delta 0
resident import             mini_api_get only
```

No blocker found.


## Hardware finding — R3

After the otherwise-good K6/R1 hardware exercise, two Mini-CW compatibility
issues remain.

### R3-A — sidetone popping between automatic-TX characters

Observed on ADV while sending M1 (for example Alt/1): audible popping/clicking
between Morse characters.

Reference behavior was re-checked against `wcheng95/Mini-CW`:

```text
components/audio_service/audio_service.c
```

Mini-CW does not use a simple linear key envelope. Its CW renderer uses:

```text
AUDIO_CW_ENVELOPE_MS = 5
5 ms raised-cosine attack on every keyed element
5 ms raised-cosine release on every keyed element
free-running DDS phase
continuous PCM stream
explicit zero-valued gap segments
```

The raised-cosine edge reaches silence with zero slope and is the field-proven
click-suppression behavior.

Current MiniShell K6 already has the correct higher-level architecture:

```text
physical/automatic logical key_down
        -> sidetone
        -> existing continuous MiniShell Audio TX stream
```

Do **not** port Mini-CW's FreeRTOS audio task, segment FIFO, HAL, or Audio service.
Keep the existing K5/K6 MiniShell Audio TX ownership and 48-frame/48 kHz streaming
path.

R3-A scope:

- replace the current linear `gain_q8++/--` edge with a portable
  Mini-CW-compatible approximately 5 ms raised-cosine attack/release;
- at 48 kHz, nominal edge length is 240 samples;
- preserve the current free-running tone phase across keyed and silent periods;
- continue writing PCM continuously while the Keyer Audio stream is open;
- silent gaps remain zero-valued PCM through the existing foreground write path;
- volume and mute remain application-side scaling;
- no public Audio API change;
- no new task/thread;
- no ESP-IDF/FreeRTOS/libm dependency in the external ELF;
- retain only `mini_api_get` as resident import;
- manual paddle sidetone, automatic keyboard/M1-M5 TX, and Tune all use the same
  improved envelope behavior where applicable;
- a cancellation/preemption must still release the tone cleanly rather than create
  a new click.

The exact implementation may use a small fixed integer/LUT approximation of the
same raised-cosine law. Do not add runtime floating point or libm merely to make
the curve mathematically exact.

Add deterministic sidetone tests proving:

- attack starts at/near zero and rises monotonically;
- release falls monotonically to zero;
- edge length is approximately 5 ms / 240 samples at 48 kHz;
- element/gap transitions do not jump directly from full-scale tone to full-scale
  silence;
- volume 0/1/50/99 and mute remain correct;
- phase remains continuous across key-up gaps;
- cancellation/release is bounded and ends at zero.

### R3-B — Mini-CW Alt M1-M5 shortcut overlay

Observed: K6 supports direct Alt+1..5 memory selection but pressing Alt itself
does not show the M1-M5 overlay that Mini-CW provides.

Reference behavior from Mini-CW `components/ui_service/ui_service.c`:

- pressing Alt itself toggles `keyer_shortcut_active`;
- while active, normal decoded-history rows are temporarily replaced by:

```text
M1:<message 1 preview>
M2:<message 2 preview>
M3:<message 3 preview>
M4:<message 4 preview>
M5:<message 5 preview>
```

- each row is 20 columns; Mini-CW uses `M#:%.17s`;
- the normal top status row remains visible;
- the normal bottom TX/status row remains visible;
- pressing plain `1`..`5` while overlay is active selects/queues that memory;
- Alt toggles the overlay off again;
- decoded-history contents are preserved underneath and reappear when the overlay
  is closed;
- entering Tune clears the overlay in Mini-CW.

MiniShell ADV already emits pressing Alt itself as `MINI_KEY_ALT`; K6's adapter
currently maps modifier state but does not expose the special Alt key as a UiShell
action.

R3-B scope:

- add a pure UiShell memory-overlay state;
- map physical `MINI_KEY_ALT` through `ui_adapter`;
- on normal Keyer screen, Alt special-key press toggles the overlay;
- overlay rows 1-5 show `M1:`..`M5:` plus up to 17 message characters;
- while overlay active, plain `1`..`5` queues the matching memory through the
  existing TX engine;
- direct `Alt+1..5` remains supported for compatibility/convenience;
- pressing Alt again closes the overlay;
- Tab/Tune closes the overlay before entering Tune;
- Opt/Operation closes or suspends the overlay so Operation is never obscured;
- Ctrl+C still quits;
- physical KeyIn preemption remains unchanged;
- no persistence is needed for overlay state;
- no change to M1 repeat semantics or stored message format.

Add focused UI/controller tests for:

- Alt special-event toggle on/off;
- exact `M1:`..`M5:` previews at 20 columns;
- decoded history restored after closing overlay;
- plain 1..5 selection while overlay active;
- direct Alt+1..5 still works;
- Tune/Operation interaction;
- Ctrl+C and physical-input behavior unchanged.

### R3 non-goals

Do not redesign:

- K3 physical engine;
- TX scheduler timing;
- KeyOut;
- settings file format;
- Audio public API;
- ADV speaker provider;
- MiniShell resident code;
- FT8.

Run the full T038 software/build gates after R3, including focused sidetone/UI/
controller regressions, ADV firmware build, clean Keyer ELF build, import check,
size/SRAM evidence and `git diff --check`.

Hardware retest after supervisor review should specifically compare:

1. M1 audio against the previous popping behavior;
2. manual paddle sidetone for any new envelope artifact;
3. Alt overlay display and plain 1..5 selection;
4. previously accepted K6 shortcuts as a quick regression.


## Codex R3 implementation / validation

Implemented R3-A and R3-B only, from
`c18c6069b79b9d3ea330fbf0eb7a86c539285201` on
`codex/T038-keyer-k6-field-ui`. Ready for supervisor review.

### Implementation summary / files changed

- `apps/keyer/src/sidetone/sidetone.c` and `.h`: replace linear Q8 gain with a
  240-sample envelope position and a 61-entry integer Q15 raised-cosine table,
  interpolated every four samples. Nominal full attack/release is five 48-frame
  blocks (5 ms). Reversal during an unfinished edge continues from its current
  position instead of resetting gain. Release reaches exact zero within 240
  samples, including shutdown; PCM then remains zero while oscillator phase keeps
  advancing. Existing carrier table, free-running DDS, volume/mute scaling,
  48 kHz stream, 48-frame writes and finite transport timeout are retained.
- Reference read: Mini-CW `3bfbf169b7c2d49a1be3e9a4c80f945edb32033e`,
  `components/audio_service/audio_service.c` raised-cosine renderer/release tail
  and `components/ui_service/ui_service.c` Alt shortcut/preview behavior. The
  integer table approximates its `(1-cos(pi*t))/2` law; no floating-point/libm,
  service, segment FIFO, task, thread or HAL code was imported.
- `apps/keyer/src/ui_shell/ui_shell.c` and `.h`: transient memory-overlay state,
  Alt toggle, plain 1–5 selection while visible, exact 20-column `M#:...` rows
  with up to 17 preview characters, and dismissal on Tune/Operation entry.
  The top/bottom rows use their unchanged render paths. History continues to
  accumulate underneath and is restored when Alt closes the overlay.
- `apps/keyer/src/ui_adapter/ui_adapter.c`: map `MINI_KEY_ALT` to the new private
  UI special-key value. Actual ADV Alt presses carrying `MINI_MOD_ALT` work;
  direct Alt+1–5 remains supported independently of overlay visibility.
- `tests/keyer_k5_sidetone_test.c`: inspect emitted PCM with an isolated carrier
  to verify monotonic edges, raised-cosine quarter/half points, near-zero attack,
  smooth transitions, exact-zero release, 240-sample length, partial-edge reversal
  and bounded shutdown. Existing volume 0/1/50/99, mute, partial writes and error
  checks remain; additional tests verify free-running phase through silent blocks.
- `tests/keyer_k6_ui_test.c`: toggle, exact previews, empty/long memories, unchanged
  header/status tail, preserved/updated history, all five plain/direct-Alt memory
  actions, Tune/Operation interactions, Ctrl+C and modifier discrimination.
- `tests/keyer_k4_controller_test.c`: actual Alt special events through adapter
  and controller, plain/direct-Alt M1 queueing, history restoration, late physical
  input cancelling the queued memory while decoding normally, Ctrl+C and cleanup.
- `apps/keyer/README.md` and this packet: behavior and validation evidence.

### Invariants / remaining risks

No controller, K3 engine, TX scheduler, KeyOut, settings format/writer, public API,
ADV speaker provider, resident source or FT8 changes. All logical output timing
and physical-input arbitration still use their existing paths. The common
sidetone renderer handles manual, automatic and Tune key state without new tasks
or allocations. Overlay state is not persisted.

The envelope remains driven by the existing logical key state: release tails
follow key-up in the current foreground Audio stream, without predicting or
changing KeyOut timing. A short edge reversal may finish in less than 5 ms; it
never jumps to full gain or resets phase. Actual audible pop reduction, manual
paddle sound and Alt usability still require architect testing after supervisor
review. No hardware testing was performed and no audible-success claim is made.
No scope deviations or new architecture were introduced.

### Tests / builds

All final gates passed:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 74/74 PASS
cmake -S tests/unit -B /tmp/T038-R3-unit
cmake --build /tmp/T038-R3-unit -j"$(nproc)"
ctest --test-dir /tmp/T038-R3-unit --output-on-failure
# 18/18 PASS
ctest --test-dir /tmp/T038-R3-unit -R 'keyer|api_audio' --output-on-failure
# 8/8 PASS, including K3/K4/K5 and K6/R1/R2/R3
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux -R 'audio|Audio' --output-on-failure
# 5/5 PASS
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . keyer
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . keyer
source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
idf.py -C platform/adv/elf_apps/keyer fullclean
idf.py -C platform/adv/elf_apps/keyer elf
xtensa-esp32s3-elf-readelf -rW platform/adv/elf_apps/keyer/build/keyer.app.elf
# Asserted exactly one R_XTENSA_JMP_SLOT: mini_api_get
# Expected loader-stripped .dynamic warning, unchanged from prior builds.
git diff --check
```

### ELF / SRAM impact

Measured with `wc -c` and `xtensa-esp32s3-elf-size -A` against pre-R3 artifacts:

| Artifact / section | Before bytes | R3 bytes | Delta |
| --- | ---: | ---: | ---: |
| Resident firmware BIN | 1,375,776 | 1,375,776 | 0 |
| Resident `.iram0.text` | 63,959 | 63,959 | 0 |
| Resident `.dram0.data` | 27,000 | 27,000 | 0 |
| Resident `.dram0.bss` | 38,864 | 38,864 | 0 |
| Static internal SRAM, sum above | 129,823 | 129,823 | 0 |
| External `keyer.app.elf` | 22,972 | 23,380 | +408 |
| External `.text` | 15,366 | 15,618 | +252 |
| External `.rodata` | 1,449 | 1,573 | +124 |
| External `.data.rel.ro` | 676 | 676 | 0 |
| External `.bss` | 3,628 | 3,628 | 0 |

Only `mini_api_get` is imported from the resident image; no libm/native helper
imports. No new task/stack or heap allocation. These are build-time measurements,
not hardware heap evidence.

### Commit reference

One R3 amendment on `codex/T038-keyer-k6-field-ui`; exact SHA is returned in the
engineer handoff. T038 is REVIEW. No PR or hardware testing.

## Supervisor review — R3

Reviewed R3 commit:

```text
e1a8090ac893256ea8159582a2a883ee1c318a54
```

Result: **PASS — ready for ADV audible/UI hardware retest.**

### R3-A sidetone review

The production change preserves the existing K5/K6 Audio ownership and logical
key timing. It replaces only the portable sidetone envelope:

- 240 samples at 48 kHz = 5 ms nominal edge;
- integer Q15 raised-cosine LUT/interpolation, no libm/floating-point dependency;
- attack and release share one envelope position, so a short reversal continues
  from the current gain instead of jumping/resetting;
- oscillator phase advances on every emitted sample, including silent gaps;
- after release reaches zero, continued key-up blocks emit zero PCM;
- volume/mute remain post-envelope application-side scaling;
- 48-frame writes, 20 ms finite timeout, endpoint and stream lifecycle remain
  unchanged;
- shutdown drains a bounded five-block release and still falls back to abort/close
  if transport fails.

Focused PCM tests verify the raised-cosine quarter/half/three-quarter points,
monotonic 240-sample attack/release, near-zero start, exact-zero end, smooth
partial-edge reversal, free-running phase through silence, volume 0/1/50/99,
mute, partial writes and transport-error behavior.

No Morse timing, TX scheduler, KeyOut or public Audio API change was introduced.

### R3-B Alt overlay review

The ADV adapter now exposes the existing `MINI_KEY_ALT` special event privately
to UiShell. UiShell implements the Mini-CW-style transient overlay without
changing persistent configuration or TX semantics:

- Alt-alone toggles the overlay on the normal Keyer screen;
- rows 1-5 render `M1:`..`M5:` and naturally truncate to the remaining
  17 columns;
- top row and bottom TX/status row use their unchanged render paths;
- decoded history continues accumulating while hidden and is restored when the
  overlay closes;
- plain 1..5 while visible returns the existing memory action;
- direct Alt+1..5 remains supported;
- Tune and Operation clear the overlay;
- Ctrl+C and physical KeyIn preemption remain unchanged.

UiShell and controller tests exercise the actual ADV Alt special-event shape,
plain/direct-Alt memory selection, overlay restoration, late physical preemption,
Tune/Operation interaction and cleanup.

### Software/build evidence

All reported gates pass:

- Linux CTest 74/74;
- portable units 18/18;
- focused Keyer/Audio regressions;
- architecture/dependency/platform checks;
- real ADV firmware build;
- clean Keyer ELF build;
- `git diff --check`.

Resource impact:

```text
keyer.app.elf             23,380 bytes  (+408 from R2)
resident firmware delta   0
resident static SRAM      0
resident import           mini_api_get only
new task/stack/heap       none
```

No architecture blocker found. Audible pop reduction and Alt-overlay usability
must now be confirmed on the Cardputer ADV with this exact reviewed ELF.


## Architect test result

Pending.
