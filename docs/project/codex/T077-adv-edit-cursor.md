# T077 — ADV blinking resident edit cursor

Status: COMPLETE

## Architect intent

T075/T076 make resident command history genuinely editable on ADV. The remaining
usability problem is that the Cardputer display has no visible edit cursor, so
moving into the middle of a recalled command gives no indication of the active
insertion/deletion position.

Add a terminal-like blinking block cursor to the ADV resident command editor.

Visual contract:

- cursor inside text: invert the character under the cursor;
- cursor at end of line: invert the blank cell immediately after the text;
- blink approximately 500 ms visible / 500 ms hidden;
- any successful edit/history/cursor action immediately shows the cursor and
  restarts the blink phase;
- cursor exists only while the resident shell is actively editing a command.

Example:

```text
M$> cp foo.txt /sd
       █
```

When the cursor is on a character, that character remains visible with foreground
and background inverted rather than being replaced by a literal block glyph.

## Objective

Add an ADV-only visual edit cursor without changing:

- T075 shared history/editor semantics;
- T076 key mapping;
- Linux behavior;
- public MiniShell APIs;
- retained console text/history contents.

## Current context

Read before editing:

```text
AGENTS.md
docs/api/console-api.md
docs/project/codex/T075-shell-history.md
docs/project/codex/T076-adv-shell-key-remap.md
platform/adv/adv_console.c
platform/adv/adv_display.cpp
platform/adv/adv_internal.h
tests/adv_console_scrollback_test.py
core/shell_editor.c
core/shell_editor.h
```

Current T076 controls:

```text
Ctrl+; / Ctrl+.   output scrollback up/down
Fn+;   / Fn+.     history previous/next
Fn+,   / Fn+/     edit cursor left/right
```

The shared editor already owns `editor->cursor`; T077 must present that position
visually, not create another editing cursor state.

## Architecture decision

Keep responsibilities split:

### `adv_console.c`

Owns **cursor blink lifecycle/timing** because it already owns the resident shell
input loop and knows when command editing starts/ends.

### `adv_display.cpp`

Owns **cursor presentation** because it owns the 20x7 cell renderer, wrapped edit
view, retained console rows, and inverse-cell drawing.

The cursor is a temporary render overlay. It must not be encoded into:

- `s_history`;
- command text;
- public Display state;
- persisted data.

No new FreeRTOS task, worker, software timer, or heap allocation. Use the existing
5 ms resident input polling loop and an existing monotonic/tick source.

## Private display contract

It is acceptable to extend the private ADV console/display interface in
`adv_internal.h`. A clean shape is conceptually:

```c
adv_display_console_edit_begin();
adv_display_console_edit_line(line, cursor);
adv_display_console_edit_cursor(visible);
adv_display_console_edit_end();
```

Exact private names/signatures are implementation details, but the behavior must
remain ADV-private. Do not edit `include/minishell/api.h`.

`edit_line` should rebuild the editable prompt region using the existing snapshot
mechanism, calculate the visible cursor cell from the core editor cursor, and
show the cursor immediately.

`edit_cursor` should toggle only the cursor presentation if possible; do not grow
retained history or alter text.

`edit_end` (or equivalent lifecycle cleanup) must remove/invalidate the overlay
before leaving resident line editing.

## Cursor geometry

The prompt already occupies four cells:

```text
M$> 
```

The cursor position is based on the actual prompt-tail column captured when edit
mode begins, not a hard-coded assumption if the current renderer already exposes
a safer value.

For the normal prompt, conceptually:

```text
cell = 4 + editor->cursor
row_delta = cell / 20
column = cell % 20
```

Requirements:

- cursor 0 is the first command cell immediately after `M$> `;
- cursor on character N inverts that character cell;
- cursor == line length inverts the blank cell after the last character;
- exact 20-column boundaries place the end cursor at column 0 of the next row;
- wrapped commands up to the existing 255-character payload remain supported;
- no command-length reduction.

## Cursor-follow viewport

A 255-character command can occupy more than the seven visible ADV rows.

The cursor must remain visible while moving left/right through such a command.
Do not merely compute an inverse cell that may be outside the current viewport.

When necessary, shift the console edit viewport to include the cursor while
preserving the existing retained-row model. A reasonable policy is:

- if cursor lies within the current seven-row edit viewport, keep the view;
- when cursor moves above it, shift enough older rows into view to reveal it;
- as cursor moves back toward the end, eventually return to the normal live-tail
  view.

This viewport following is part of edit presentation only; it must not mutate
the 50-row retained history.

## Cursor appearance

Use the existing inverse-cell rendering concept:

```text
normal character: white on black
cursor-on character: black on white
cursor-on blank: black glyph/space on white cell (solid block appearance)
```

Do not store `MINI_TEXT_ATTR_INVERSE` into retained console history. Apply it
transiently while rendering the cursor cell.

If the underlying cell has a non-default foreground attribute, invert using that
cell's normal foreground/background rules rather than destroying its underlying
attribute.

## Blink behavior

Use approximately:

```text
500 ms visible
500 ms hidden
```

A small scheduling tolerance from the existing 5 ms polling loop is acceptable.

At edit start:

- cursor is immediately visible;
- blink deadline starts from that moment.

After any successful editor change:

- printable insertion;
- Backspace/Delete;
- Fn+, / Fn+/ cursor movement;
- Fn+; / Fn+. history navigation/draft restoration;

the cursor becomes immediately visible and its 500 ms visible interval restarts.

Actions that do not change editor state at a boundary (for example Fn+, at cursor
0) need not restart blink.

Ctrl+; / Ctrl+. output scrollback does not edit the command.

## Scrollback interaction

While the operator is viewing older output with Ctrl+;:

- do not draw the command cursor on unrelated historical rows;
- command/editor state remains unchanged.

When the view returns to the live edit region, the cursor may resume according to
its current blink state. Any subsequent edit action must immediately restore the
live edit view and show the cursor, consistent with T075/T076 behavior.

## Editing lifecycle

Cursor is visible/blinking only during `minishell_platform_console_read_line()`
resident ADV editing.

Hide/invalidate it before:

- Enter submission/newline;
- EOF/exit from line reader;
- foreground application ownership;
- any path that leaves resident editing.

The first prompt after an app returns starts a fresh cursor blink normally.

Foreground full-screen Display ownership remains unchanged.

## USB mirror

T077 is specifically the Cardputer TFT cursor.

Do not require a blinking cursor implementation for the ADV USB Serial/JTAG mirror.
The existing ANSI tail preview may remain unchanged. Do not emit periodic USB
bytes merely to blink the TFT cursor.

## Architectural constraints

- Preserve `include/minishell/api.h` byte-for-byte.
- Preserve `core/shell_editor.c/.h` unless an actual editor bug is discovered.
- Preserve `platform/adv/adv_keyboard.cpp` and T076 key mapping.
- Preserve Linux sources/behavior.
- Preserve 10-entry RAM history semantics.
- Preserve 50-row retained ADV console history.
- No new task/timer/thread.
- No heap allocation.
- Cursor overlay must not mutate retained text.
- Blink must not create console output or USB replay.

## Non-goals

Do not implement:

- insert/overwrite mode;
- cursor shape settings;
- cursor color settings;
- Home/End ADV chords;
- persistent cursor state;
- Linux cursor changes;
- command/path completion;
- Tab choice display;
- `clear`;
- changes to command syntax/history capacity.

## Acceptance criteria

- [x] Empty prompt shows a blinking inverse blank cell after `M$> `.
- [x] Cursor inside text inverts the underlying character.
- [x] Cursor at end of text inverts the following blank cell.
- [x] Cursor position tracks Fn+, / Fn+/ movement.
- [x] History recall/draft restoration moves the cursor to the core editor position.
- [x] Any successful edit makes cursor immediately visible and restarts blink.
- [x] Cursor toggles roughly every 500 ms while idle.
- [x] Blink produces no retained-history mutation.
- [x] Blink produces no USB output.
- [x] Wrapped-line cursor coordinates are correct at 20-column boundaries.
- [x] Cursor remains visible while traversing commands longer than seven display rows.
- [x] Moving back toward command end returns to the live-tail edit view.
- [x] Ctrl scrollback hides cursor from historical output.
- [x] Cursor resumes on the live edit view.
- [x] Enter/app handoff leaves no stale cursor overlay.
- [x] T076 key mapping remains unchanged.
- [x] Shared editor/history remains unchanged.
- [x] Linux behavior remains unchanged.
- [x] Public API remains unchanged.
- [x] Full Linux CTest passes.
- [x] ADV firmware builds successfully.

## Automated tests

Extend `tests/adv_console_scrollback_test.py` using the existing renderer/hardware
stubs. Cover at least:

1. empty-line cursor on blank cell after prompt;
2. cursor on first/middle/last text character;
3. end-of-line blank cursor;
4. cursor at a 20-column wrap boundary;
5. cursor left/right movement using T076 Fn events;
6. history recall and draft cursor restoration;
7. cursor visible immediately after edit;
8. deterministic blink visible -> hidden -> visible using a controllable/stubbed
   monotonic time source;
9. no USB bytes added by blink-only transitions;
10. retained `s_history` identical before/after blink-only transitions;
11. long (>7-row) command: moving cursor to early/middle/end positions keeps the
    cursor in the seven-row viewport;
12. Ctrl scrollback shows historical rows without cursor overlay;
13. returning to live edit plus a subsequent editor action restores cursor;
14. submission/edit-end removes overlay.

Prefer testing cursor overlay state separately from retained `s_attrs`/`s_history`
so the test proves it is transient rather than accidentally persisted.

Retain all T075/T076 history, wrapped-redraw, scrollback and punctuation tests.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T077-unit
cmake --build /tmp/T077-unit -j"$(nproc)"
ctest --test-dir /tmp/T077-unit --output-on-failure

idf.py -C platform/adv build
git diff --check
```

## Manual / hardware validation

On ADV:

1. At empty `M$>` verify a blinking white block appears after the prompt.
2. Type a command and verify the block follows the insertion point.
3. Recall a command with Fn+;.
4. Use Fn+, repeatedly to move into the middle; verify each character under the
   cursor is visibly inverted.
5. Edit the middle of the command and move right with Fn+/.
6. Leave the keyboard idle and confirm a comfortable blink around 1 Hz total
   cycle (about 500 ms on / 500 ms off).
7. Test a wrapped command and move the cursor across row boundaries.
8. If convenient, test a very long command spanning more than seven rows and
   verify the display follows the cursor.
9. Use Ctrl+; / Ctrl+. scrollback and verify no cursor appears on old output.
10. Enter the command / launch-return an app and verify no stale cursor remains.

No QMX/RF validation is required.

## Codex branch / handoff

Work on:

```text
codex/T077-adv-edit-cursor
```

Start from current `main`.

Read T075, T076 and this task before editing. Keep T077 to one reviewable commit.
Do not merge to `main` and do not open a PR unless asked.

## Codex implementation notes

### Implementation summary

Implemented the ADV resident inverse-cell cursor with a 500 ms visible / 500 ms
hidden phase driven by the existing 5 ms input loop and `adv_monotonic_us()`.
Successful editor actions immediately show it and restart the deadline. Enter
and EOF remove it before leaving the reader. Ordinary console output invalidates
an active overlay.

The display derives cursor geometry from the prompt snapshot and shared editor
position. It follows the cursor through wrapped commands while keeping the
existing 50-row ring and snapshot model. Manual output scrollback suppresses the
overlay; editing restores a viewport containing the cursor. Blink transitions
redraw only the affected cell and preserve underlying glyphs and attributes.
No task deviations.

### Files changed

- `platform/adv/adv_console.c`: cursor timing, reset and reader lifecycle.
- `platform/adv/adv_display.cpp`: transient inverse rendering, cursor geometry,
  viewport following and scrollback suppression.
- `platform/adv/adv_internal.h`: private edit position/visibility/end interface.
- `tests/adv_console_scrollback_test.py`: deterministic clock, blink/lifecycle,
  character/blank/attribute rendering, wrap and full-length cursor traversal tests.
- `docs/api/console-api.md`, `platform/adv/README.md`: resident cursor behavior.
- This packet: implementation and validation evidence.

### Invariants preserved

Public API, shared editor, ADV keyboard backend and Linux sources are unchanged
(`git diff --exit-code origin/main -- include/minishell/api.h core/shell_editor.c
core/shell_editor.h platform/linux platform/adv/adv_keyboard.cpp` passed).
T076 key translation is unchanged. Ten-entry RAM command history, 255-character
payload and 50-row retained console history remain intact. No heap allocation,
new task, timer or thread. Cursor overlay never writes retained text or attributes,
and blink-only transitions emit no USB bytes. Foreground Display ownership and
USB ANSI tail preview are preserved.

### Local tests run

- `PYTHONDONTWRITEBYTECODE=1 python3 tests/adv_console_scrollback_test.py`: passed,
  including existing T075/T076 regression cases.
- `cmake -S . -B build-linux`: passed.
- `cmake --build build-linux -j8`: passed.
- `PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure`:
  first run passed 124/125. The previously observed unrelated `linux_serial_unit`
  PTY saturation assertion at line 67 failed (`MINI_ERR_TIMEOUT && n == 0`).
- `PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux -R '^linux_serial_unit$'
  --output-on-failure`: passed 1/1 immediately afterward. No serial code or tests
  were changed.
- Full Linux CTest rerun with the same command: passed 125/125 without retries.
- `cmake -S tests/unit -B /tmp/T077-unit`: passed.
- `cmake --build /tmp/T077-unit -j8`: passed.
- `PYTHONDONTWRITEBYTECODE=1 ctest --test-dir /tmp/T077-unit --output-on-failure`:
  passed 28/28.
- `source /home/wei/projects/esp-idf/export.sh` followed by
  `idf.py -C platform/adv build`: passed. Firmware size `0x153340` bytes;
  application partition has 78% free. Existing SDK/dependency warnings remain.
- `git diff --check`: passed.

Acceptance boxes above reflect automated evidence; physical validation below
remains pending.

### Manual/hardware validation still required

Architect to run the ADV checklist above: physical blink readability/timing,
Fn navigation and middle edits, row wrapping and long-command viewport following,
Ctrl scrollback, and launch/return cleanup. No device was flashed. No QMX/RF
validation is required.

### Known limitations / risks

Hardware rendering and keyboard feel have only host-stub coverage until ADV
validation. Blink timing inherits the resident polling loop's scheduling delay.
The unrelated Linux PTY timeout test is intermittent, as recorded above.

### Commit

One commit on `codex/T077-adv-edit-cursor`, titled
`T077: show blinking ADV shell edit cursor`, based on current main
`f146b21f226b43200ef1efce3458329d1edcbf82`. The exact implementation SHA is provided
in the Codex handoff (this packet is included in that commit).

## Supervisor review

Reviewed `main..50874626a20cace05a516e2c2f8d2fbed40a872f` against T077,
the accepted T075/T076 editor/keymap behavior, and the current ADV console
retention/display ownership.

Result: **PASS for software review; advanced to TESTING.**

Findings:

- exactly one bounded implementation commit, one commit ahead of the T077 task baseline;
- blink timing is owned by the existing resident ADV input loop and uses
  `adv_monotonic_us()`; no task, timer, worker or heap allocation was added;
- the display owns only transient cursor presentation and cursor-follow viewport;
  core history/editor state remains the single semantic cursor owner;
- cursor rendering XORs the temporary overlay with the underlying inverse
  attribute, so colored/inverse cells retain their original attribute state;
- cursor blink toggles only the affected TFT cell and emits no USB traffic;
- edit actions rebuild the line from the existing snapshot, force the cursor
  visible, and restart the 500 ms phase;
- end-of-line and exact 20-column boundary geometry are covered;
- long-command viewport math accounts for temporary retained-ring eviction and
  keeps every cursor position in the seven-row viewport without mutating the
  50-row retained history;
- manual output scrollback suppresses the overlay; the next edit restores a view
  containing the insertion point;
- Enter/EOF call the edit-end path before leaving resident input, so foreground
  app Display ownership cannot inherit a stale cursor;
- public API is byte-for-byte unchanged
  (`13ce3b15fb5b4e1b0047d9559eb9aca91e6312a0`);
- shared editor/history is byte-for-byte unchanged:
  - `shell_editor.c`: `b17fa770aa9c4bba2702b30a6eabbc1a5cf61cff`
  - `shell_editor.h`: `a4f15ffc468eb5fc16942cd648588cfff99749b9`;
- ADV keyboard/T076 mapping is byte-for-byte unchanged
  (`5afceeb97b3fee479b2a30238286dc0083607b44`);
- Linux resident editor path is byte-for-byte unchanged
  (`5b2bb83337c8509009ee76ecc037f9b66caca1bf`).

Accepted local evidence:

```text
ADV console/renderer host test: PASS
final Linux CTest rerun: 125/125 PASS
portable unit tests: 28/28 PASS
ADV ESP-IDF build: PASS
git diff --check: PASS
```

The initial `linux_serial_unit` PTY saturation failure is the previously
documented intermittent serial flake; serial implementation/tests were unchanged
and the focused retry plus final full rerun passed.

No blocking review finding. `main` was fast-forwarded to
`50874626a20cace05a516e2c2f8d2fbed40a872f`.

Remaining gate: ADV hardware validation of cursor readability/blink timing,
Fn-based middle editing, wrapped/long command following, Ctrl scrollback
suppression, and clean cursor removal on Enter/app handoff. No QMX/RF validation
is required.

## Architect test result

ADV hardware validation passed on 2026-09-24.

Verified the blinking inverse-cell cursor on the real Cardputer display, including
middle-of-command movement/editing with the T076 Fn cursor chords and normal
resident-shell lifecycle.

Result: **PASS. T077 COMPLETE.**

The physical test also confirms the dependent ADV T076 control mapping remains
usable with the cursor presentation enabled.