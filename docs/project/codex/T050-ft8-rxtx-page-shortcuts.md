# T050 — MiniFT8 RX/TX plain page shortcuts

Status: READY

## Baseline

Start from current production main:

```text
main
fc7a54b1798d0431ee250a6fa314fa6c3867b9cc
```

Branch:

```text
codex/T050-ft8-rxtx-page-shortcuts
```

No PR. No hardware testing by Codex.

## Objective

Add convenient plain-character page navigation for the two frequently used
MiniFT8 list screens:

```text
RX top-level:  ; = previous page, . = next page
TX top-level:  ; = previous page, . = next page
```

Existing special-key navigation remains unchanged:

```text
Up / PageUp       previous page
Down / PageDown   next page
```

On Cardputer ADV those special keys may require Fn; the new plain `;` / `.`
shortcuts avoid that for RX/TX.

## Scope

Implement this in `ui_shell_handle_input()` only.

When `input.type == UI_INPUT_CHAR`:

- if current screen is `SCREEN_RX` or `SCREEN_TX`;
- and `ui->submenu == UI_SUBMENU_NONE`;
- `;` calls the same page movement as previous-page navigation;
- `.` calls the same page movement as next-page navigation;
- preserve existing wraparound semantics;
- reset `selected_line` exactly as `move_page()` already does;
- consume the character and return no AppAction.

Do not remap the keys in `ft8_ui_adapter`; they should remain ordinary
character input so screen policy stays in UiShell.

## Explicit non-behavior

Plain `;` / `.` must **not** become page shortcuts on:

- O screen;
- S screen;
- V screen;
- V -> QSO;
- any submenu/editor.

Those less-frequent screens keep their existing Fn/special-key navigation only.

This also avoids stealing `;` / `.` from future text/numeric editors outside
RX/TX.

## Existing behavior to preserve

Do not change:

- R/T/O/S/V screen-switch keys;
- 1..6 selection/action behavior;
- Up/Down/PageUp/PageDown behavior;
- V -> QSO paging and `APP_ACTION_LOAD_QSO_PAGE`;
- RX ordering/colors/lifetime;
- TX queue semantics;
- T049 separator/colors;
- AutoSeq, CAT, TX timing, logging, decoder/DSP;
- MiniShell Input API.

If RX/TX has one page, `;` / `.` simply remain on page 1, matching
`move_page()`.

## UI help text

Update the RX and TX footer/help strings so the shortcuts are discoverable,
without changing the 20x7 ADV geometry.

Use concise wording that fits the existing presentation. Exact wording may be
chosen by Codex, but both `;` and `.` must be visible or clearly documented
in `docs/MiniFT8/ui.md`.

Desktop text may use the same footer if it remains readable.

## Tests

Add focused UiShell tests proving:

1. RX with >6 rows:
   - `.` moves page 1 -> 2;
   - `;` moves page 2 -> 1;
   - wraparound works in both directions;
   - selected line resets to 0.
2. TX with >6 rows has identical behavior.
3. RX/TX with one page remain at page 0.
4. O/S/V top level ignore plain `;` / `.`.
5. V -> QSO ignores plain `;` / `.`; existing special PageUp/PageDown still
   emits `APP_ACTION_LOAD_QSO_PAGE`.
6. Existing Up/Down/PageUp/PageDown RX/TX navigation still passes unchanged.
7. R/T/O/S/V and 1..6 input behavior remains unchanged.
8. T049 color/separator frame metadata stays unchanged except for footer text if
   intentionally updated.

Run:
- full Linux CTest;
- portable units;
- focused FT8/UI regression;
- real ADV firmware build;
- `git diff --check`.

Resident resource impact should be negligible; report actual firmware/SRAM delta.

## Hardware acceptance

On ADV:

1. RX screen with multiple pages: press `.` to go forward and `;` to go back;
2. verify wraparound;
3. TX screen with multiple pages: same behavior;
4. confirm no Fn is required for these two shortcuts;
5. verify S and V still require their existing special/Fn navigation;
6. confirm RX colors, TX red separator, QMX operation and Ctrl+C remain normal.

Return exact implementation SHA and evidence.