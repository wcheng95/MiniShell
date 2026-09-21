# T045 — Mini-CW Keyer UI / I/O mode cleanup, audio-frozen

Status: TESTING

## Golden baseline / recovery

Current stable checkpoint:

```text
main: da934b03bce4cc8f908fbc1a40afed501a37196d
recovery: golden/minicw-persistence-clean-audio
```

Hardware truth:

```text
MiniShell -> minicw.elf
persistence   PASS
paddle        clean / no pop
automatic M1  clean / no pop
```

The earlier audio-only recovery point also remains:

```text
golden/minicw-clean-audio
48a40d79c13ed60ef9f8444a060164852d226fcd
```

Any audible regression is an immediate stop/revert condition.

## Objective

Make four bounded Keyer UI/model cleanups:

1. Operation/settings menu moves from **Ctrl** to **Opt**.
2. KeyIn user options become:
   - Paddle-Normal (**PDN**)
   - Paddle-Reverse (**PDR**)
   - SK-Tip (**SKT**)
   - SK-Ring (**SKR**)
   - SK-Both (**SKB**)
3. KeyOut user options become:
   - SK-Normal (**SKN**)
   - SK-Mono (**SKM**)
   - OFF (**OFF**)
4. Normal and Operation screens use the persistent 20-column top line:

```text
HH:MM KIN KOUT WW Vnn
```

Example:

```text
09:37 PDN SKN 20 V80
```

This is an external `minicw.elf` change only. No resident MiniShell change is
expected.

## Absolute audio freeze

Do not modify:

```text
include/minishell/api.h
core/minishell_services/audio_service.c
platform/common/tone_stream.c
platform/common/tone_stream.h
platform/common/tone_sim.c
platform/common/tone_sim.h
platform/adv/adv_audio_speaker.cpp
apps/minicw/src/audio_service/audio_service.c
```

Do not alter:

- Tone API;
- worker priority/stack;
- segment ring;
- 5 ms renderer;
- envelope/DDS/phase;
- startup prime;
- codec/I2S/DMA;
- `esp_codec_dev_write()`;
- busy accounting;
- existing paddle/M1 timing;
- persistence quiet-save policy.

Resident firmware must remain byte-for-byte identical to the golden baseline.

## 1. Operation moves from Ctrl to Opt

Current Keyer settings/Operation menu is toggled by the logical Ctrl key.

Change it so:

- Opt toggles Operation at top level;
- Opt while already in Operation exits Operation;
- Ctrl alone has no Keyer UI action;
- Ctrl+C continues to exit the application exactly as today;
- Alt remains the M1-M5 shortcut overlay;
- Fn/navigation/edit controls remain unchanged.

Do not change the raw MiniShell Input API. Use the existing logical
`MINICW_INPUT_EVENT_OPT`.

## 2. KeyIn model

User-facing options and codes:

| Option | top-line code |
| --- | --- |
| Paddle-Normal | PDN |
| Paddle-Reverse | PDR |
| SK-Tip | SKT |
| SK-Ring | SKR |
| SK-Both | SKB |

Use **Paddle-Reverse** as the canonical human label. Persistence may accept
`Paddle_Reverse` as an alias.

### Existing behavior

PDN, PDR, SKT and SKR retain their current electrical/timing behavior exactly.

### New SK-Both behavior

SKB treats either physical input contact as the same straight key:

```text
key_down = tip_pressed || ring_pressed
```

It must reuse the existing straight-key timing/decoder/audio/KeyOut path.

Behavior:

- first asserted contact begins one straight-key element;
- while either contact remains asserted, the element remains down;
- the element ends only when both contacts are released;
- pressing the second contact while the first is held does not begin a second
  element;
- adaptive SK timing and decode behavior are the same as SKT/SKR;
- sidetone hold/release calls are unchanged.

If SKB physical input cancels automatic TX:

- every currently asserted KeyIn contact involved in that cancel is consumed
  until that contact is released;
- no stale second contact may immediately re-key after cancellation.

Add SKB at the end of the KeyIn enum so existing PDN/PDR/SKT/SKR numeric values
remain stable.

KeyIn cycling order:

```text
PDN -> PDR -> SKT -> SKR -> SKB -> PDN
```

For display WPM:

- PDN/PDR use KeyIn WPM;
- SKT/SKR/SKB use current adaptive SK WPM.

## 3. KeyOut model

User-visible options are only:

```text
SK-Normal (SKN)
SK-Mono   (SKM)
OFF       (OFF)
```

Electrical behavior must reuse the already-tested existing modes:

### SKN

Equivalent to current `KEYER_KEY_OUT_SK`:

- idle: tip released, ring released;
- key down: tip active and ring active;
- element end: both released.

### SKM

Equivalent to current `KEYER_KEY_OUT_SK_M`:

- idle: tip released, ring active;
- key down: tip active, ring active;
- shutdown: both lines released.

### OFF

Both lines always released.

The old Paddle / PaddleR KeyOut choices are no longer user-selectable.

For minimum risk, legacy internal enum constants may remain, but:

- UI cycling must only expose SKN -> SKM -> OFF;
- runtime setter/config application must canonicalize legacy Paddle/PaddleR to
  SKN so the application cannot remain in an obsolete output mode;
- do not rewrite the proven SK/SK-M electrical implementation.

## 4. Persistent 20-column header

Use this header on both:

- normal Keyer screen;
- Operation/settings pages.

Exact format:

```text
HH:MM KIN KOUT WW Vnn
```

Fields:

- HH:MM = UTC from MiniShell Time/Location;
- KIN = PDN/PDR/SKT/SKR/SKB;
- KOUT = SKN/SKM/OFF;
- WW = two-digit displayed WPM;
- Vnn = two-digit volume 00..99.

This is exactly 20 columns.

Examples:

```text
00:05 PDN SKN 19 V80
23:59 SKB SKM 27 V05
--:-- PDR OFF 20 V99
```

If MiniShell UTC is unavailable or `utc_get()` fails, display `--:--`.
UTC availability must remain optional; failure must not prevent `minicw`
startup.

Do not add RTC ownership, UTC setting, or persistence of time.

Tune/status remain in the existing lower status line; they do not replace the
new header.

The current dormant OP-name lookup display must not override the fixed header.
Callsign lookup/table loading is a separate future task.

## Operation menu labels

Use the human-readable long names in Operation pages where the current mode is
shown.

Fit within 20 columns by shortening the field prefix, e.g.:

```text
5 In:Paddle-Normal
5 In:Paddle-Reverse
5 In:SK-Both
6 Out:SK-Normal
6 Out:SK-Mono
6 Out:OFF
```

Do not redesign the rest of the Operation pages.

## Persistence compatibility

T044A persistence remains active and must continue to work.

Canonical serialization after T045:

### KeyIn

```text
Paddle-Normal
Paddle-Reverse
SK-Tip
SK-Ring
SK-Both
```

### KeyOut

```text
SK-Normal
SK-Mono
OFF
```

Backward-compatible parser aliases must include the existing T044A / standalone
Mini-CW values:

KeyIn:

```text
Pdl / Paddle                  -> PDN
Pdl-R / PdlR / PaddleR /
Paddle-R / Paddle_R           -> PDR
SK-T / SKT                    -> SKT
SK-R / SKR                    -> SKR
Paddle_Reverse                -> PDR
```

KeyOut:

```text
SK                             -> SKN
SK-M / SKM / SK-Mono          -> SKM
OFF                            -> OFF
Pdl / Paddle / Pdl-R / PaddleR -> SKN   (legacy removed modes)
```

Existing numeric aliases may remain compatible. Do not introduce a numeric
serialization format.

An existing T044A `/flash/minicw/setting.txt` must load successfully without
manual editing. The next successful settings save may canonicalize it to the new
labels.

## Architecture / scope

Allowed production changes are expected only under:

```text
apps/minicw/
```

plus tests/docs/external ELF packaging if alignment requires it.

No changes to:

- resident MiniShell;
- public API;
- existing `apps/keyer`;
- FT8;
- audio implementation;
- persistence transaction/quiet-save mechanics.

Do not add heap allocation.

## Tests

### Input/UI

Prove:

- Opt opens Operation;
- Opt closes Operation;
- Ctrl alone does nothing;
- Ctrl+C still exits;
- Alt M1-M5 remains unchanged;
- exact 20-character headers for all KeyIn/KeyOut modes;
- `--:--` when UTC unavailable;
- Operation pages retain the same header;
- volume/WPM zero padding;
- SK modes display adaptive SK WPM.

### KeyIn

Prove:

- PDN/PDR behavior unchanged;
- SKT/SKR behavior unchanged;
- SKB tip-only press;
- SKB ring-only press;
- SKB both simultaneous;
- SKB one-contact then second-contact overlap;
- SKB does not end until both release;
- SKB adaptive decode path matches straight-key behavior;
- automatic-TX cancellation consumes every asserted SKB contact until release.

### KeyOut

Prove:

- UI cycle is SKN -> SKM -> OFF only;
- SKN electrical behavior is unchanged current SK;
- SKM electrical behavior is unchanged current SK-M;
- OFF remains released;
- legacy Paddle/PaddleR setter/config values canonicalize to SKN;
- shutdown still releases both wires in every mode.

### Persistence

Prove:

- existing T044A file loads unchanged;
- old labels map to new modes;
- new canonical labels round-trip;
- SKB persists;
- legacy removed KeyOut labels load as SKN;
- persistence quiet-save tests remain unchanged/passing.

### Time

Use injected MiniShell `utc_get()` in host tests:

- midnight;
- ordinary time;
- 23:59;
- failure/unavailable -> `--:--`.

Do not require UTC for application availability.

### Regression

Run:

- full Linux CTest;
- portable units;
- focused Mini-CW tests;
- architecture/boundary checks;
- real ADV firmware build;
- clean external ELF build/import inspection;
- `git diff --check`.

Before handoff explicitly report:

```text
protected audio diff     NONE
resident firmware BIN    identical
resident SRAM delta      0
external ELF size        exact
resident imports         mini_api_get only
```

## Hardware acceptance

Install only the new external `minicw.elf` over the current validated resident
MiniShell firmware.

Test in this order:

1. paddle + M1 audio first — must remain clean/no pop;
2. header exactly resembles `HH:MM PDN SKN 20 V80`;
3. Opt opens/closes Operation; Ctrl no longer does;
4. cycle all five KeyIn modes;
5. verify SKT, SKR and especially SKB physical behavior;
6. cycle only SKN/SKM/OFF KeyOut;
7. verify existing settings survive/reload;
8. exit/relaunch and verify persistence;
9. Tune/straight key remain clean;
10. Ctrl+C returns silent.

Any audio regression is immediate failure. Do not tune audio inside T045.

Recovery:

```text
golden/minicw-persistence-clean-audio
da934b03bce4cc8f908fbc1a40afed501a37196d
```

## Non-goals

Do not:

- load callsign lookup tables;
- add GPS;
- add trainer modes;
- add battery/sleep;
- redesign M1-M5;
- change Tune behavior;
- change persistence save timing;
- change audio;
- change MiniShell resident code.

## Branch

Use:

```text
codex/T045-minicw-ui-io-cleanup
```

No PR and no hardware testing by Codex.

Set T045 to REVIEW, push one bounded implementation commit, and return exact SHA
with tests and binary/audio guard evidence.


## Implementation handoff

Baseline: `9be7cec313258deae69136d07b244e6ee99ed638` on
`codex/T045-minicw-ui-io-cleanup`. Recovery checkpoint:
`da934b03bce4cc8f908fbc1a40afed501a37196d`. Commit reference: the single
implementation commit containing this handoff; exact SHA returned after push.

### Implementation summary / changed files

- `apps/minicw/src/keyer_service/keyer_service.{c,h}` appends SKB (numeric 4),
  expands KeyIn cycling, and dispatches tip OR ring into the existing straight
  timing/decoder/hold/release path. On automatic-TX cancellation, both currently
  asserted contacts are independently consumed until release. Existing four
  KeyIn dispatches and SKT/SKR timing routines are unchanged. KeyOut setters and
  config canonicalize old Paddle/PaddleR to SKN; cycling exposes only the existing
  SK/SK-M/OFF implementations. Electrical output routines are unchanged.
- `apps/minicw/src/ui_service/ui_service.c` moves the Operation toggle to Opt;
  Ctrl alone is ignored. The shared exact 20-column header uses UTC, short mode
  codes, appropriate WPM and zero-padded volume on normal/Tune/Operation pages.
  It refreshes on UTC-minute/availability changes even during idle, using the
  existing foreground loop. No new task, delay or timer is added. Long menu
  labels fit via `5 In:` and `6 Out:`. OP lookup cannot override the header.
  Tune/status and the Alt memory overlay retain their lower-row behavior.
- `apps/minicw/src/port/minicw_port.{c,h}` adds one private optional UTC read
  helper; failed/missing UTC does not latch an application error. HH:MM is derived
  with integer day arithmetic, including negative Unix timestamps, without
  platform time calls or new imports. Existing Tone and Filesystem wrappers are
  unchanged.
- `apps/minicw/src/storage_service/storage_service.c` updates only mode labels,
  aliases/ranges and obsolete-output canonicalization. Existing T044A files load
  unchanged; new labels and SKB round-trip. Old numeric values 0–3 for KeyIn
  retain their meanings; old KeyOut 0/1 map to SKN. No startup migration write is
  added. Parser bounds, all-or-nothing load, transaction and quiet-save policy
  remain unchanged; `app_core` has no diff.
- `tests/minicw_domain_test.c` adds SKB electrical/hold/decoder/overlap/cancel and
  mode-cycling cases. Legacy output expectations are narrowly changed to the
  newly required SKN canonicalization. All existing paddle, straight, automatic,
  Tune, decoder and timing traces remain.
- `tests/minicw_runtime_test.c` uses Opt in its existing Operation trace and checks
  the new normal/Tune header; Ctrl+C and Alt traces remain. The persistence test
  changes only its canonical SK-M serialization expectation to SK-Mono; quiet
  save and transactional fault assertions are unchanged.
- `tests/minicw_ui_io_test.c` and `tests/minicw_tests.cmake` add injected UTC,
  input/header and persistence compatibility coverage. `apps/minicw/README.md`
  documents the new controls/modes/header. No external packaging change was needed.

### Behavior / invariants preserved

All eight protected audio files match the stable recovery commit byte-for-byte.
No resident source, public API, PCM/Tone semantics, task/stack/queue/DMA,
application audio wrapper, persistence transaction/save timing, existing `keyer`
or FT8 changes. No heap allocation was introduced. SKB shares the existing
straight-key implementation; SKN/SKM reuse the proven electrical implementation.
No audio tuning, hardware testing, UTC setting or time persistence occurred.

### Tests run and results

```sh
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
# PASS: 84/84
cmake -S tests/unit -B /tmp/T045-unit
cmake --build /tmp/T045-unit -j8
ctest --test-dir /tmp/T045-unit --output-on-failure
# PASS: 26/26
ctest --test-dir build-linux -R minicw --output-on-failure
# PASS: 6/6
ctest --test-dir build-linux -R 'minicw|tone|architecture|boundary' --output-on-failure
# PASS: 19/19

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: real ADV build, before/after implementation
idf.py -C platform/adv/elf_apps/minicw fullclean
idf.py -C platform/adv/elf_apps/minicw elf
# PASS: clean 1074-step external build
python3 tests/minicw_elf_inspect.py platform/adv/elf_apps/minicw/build/minicw.app.elf
# PASS: sole import mini_api_get; 622 mapped relocations; packed alignment valid
xtensa-esp32s3-elf-size -A platform/adv/build/minishell_adv.elf
xtensa-esp32s3-elf-size -A platform/adv/elf_apps/minicw/build/minicw.app.elf
cmp /tmp/T045-before.bin platform/adv/build/minishell_adv.bin
# PASS: identical
git diff --check
# PASS
```

Focused coverage proves: Opt open/close, Ctrl inactivity both inside/outside
Operation, preserved Ctrl+C/Alt; all 15 KIN/KOUT header combinations on normal
and all Operation pages; midnight, 00:05, 09:37, 23:59, failed/absent UTC and idle
minute advance; V00/V05/V99 and WPM 05; adaptive SK WPM selection; SKB tip-only,
ring-only, simultaneous, overlap and last-release behavior; one hold/release per
element; SKB duration/decode/adaptation matches SKT; two-contact cancellation
consumption; KeyIn and KeyOut cycling, legacy setter/config canonicalization,
SKN/SKM/OFF outputs and shutdown release; legacy label/numeric compatibility and
canonical round-trip for every new mode. T044A save guards/fault tests and all
frozen audio regressions remain passing.

### Explicit audio / binary / resource evidence

Each protected file was compared with
`git show da934b03bce4cc8f908fbc1a40afed501a37196d:<path>`:

```text
protected audio diff:  NONE
resident BIN:          IDENTICAL (local baseline vs final real build)
resident SRAM delta:   0 bytes
resident ELF imports:  mini_api_get only
```

Resident source/build inputs at the task baseline match the recovery checkpoint;
there is no diff under `core/`, `include/` or `platform/`. Baseline and final
resident BIN are both 1,381,280 bytes with SHA-256:

```text
72d644ef5f07cbd3ff5c60c0360b75e686426fe6edf284afe3c31f8647bd995a
```

ESP-IDF v5.5.4 / Xtensa GCC 14.2.0:

| Resident section | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,016 | 27,016 | 0 |
| `.dram0.bss` | 40,336 | 40,336 | 0 |
| Static internal SRAM delta | — | — | **0** |

| External `minicw.app.elf` | Before | After | Delta |
| --- | ---: | ---: | ---: |
| File bytes | 40,388 | 43,024 | +2,636 |
| `.text` | 27,568 | 29,648 | +2,080 |
| `.rodata` | 1,880 | 1,940 | +60 |
| `.data` | 1,224 | 1,284 | +60 |
| `.bss` | 3,268 | 3,276 | +8 |
| Section-loader text + data allocation | 33,940 | 36,148 | +2,208 |

Other final sections: `.hash` 40, `.dynsym` 80, `.dynstr` 38, `.rela.dyn` 7,488,
`.rela.plt` 12, `.eh_frame` 92, `.got` 4; `size -A` total 43,902. File-size and
loaded-size deltas differ because of ELF packing. Integer division helpers are
linked locally from the existing libgcc packaging; no new resident import.
No heap, task or resident SRAM allocation was added. Hardware stack/free-heap
measurements were not taken.

### Hardware/manual validation still required / known risks

No PR or hardware testing was performed. After supervisor review, install only
the external `minicw.elf` over the validated resident firmware and execute the
packet's ordered acceptance: paddle/M1 audio first, then header, Opt/Ctrl, all
KeyIn modes including SKB, three KeyOut modes, legacy persistence/relaunch, Tune
and silent Ctrl+C exit. Software tests and unchanged audio files do not replace
that hardware acceptance. Any audible regression requires stopping/reverting to
`golden/minicw-persistence-clean-audio` at
`da934b03bce4cc8f908fbc1a40afed501a37196d`; no audio tuning belongs in T045.


## Supervisor review

Reviewed implementation:

```text
a699602f5ad789b3d89fc4059430e41ee80a020f
```

Result: **PASS — ready for ADV hardware validation.**

### Scope / audio freeze

The implementation is confined to `apps/minicw/**`, tests and documentation.
No protected audio file, resident MiniShell service, public API, FT8 or existing
`apps/keyer` code changed.

Resident firmware remains byte-for-byte identical to the post-T044A golden
baseline. Static resident SRAM delta is zero. Hardware validation therefore
requires installing only the new external `minicw.elf`.

### KeyIn review

PDN/PDR/SKT/SKR retain their existing behavior.

SKB is appended as the fifth KeyIn mode, preserving existing numeric values.
It feeds:

```text
tip_pressed || ring_pressed
```

into the existing straight-key timing/decoder/audio/KeyOut path.

When SKB cancels automatic TX, one TX-cancel event is produced and every
currently asserted contact is independently marked consume-until-release. Thus
a second held contact cannot immediately re-key after the cancel.

Adaptive SK timing remains the same straight-key implementation used by SKT/SKR.

### KeyOut review

The existing electrical implementations are preserved.

The user-visible cycle is restricted to:

```text
SKN -> SKM -> OFF -> SKN
```

Legacy Paddle/PaddleR enum/config values are canonicalized to existing
`KEYER_KEY_OUT_SK` / SKN. SKM remains the existing SK-M electrical behavior.
Shutdown release behavior is unchanged.

### Persistence compatibility review

Canonical labels are now:

```text
KeyIn:  Paddle-Normal / Paddle-Reverse / SK-Tip / SK-Ring / SK-Both
KeyOut: SK-Normal / SK-Mono / OFF
```

The parser accepts existing T044A / standalone aliases, including legacy
Paddle/PaddleR output modes and numeric mode values. Legacy output modes are
mapped to SKN rather than remaining selectable.

The T044A transactional writer and quiet-save state machine are unchanged.

### UI/Input review

Opt now toggles Operation. Ctrl alone is ignored at the Keyer UI level.
Ctrl+C still exits in the private Mini-CW Input port before UI dispatch.

Alt remains the M1-M5 overlay.

The fixed header is exactly 20 columns:

```text
HH:MM KIN KOUT WW Vnn
```

and is used on both normal and Operation screens. OP-name state no longer
replaces the header.

SKT/SKR/SKB display adaptive SK WPM; paddle modes display KeyIn WPM.

### UTC boundary review

The new application helper calls MiniShell Time/Location `utc_get()`.

On ADV, public `utc_get()` does **not** perform an RTC/I2C read per call.
The RTC is read when the resident Time/Location service establishes its UTC
anchor; subsequent `utc_get()` calls derive UTC from resident monotonic time
under the service's in-memory state lock.

Therefore the UI's minute-change polling does not introduce repeated RTC/I2C
traffic into the clean-audio path.

If UTC is unavailable, the header shows `--:--` without making UTC a required
application capability.

### Software / binary evidence

```text
Linux CTest              84/84
portable units           26/26
focused minicw            6/6
architecture/boundary    PASS
ADV firmware build       PASS
clean external ELF       PASS
ELF inspection           PASS
git diff --check         PASS

protected audio diff     NONE
resident BIN             IDENTICAL
resident SRAM delta      0
minicw.app.elf           43,024 bytes
resident import          mini_api_get only
```

### Hardware gate

Install only the T045 external `minicw.elf` over the already accepted resident
MiniShell firmware.

Validate in this order:

1. paddle and M1 remain clean/no-pop;
2. top line is `HH:MM KIN KOUT WW Vnn`;
3. Opt opens/closes Operation; Ctrl alone does nothing;
4. PDN/PDR/SKT/SKR/SKB cycle and behave correctly;
5. SKB tip-only, ring-only and either-contact behavior are correct;
6. KeyOut cycles only SKN/SKM/OFF;
7. existing T044A settings load and persist with new canonical labels;
8. Tune/straight-key remain clean;
9. Ctrl+C returns silent;
10. repeated launch/exit remains clean.

Any audible regression is an immediate failure. Do not alter audio in T045.

Recovery:

```text
golden/minicw-persistence-clean-audio
da934b03bce4cc8f908fbc1a40afed501a37196d
```
