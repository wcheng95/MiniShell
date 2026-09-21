# T050 — MiniFT8 RX/TX plain page shortcuts

Status: TESTING

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

## Implementation handoff

Implemented on task branch baseline `59a01f2a0a6ac2bfd6d1d4bf1935b07f9cecab1a`
(production baseline `fc7a54b1798d0431ee250a6fa314fa6c3867b9cc`).

### Implementation summary / files changed

- `apps/ft8/src/ui_shell/ui_shell.c`: top-level RX/TX character handling
  delegates `;` / `.` to existing `move_page()` and returns no AppAction.
  RX/TX desktop footers now include `;/. page` within 30 columns.
- `tests/ft8_ui_smoke.c`: exercises both presentation profiles and all pages
  of 13-row RX/TX lists, both wrap directions, selected-line reset, all four
  special navigation inputs, one-page behavior, other screens and every
  submenu, screen switching, and page-relative 1..6 actions. Frames from
  plain and special navigation compare identically, including T049 metadata.
  Existing QSO special paging/action and color regressions remain in place.
- `docs/MiniFT8/ui.md`: documents shortcut direction, scope and unchanged
  ADV geometry. This task file records validation and review status.

### Behavior / invariants preserved

No adapter remapping or API changes. RX/TX use exactly the existing page
movement semantics, including retaining selection on a single page. O/S/V
and submenus ignore these plain characters. Existing radio, DSP, AutoSeq,
CAT, TX queue, logging, color/separator and screen-selection behavior remain
unchanged. No deviations from the task.

### Tests run and results

```sh
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
ctest --test-dir build-linux -R '^linux_serial_unit$' --output-on-failure
ctest --test-dir build-linux --output-on-failure
ctest --test-dir build-linux -R 'ft8|display|architecture|boundary|minicw' --output-on-failure
cmake -S tests/unit -B /tmp/T050-unit
cmake --build /tmp/T050-unit -j8
ctest --test-dir /tmp/T050-unit --output-on-failure
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
xtensa-esp32s3-elf-size -A platform/adv/build/minishell_adv.elf
git diff --check
```

- Linux configure/build passed. First full CTest: 87/88, with the previously
  reported `linux_serial_unit` line-67 timeout assertion. Isolated retry
  passed 1/1; full rerun passed **88/88**. No Serial code/test changes.
- Focused FT8/UI, display, MiniCW and architecture/boundary regressions:
  **52/52 passed**.
- Portable units: **28/28 passed**.
- Real ADV firmware: baseline and implementation builds passed with ESP-IDF
  v5.5.4. No flashing or hardware tests performed.
- `git diff --check`: passed.

### Resident resource evidence

Measured sequential baseline and implementation builds in the same checkout,
with the baseline build completed before production edits (bytes):

| Resource | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Firmware BIN | 1,382,016 | 1,382,096 | +80 |
| `.flash.text` | 1,052,698 | 1,052,778 | +80 |
| `.flash.rodata` | 236,788 | 236,788 | 0 |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,016 | 27,016 | 0 |
| `.dram0.bss` | 40,336 | 40,336 | 0 |
| Sum of measured static internal SRAM sections | 131,311 | 131,311 | 0 |

No new persistent state or allocation. FT8 remains resident; no external ELF
port is introduced.

### Remaining validation / risks / commit reference

Supervisor review and the ADV hardware acceptance checklist above remain
pending. The pre-existing intermittent Linux Serial timeout test remains a
validation limitation; it passed on retry. No known shortcut-specific issue.
This handoff is included in the single implementation commit on
`codex/T050-ft8-rxtx-page-shortcuts`; the exact SHA is returned after push.


## Supervisor review

Reviewed implementation commit:

```text
967a7259f57aed64ba43297f2386481eb1e7a183
```

No software blocker found.

The implementation is intentionally narrow: only top-level RX/TX plain-character
input recognizes `;` / `.`, and both delegate to the existing `move_page()`
helper. Existing wraparound and selected-line behavior therefore stay identical
to special Up/Down/PageUp/PageDown navigation.

O/S/V, V->QSO and every submenu ignore the new plain characters; no Input API or
adapter mapping changed. Existing R/T/O/S/V switching, 1..6 actions, T049
color/separator metadata and FT8 radio/DSP/AutoSeq behavior are untouched.

The reported intermittent `linux_serial_unit` failure is pre-existing and
unrelated to this diff; isolated retry and the full rerun passed.

T050 is ready for brief ADV hardware acceptance.
