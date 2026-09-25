# T077 — ADV blinking resident edit cursor

Status: READY

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

- [ ] Empty prompt shows a blinking inverse blank cell after `M$> `.
- [ ] Cursor inside text inverts the underlying character.
- [ ] Cursor at end of text inverts the following blank cell.
- [ ] Cursor position tracks Fn+, / Fn+/ movement.
- [ ] History recall/draft restoration moves the cursor to the core editor position.
- [ ] Any successful edit makes cursor immediately visible and restarts blink.
- [ ] Cursor toggles roughly every 500 ms while idle.
- [ ] Blink produces no retained-history mutation.
- [ ] Blink produces no USB output.
- [ ] Wrapped-line cursor coordinates are correct at 20-column boundaries.
- [ ] Cursor remains visible while traversing commands longer than seven display rows.
- [ ] Moving back toward command end returns to the live-tail edit view.
- [ ] Ctrl scrollback hides cursor from historical output.
- [ ] Cursor resumes on the live edit view.
- [ ] Enter/app handoff leaves no stale cursor overlay.
- [ ] T076 key mapping remains unchanged.
- [ ] Shared editor/history remains unchanged.
- [ ] Linux behavior remains unchanged.
- [ ] Public API remains unchanged.
- [ ] Full Linux CTest passes.
- [ ] ADV firmware builds successfully.

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

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and test evidence.

## Architect test result

Record ADV cursor validation here.