# T049 — MiniFT8 TX separator + RX message colors

Status: COMPLETE

## Baseline

Start from current production main:

```text
main
c7ccd911ef79bc5c0f05b7cccba30fe128dc0def
```

Branch:

```text
codex/T049-ft8-color-status
```

No PR. No hardware testing by Codex.

## Objective

Use MiniShell's generic text-color and row-separator capabilities to add two
small MiniFT8 presentation improvements on ADV.

### 1. Header separator

Render a 2-pixel separator directly below the top row:

```text
normal / RX / idle TX      WHITE
physical FT8 TX active     RED
```

The separator is global to MiniFT8, not just the RX screen. It remains white
while a TX is merely queued/pending. It becomes red only while
`app_controller_tx_active()` is true, i.e. after physical TX begin succeeds,
and returns white when TX ends or fails.

ADV already owns the physical separator geometry introduced for T047:
separator-after-row-0 maps to y=19, height=2. MiniFT8 must not encode pixel
coordinates.

### 2. RX screen row colors

For displayed RX rows only:

```text
reply/directly addressed to my callsign   RED
CQ                                        GREEN
all other decoded messages               WHITE
```

Color the entire displayed row, including its existing `1..6` selection
prefix.

Use the existing factual `RxMessage.is_to_me` and `RxMessage.is_cq`
classification. Do not re-parse canonical display text.

If both flags were ever true, `is_to_me` takes precedence, matching the
existing T028 display-priority grouping.

Do not change RX ordering, SNR ordering, selection indexes, AutoSeq behavior,
decode behavior, or row lifetime.

## Generic Display prerequisite: RED

Current MiniShell v3 text-color attributes provide DEFAULT/WHITE/GREEN/CYAN but
not RED. Add RED generically without introducing FT8 semantics.

Preserve all existing numeric values:

```c
MINI_TEXT_ATTR_FG_DEFAULT = 0
MINI_TEXT_ATTR_FG_WHITE   = 1 << 1
MINI_TEXT_ATTR_FG_GREEN   = 2 << 1
MINI_TEXT_ATTR_FG_CYAN    = 3 << 1
MINI_TEXT_ATTR_FG_RED     = 4 << 1
```

Widen the foreground field mask from 2 bits to 3 bits:

```c
MINI_TEXT_ATTR_FG_MASK = 7u << 1
```

No API function/table change and no API version bump are required.

Update:
- public header constants;
- Display API documentation;
- generic service validation tests;
- ADV color mapping/tests.

ADV maps RED to full-intensity red on black. Existing WHITE/GREEN/CYAN and
inverse behavior must remain unchanged.

Linux continues to advertise no text-color/separator capability and therefore
remains monochrome.

## Data/presentation ownership

Keep ownership:

```text
RxResultBuilder
  owns factual is_to_me / is_cq classification

app_controller
  projects factual RX category + physical tx_active into UiModel

ui_shell
  owns presentation policy:
  - to-me -> red
  - CQ -> green
  - regular -> white
  - TX-active separator -> red, otherwise white

ft8_ui_adapter
  maps UiFrame semantic colors to generic MiniShell Display attributes

MiniShell Display
  owns generic WHITE/GREEN/CYAN/RED vocabulary + separator operation

ADV
  owns actual RGB and y=19 / height=2 geometry
```

Do not place FT8/CQ/reply semantics in MiniShell or ADV.

## UiModel additions

Add a bounded factual RX category aligned with `rx_lines[]`, e.g.:

```c
typedef enum {
    UI_RX_NORMAL = 0,
    UI_RX_CQ,
    UI_RX_TO_ME,
} UiRxKind;
```

and:

```c
UiRxKind rx_kind[APP_MAX_RX_LINES];
bool tx_active;
```

When building the ordered RX projection, populate `rx_lines[i]` and
`rx_kind[i]` from the same factual message:

```text
batch.messages[display_order[i]]
```

This pairing must stay exact across paging and selection.

Set `tx_active` from the controller's actual physical TX state.

## UiFrame additions

Carry semantic presentation metadata in the rendered frame. A suitable small
shape is:

```c
typedef enum {
    UI_COLOR_DEFAULT = 0,
    UI_COLOR_WHITE,
    UI_COLOR_GREEN,
    UI_COLOR_RED,
} UiColor;

UiColor row_color[UI_MAX_ROWS];
bool separator_after_top;
UiColor separator_color;
```

Equivalent bounded representation is acceptable.

Default all text rows to WHITE. The RX renderer overrides body rows according to
the corresponding `rx_kind[]`. Other screens remain WHITE.

Every MiniFT8 frame requests the top separator:
- RED when `model->tx_active`;
- WHITE otherwise.

The top-row text itself remains WHITE; only the separator changes during TX.

## ft8_ui_adapter

Color/separator are optional capabilities and must not become required for
MiniFT8 startup.

At init/render:
- continue requiring only ordinary text Display/Input as today;
- discover `MINI_DISPLAY_CAP_TEXT_COLOR`, `write_at_attr`,
  `MINI_DISPLAY_CAP_ROW_SEPARATOR`, and `set_row_separator` using capability
  and `struct_size` checks;
- when available, write each row with its requested foreground color;
- set separator-after-row-0 to requested WHITE/RED;
- when unavailable/unsupported, fall back to current plain `write_at()`
  behavior and omit the separator;
- optional styling absence must not fail MiniFT8.

Because `text->clear()` removes separators, set the requested separator again
for every frame before `present()`.

Do not add raw M5/ADV calls to MiniFT8.

## Behavior details

### TX separator state

The bar is RED only for actual active physical transmission.

Expected state transitions:

```text
RX / idle               WHITE
TX intent queued         WHITE
TX pending slot          WHITE
radio begin succeeds
app->tx.active = true    RED
79-tone TX completes     WHITE
TX failure/abort         WHITE
```

Existing RX rows may remain visible during TX per T029; their row colors stay
unchanged while the separator alone becomes RED.

### RX row classification

Use the same factual categories already used by T028 ordering:

```text
is_to_me    -> RED
else is_cq  -> GREEN
else        -> WHITE
```

This includes standard, non-standard, Field Day, DXpedition, and free-text CQ
cases only insofar as `RxResultBuilder` already sets those flags. Do not expand
classification scope in this task.

## Freeze / non-goals

Do not change:

- FT8 decoder/DSP/search parameters;
- RxResultBuilder classification semantics;
- T028 ordering or descending SNR;
- T029 RX display lifetime;
- AutoSeq/QSO state;
- CAT/radio timing;
- 79-tone plan or TX scheduler;
- RX/TX log formats;
- band sync;
- QSO view;
- Mini-CW;
- font, row geometry or top-line text format.

This is presentation only plus the generic RED palette extension.

## Tests

Cover at minimum:

### MiniShell Display

- existing DEFAULT/WHITE/GREEN/CYAN numeric values unchanged;
- RED is accepted under the expanded foreground mask;
- unknown bits remain INVALID;
- RED combines correctly with INVERSE;
- ADV maps RED to `0xFF0000`;
- separator-after-row-0 renders RED at y=19, height=2;
- WHITE separator remains white;
- existing Mini-CW green separator and colors regress unchanged.

### App/controller projection

Build a retained RX batch containing:
- reply-to-me;
- CQ;
- regular.

Prove ordered `rx_lines[]` and `rx_kind[]` remain aligned after T028
reordering.

Prove `model->tx_active` reflects actual controller physical-active state, not
queue count or pending intent.

### ui_shell

For ADV frame:
- top row text unchanged;
- separator WHITE when `tx_active=false`;
- separator RED when `tx_active=true`;
- reply-to-me RX row RED;
- CQ RX row GREEN;
- regular RX row WHITE;
- row numbering prefix receives same row color;
- paging preserves correct row/category pairing;
- non-RX screens remain WHITE.

Text bytes for all equivalent frames must remain identical to pre-T049 baseline.

### adapter fallback

Prove:
- full color+separator provider receives requested attrs;
- color-only provider still works without separator;
- separator-only provider still renders text plainly and separator correctly;
- plain legacy text provider works exactly as before;
- optional `MINI_ERR_UNSUPPORTED` styling does not fail the app.

### Regression

Run:
- full Linux CTest;
- portable units;
- focused FT8/UI/Display/architecture tests;
- real ADV firmware build;
- clean MiniFT8 ELF build/inspection;
- `git diff --check`.

Report resident firmware/SRAM and MiniFT8 ELF deltas.

No Audio/DSP/Radio changes are expected.

## Hardware acceptance

On ADV:

1. launch MiniFT8 with QMX;
2. verify 2-pixel separator under top row is WHITE while receiving/idle;
3. observe RX screen:
   - addressed-to-me row RED;
   - CQ row GREEN;
   - ordinary row WHITE;
4. start a real FT8 transmission and verify separator turns RED only during
   active TX;
5. verify separator returns WHITE after TX completes and RX resumes;
6. verify retained RX row colors do not change merely because TX is active;
7. confirm selection, decode, QMX TX/RX, logging and Ctrl+C remain normal.

Return exact implementation SHA and evidence.

## Implementation handoff

Task branch baseline: `d836a57b269aab7fad0f18d047ccb9d7c8859081`, containing
production baseline `c7ccd911ef79bc5c0f05b7cccba30fe128dc0def`.
Branch: `codex/T049-ft8-color-status`. Status: **REVIEW**.

### Architect clarification: ELF gate

The architect explicitly directed: **“Use resident ADV build; ELF gate N/A.”**
MiniFT8 is deliberately resident in this baseline and has no external ELF target,
as documented in `platform/adv/README.md`. No external FT8 port was added.
The real ADV firmware build is the FT8 target gate; separate MiniFT8 ELF size and
import inspection are **not applicable**. This is the only task-gate adjustment.

### Implementation summary / files changed

- `include/minishell/api.h` adds generic RED=8 and widens the foreground mask to
  14, preserving DEFAULT/WHITE/GREEN/CYAN and INVERSE values. No API version or
  table/function change. `core/minishell_services/display_service.c` accepts
  RED plus INVERSE and rejects reserved foreground selectors and unknown bits.
- `platform/adv/adv_display.cpp` maps RED to `0xFF0000` and validates separator
  colors. Existing RGB mappings, inverse, black background and geometry remain.
- `apps/ft8/include/ft8/app_types.h` adds bounded factual RX categories and
  physical `tx_active` to UiModel, plus row/separator semantic colors to UiFrame.
- `apps/ft8/src/app_controller/app_controller.c` projects text and category from
  the same ordered factual RxMessage, with to-me precedence over CQ. Physical
  state comes directly from `app_controller_tx_active()`.
- `apps/ft8/src/ui_shell/ui_shell.c` defaults all text to white, colors only RX
  body rows by category, and requests the global white/red top separator. The
  numeric row prefix receives its row's color. Text rendering is unchanged.
- `apps/ft8/main/ft8_ui_adapter.c` discovers optional styling by capability,
  struct size and callback, maps frame colors to generic attributes, and restores
  the requested separator after each clear. Missing or UNSUPPORTED styling falls
  back to plain text/no separator; ordinary errors retain existing failure policy.
- `tests/unit/test_display.c`, `tests/adv_display_color_test.py`,
  `tests/ft8_physical_tx_test.c`, `tests/ft8_ui_smoke.c`,
  `tests/ft8_color_adapter_test.c` and `CMakeLists.txt` provide generic palette,
  physical-state/projection, UI/paging and optional-provider regression coverage.
- `docs/api/display-api.md`, `docs/MiniFT8/ui.md` and this task packet document
  the generic extension, presentation policy and validation evidence.

### Behavior / invariants preserved

No decoder/DSP, factual RX classification, ordering/SNR, selection, retained-row
lifetime, AutoSeq, CAT/radio timing, 79-tone scheduler, logging, band sync, QSO
view, Audio or Mini-CW changes. No FT8 policy names are added to the provider.
Linux still advertises neither optional styling capability and remains plain.
The existing full-frame comparison detects color/separator transitions without
changing FT8's event loop. Top-row text stays white on every screen.

### Tests run and results

```sh
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
# PASS: 88/88
cmake -S tests/unit -B /tmp/T049-unit
cmake --build /tmp/T049-unit -j8
ctest --test-dir /tmp/T049-unit --output-on-failure
# PASS: 28/28
ctest --test-dir build-linux -R 'ft8|display|architecture|boundary|minicw' --output-on-failure
# PASS: 52/52
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: sequential baseline and implementation builds
xtensa-esp32s3-elf-size -A platform/adv/build/minishell_adv.elf
git diff --check
# PASS
```

The ordered-batch regression uses real RxResultBuilder output and verifies
text/category alignment, to-me precedence when both flags are present, all
50 projected CQ rows and retained categories across stream reset. Physical-TX
regressions check queued/pending false, successful begin true, completion false
and TX failure false in the model. UI tests cover row prefixes, paging, white
header, global separator and unchanged RX colors across physical TX state.
Non-RX rows remain white.

Adapter tests cover full styling, color-only, separator-only, ordinary plain,
UNSUPPORTED callbacks, and a legacy text-table prefix even with capability bits
present. Each render reestablishes its separator after clear, and shutdown clears
it. Generic and executable ADV tests preserve numeric palette values and inverse,
verify RED/WHITE/GREEN/CYAN RGB, separator y=19/height=2 and console reset.
Existing Mini-CW color/audio/transcript regressions pass unchanged.

Additional differential validation compiled baseline `ui_shell.c` from
`c7ccd911ef79bc5c0f05b7cccba30fe128dc0def` under renamed entry points alongside
the current renderer using the same input structures. `/tmp/T049-text` compared
**6,720 frames** across both presentations, all five screens, all 16 submenu
values, three pages, seven selections and both physical-TX states. Every text
byte, row/column count and footer flag matched. Existing exact-header tests pass.

### Resource evidence

ESP-IDF v5.5.4 / Xtensa GCC 14.2.0. Baseline and final firmware measurements were
rerun sequentially under identical build metadata; an initial overlapping build
was not used for the baseline resource comparison.

| Resident measure | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Firmware BIN bytes | 1,381,568 | 1,382,016 | +448 |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,016 | 27,016 | 0 |
| `.dram0.bss` | 40,336 | 40,336 | 0 |
| Static internal SRAM | 131,311 | 131,311 | **0** |
| `.flash.text` | 1,052,262 | 1,052,698 | +436 |
| `.flash.rodata` | 236,788 | 236,788 | 0 |

Xtensa compile-time size probes (`/tmp/T049-size-before.o` and
`/tmp/T049-size-after.o`) report:

| FT8 presentation object | Before | After | Delta |
| --- | ---: | ---: | ---: |
| UiModel | 2,848 | 3,048 | +200 |
| UiFrame | 260 | 300 | +40 |

The main function retains one model and two frames: **+280 bytes** of bounded
object payload on its existing foreground stack. This is not an on-device stack
high-water measurement. No stack configuration, DSP profile, dynamic allocation,
Audio buffer, worker or USB ownership change. MiniFT8 external ELF delta/import
inspection: **N/A by architect direction**; FT8 is included in the resident
firmware figures above.

### Remaining validation / risks / commit reference

No PR and no hardware testing by Codex. Supervisor review precedes ADV/QMX
acceptance: white idle separator, physical-TX-only red separator, correct RX
row colors, retained rows, selection and normal RX/TX/logging/quit behavior.
Host tests establish attributes/geometry and state projection, not physical TFT
appearance or radio acceptance. No new known limitation beyond optional-provider
monochrome fallback and the measured stack payload increase.

Commit reference: the single implementation commit containing this evidence;
exact SHA returned after push. Task status: **REVIEW**.


## Supervisor review

Reviewed implementation commit:

```text
c908d8d08e8cb317b7fa3119e9af94abe80e07f2
```

No software blocker found.

The generic Display extension preserves the existing DEFAULT/WHITE/GREEN/CYAN
numeric values, adds RED without an API table/version change, rejects reserved
foreground selectors at the MiniShell service boundary, and keeps ADV free of
FT8/CQ/reply semantics.

MiniFT8 projects existing factual `RxMessage.is_to_me` / `is_cq` metadata
through the same T028 ordered indexes as the displayed text. UiShell alone owns
the red/green/white presentation mapping. The entire numbered RX row receives
the message color; paging retains text/category alignment.

The separator state is driven by `app_controller_tx_active()`: queued or pending
TX remains white; only successful physical TX active state is red; completion
and failure return white. Because the new UiFrame metadata participates in the
existing full-frame comparison, separator-only state transitions still cause a
render even when text bytes are unchanged.

The adapter treats color and separator support as optional and falls back to the
pre-T049 monochrome text path. The reported 6,720-frame differential text check,
full regression gates, zero static-SRAM delta and unchanged Audio/DSP/Radio/Mini-CW
scope are consistent with the reviewed diff.

T049 is ready for ADV/QMX hardware acceptance.


## Final hardware acceptance — 2026-09-21

ADV/QMX validation accepted.

Observed on hardware:

- the 2-pixel separator is WHITE while idle/receiving;
- it turns RED during physical FT8 transmission and returns afterward;
- CQ RX rows render GREEN;
- ordinary operation remains normal.

Reply-to-me RED was not separately captured in the final on-air check, but the
same reviewed color path is covered by the factual `is_to_me` projection and
hardware-proven RED rendering used by the TX separator. Keep this as
software-proven / opportunistic-on-air confirmation rather than a remaining task
blocker.

Implementation:

```text
c908d8d08e8cb317b7fa3119e9af94abe80e07f2
```

T049 is COMPLETE.
