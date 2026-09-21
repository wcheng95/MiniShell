# T049 — MiniFT8 TX separator + RX message colors

Status: READY

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