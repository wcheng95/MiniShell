# T039 — ADV dirty-row Display present + live Keyer TX refresh

Status: BREAK

## Objective

Restore live Keyer display updates during automatic CW transmission **without**
reintroducing the ADV speaker pop that T038/R4 eliminated.

T038 hardware testing established the root cause:

- automatic TX with normal Display refresh produced audible pops;
- the same message sent manually with the paddle did not pop;
- the R3 raised-cosine sidetone envelope did not remove the automatic-TX pop;
- suppressing Display render/present during active automatic TX removed the pop.

Further supervisor inspection found the concrete Display-side amplification:

```text
apps/keyer/src/ui_adapter/ui_adapter.c
    writes only dirty rows

platform/adv/adv_display.cpp
    adv_display_present()
        -> render_all()
        -> redraws all 7 x 20 = 140 cells
```

So a one-row Keyer TX-tail update currently causes a full-screen 140-cell ADV
redraw. T039 fixes that provider behavior and then removes the T038 display freeze.

## Architect intent

Keep the successful T038 Keyer architecture:

```text
tx_engine / keyer_engine
        |
        +--> KeyOut
        +--> sidetone -> MiniShell Audio TX
        `--> UiShell -> MiniShell Display
```

Do **not** redesign Audio. The T038 workaround proved that synchronous Display
latency was the interfering work; T039 should reduce that work at the ADV Display
provider boundary.

Target result:

```text
M1 automatic TX
    -> row 6 / relevant UI updates remain live
    -> ADV present redraws only changed Display rows/runs
    -> speaker remains pop-free
```

## Baseline

Start from current `main` after T038 COMPLETE.

T038 final accepted behavior includes:

- dedicated 20x7 Keyer UI;
- `PdL/PdR/SkT/SkR` labels;
- keyboard/message TX and M1-M5;
- Alt memory overlay;
- raised-cosine sidetone envelope;
- persistent Operation settings;
- current temporary/permanent-for-T038 Display deferral during
  `TX_ELEMENT`, `TX_ELEMENT_GAP`, `TX_CHAR_GAP`, `TX_WORD_GAP`.

T039 must replace that Display deferral with an efficient ADV provider path and
restore normal Keyer refresh cadence during automatic TX.

## Source of truth

### Keyer dirty-row producer

`apps/keyer/src/ui_adapter/ui_adapter.c` already compares the new 20x7 frame
against the previous frame. It calls `write_at_attr()` only for dirty rows and
calls `present()` only when something changed.

Preserve that application-side behavior.

### ADV Display provider

Current `platform/adv/adv_display.cpp` keeps a 20x7 back buffer but
`adv_display_present()` always calls `render_all()`, which redraws every cell.

This defeats the application dirty-row optimization.

## Target design

### 1. ADV provider dirty-row tracking

Keep the existing public Display API unchanged.

Inside `platform/adv/adv_display.cpp`, track which back-buffer rows actually
changed.

Required behavior:

- `write_at()` / `write_at_attr()` mark only affected rows dirty when cell
  content or attributes actually change;
- `clear_at()` marks affected rows dirty;
- full `clear()`, console-mode reset, and console scroll mark every affected row
  dirty;
- `adv_display_present()` performs no LCD drawing when nothing is dirty;
- after a successful present, rendered rows become clean;
- initial prepare still produces the same blank/console-visible screen.

Do not change the public 20x7 model or text attributes.

### 2. Render only dirty rows

Replace the unconditional 140-cell redraw with dirty-row rendering.

For each dirty row:

- preserve the existing geometry, font, text size, foreground/background colors,
  and inverse attribute semantics;
- redraw that row only;
- avoid repainting the fixed top separator/gap on every present when it has not
  changed.

Prefer a row/run renderer rather than 20 unrelated per-cell transactions:

- group adjacent cells with the same attribute where practical;
- clear/fill the corresponding run background once;
- emit the contiguous text run;
- preserve correctness if a row ever contains mixed normal/inverse attributes.

Do not assume Keyer is the only Display client.

### 3. Console behavior must remain correct

`adv_display_console_write()` currently mutates the same 20x7 backing buffer.

Preserve:

- wrapping;
- newline;
- backspace;
- scroll;
- switch between console and app full-screen modes.

A console scroll moves multiple rows, so mark/render all moved rows as needed.

### 4. Restore live Keyer rendering during automatic TX

In `apps/keyer/src/app_controller/app_controller.c`, remove the T038/R4
automatic-TX render suppression.

Return to the normal render cadence:

```text
if now >= next_render:
    render
    next_render = now + 50 ms
```

Input events may still force `next_render = 0` as today.

Do not special-case M1, M2-M5, element gaps, character gaps, or word gaps after
this change.

The UI should therefore visibly advance during automatic TX again.

### 5. Keep Audio and timing unchanged

Do **not** change:

- `adv_audio_speaker.cpp`;
- I2S DMA geometry;
- MiniShell Audio API;
- sidetone block size or write timeout;
- raised-cosine envelope;
- `tx_engine` timing/state machine;
- K3 physical engine;
- KeyOut;
- public API version;
- FT8.

T039 is a Display-provider efficiency fix plus restoration of the normal Keyer
render schedule.

## Why not add a speaker worker/ring in this task

The Audio TX API has caller-visible timeout/progress semantics established by
T009/T010. A provider-side asynchronous software queue would require careful new
acceptance/backpressure/timing semantics and could affect every TX application.

The confirmed immediate inefficiency is simpler and local:

```text
dirty Keyer row -> full 140-cell ADV redraw
```

Fix that first. Do not introduce an Audio architecture change unless this
Display-side fix fails hardware validation.

## Automated tests

### A. Keyer controller live-render regression

Update the T038/R4 controller regression.

Prove that during deterministic automatic TX:

- Display render/present occurs while `TX_ELEMENT` / gap phases are active;
- the TX-tail row can change and be presented before automatic TX completes;
- the first post-TX idle render still works;
- TX timing / KeyOut expectations remain unchanged;
- manual paddle behavior remains unchanged.

The old assertion that no render occurs during the active TX interval must be
removed/replaced.

### B. ADV dirty-state pure test

Because the actual ADV provider depends on M5Unified/ESP-IDF, extract only the
minimal dirty-state decision logic into a small private testable helper if useful.

Host tests should prove at least:

- identical write does not dirty a clean row;
- changed character dirties only that row;
- changed attribute dirties only that row;
- `clear_at` dirties the touched row(s);
- full clear marks all required rows;
- consume/present clears only the rows reported as rendered;
- console scroll/full-row movement can mark all moved rows.

Do not create a public API for this helper.

### C. Existing regression gates

Keep passing:

- full Linux CTest;
- portable unit suite;
- focused Keyer/Audio tests;
- architecture/dependency/platform boundaries;
- real ADV firmware build;
- clean external Keyer ELF build;
- `git diff --check`.

Record exact results.

## Resource evidence

Record before/after:

- ADV resident firmware binary size;
- resident `.iram0.text`, `.dram0.data`, `.dram0.bss`;
- Keyer external ELF size/sections;
- Keyer resident imports.

A few bytes of ADV dirty-row state are expected. No new task/stack/heap allocation
is expected.

## Hardware validation

After supervisor diff review, the architect validates the exact reviewed ADV
firmware + Keyer ELF.

### H1 — automatic TX live display + audio

Use the same M1/message that reproduced the T038 pop.

Verify simultaneously:

1. start M1;
2. row 6 / TX-tail display visibly advances during the transmission, not only
   after it finishes;
3. no character-to-character speaker pop returns;
4. Morse/keying timing sounds normal.

Repeat several times.

### H2 — manual paddle

Send the same or similar message manually:

- no pop;
- decoded-history rows update normally;
- no obvious display lag.

### H3 — Keyer UI regression

Quickly verify:

- Alt memory overlay;
- Operation screen;
- `PdL` label;
- mute/volume;
- backtick Back/Cancel;
- Ctrl+C cleanup.

### H4 — resident Display regression

Because `adv_display.cpp` is resident/shared, check at least:

- MiniShell console output/wrap/scroll looks correct;
- launch/quit `ft8`; its 20x7 display remains visually correct;
- return to shell normally.

No RF FT8 validation is required unless an unexpected display/runtime issue is
observed.

## Acceptance criteria

- [ ] ADV present no longer redraws all 140 cells for a one-row update;
- [ ] dirty-row/run behavior preserves normal/inverse rendering;
- [ ] console behavior remains correct;
- [ ] Keyer automatic-TX Display suppression is removed;
- [ ] live Keyer display updates occur during automatic TX in software tests;
- [ ] TX/K3/KeyOut/Audio timing code remains unchanged;
- [ ] no public API/version change;
- [ ] no new task/thread/heap allocation;
- [ ] full software/build gates pass;
- [ ] resident/ELF resource evidence recorded;
- [ ] hardware M1 display visibly advances during TX;
- [ ] hardware M1 remains pop-free;
- [ ] manual paddle remains pop-free;
- [ ] shell/FT8 display smoke tests pass;
- [ ] no unrelated cleanup.

## Branch workflow

Use:

```text
codex/T039-adv-display-dirty-present
```

Codex handoff:

1. implement only T039;
2. run all required local tests/builds;
3. record changed files, design details, tests and resource evidence here;
4. set `Status: REVIEW`;
5. commit and push;
6. return exact SHA;
7. no PR and no GitHub Actions wait;
8. no hardware testing by Codex.

Supervisor reviews the actual diff before hardware testing.

Do not merge to `main` until H1-H4 pass.


## Closeout — BREAK

The architect stopped T039 after repeated Cardputer ADV hardware experiments did
not eliminate the Keyer speaker pop.

The experimental branch remains preserved with detailed R1-R3 evidence. In
summary:

- dirty-row Display rendering restored live automatic-TX refresh but the pop
  remained;
- a resident CPU1 continuous-tone worker with zero PCM during silence did not
  eliminate the pop, and manual paddle could occasionally pop;
- changing only that worker from direct I2S writes to Mini-CW's
  `esp_codec_dev_write()` transport still did not eliminate the pop.

None of the T039 production experiments were merged to `main`.

The project returns to the T038 code line. The remaining sound-pop issue is
explicitly deferred while Keyer UI/transcript simplification continues in T040.
