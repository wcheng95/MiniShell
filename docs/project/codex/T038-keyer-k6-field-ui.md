# T038 — Keyer K6 field UI, keyboard TX, memories, and persistence

Status: READY

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

- [ ] dedicated 20x7 Keyer screen owns Display during app execution;
- [ ] top row is exactly `HH:MM KIN KOUT WW Vnn` using UTC;
- [ ] KeyIn labels are Pdl/PdR/SkT/SkR;
- [ ] KeyOut is only SKS/SKM/OFF;
- [ ] SKS/SKM electrical behavior matches this task;
- [ ] shutdown always releases both KeyOut lines;
- [ ] rows 1-5 show bounded decoded history and follow tail;
- [ ] row 6 shows unsent TX tail or temporary status;
- [ ] ordinary printable keys are available for CW TX;
- [ ] Ctrl+C exits safely;
- [ ] Opt toggles Operation;
- [ ] Fn+Up/Down wraps O1/O2/O3;
- [ ] [ and ] adjust WPM and persist;
- [ ] { and } adjust volume and persist;
- [ ] Alt+1..5 selects M1-M5;
- [ ] Tab Tune works with timeout;
- [ ] backtick cancels keyboard/message TX;
- [ ] Enter bypasses TxDelay;
- [ ] Backspace edits only unsent TX tail;
- [ ] physical KeyIn cancels queued/active automatic TX and wins immediately;
- [ ] keyboard/message Morse timing is nonblocking and 1/3/1/3/7 correct;
- [ ] M1 repeat behavior works and cancellation rules are tested;
- [ ] O1/O2/O3 editing works at 20 columns;
- [ ] all exposed settings persist immediately;
- [ ] persistence includes GPIO line IDs and does not destroy wiring settings;
- [ ] safe save failure cannot corrupt prior valid settings;
- [ ] legacy key_out=SK and SK-M load as SKS/SKM;
- [ ] volume is portable PCM scaling, no public Audio API change;
- [ ] K3 engine timing/decoder regressions remain unchanged;
- [ ] K4/K5 GPIO/sidetone tests remain passing;
- [ ] external ADV ELF retains only `mini_api_get` resident import;
- [ ] no MiniShell resident Keyer semantics added;
- [ ] real ADV `keyer.elf` builds.

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

Codex fills this section before handoff.

### Implementation summary

### Files changed

### Invariants preserved

### Memory / size evidence

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

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

## Architect test result

Pending.
