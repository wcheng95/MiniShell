# T076 — ADV shell history/cursor/scrollback key remap

Status: REVIEW

## Architect intent

T075's shared history/editor works, but the initial ADV key assignment was wrong:
using Fn+, / Fn+/ for history and Fn+; / Fn+. for scrollback left no practical
cursor-left/right control inside a recalled command.

Correct ADV resident-shell mapping:

```text
Ctrl + ;    console scroll up 5 rows
Ctrl + .    console scroll down 5 rows

Fn + ;      previous command
Fn + .      next command

Fn + ,      cursor left
Fn + /      cursor right

, / ; .     ordinary printable characters without modifiers
```

Linux behavior is unchanged:

```text
Up / Down      history previous / next
Left / Right   cursor left / right
PgUp / PgDn    unchanged terminal behavior; not command history
```

## Objective

Correct only the ADV resident-shell key consumption introduced by T075.

Do not redesign the history engine, editor storage, keyboard matrix, public API,
or console scrollback implementation.

## Current event mapping

`platform/adv/adv_keyboard.cpp` already normalizes the physical chords correctly:

```text
Fn + ;  -> MINI_KEY_UP    + MINI_MOD_FN
Fn + ,  -> MINI_KEY_LEFT  + MINI_MOD_FN
Fn + .  -> MINI_KEY_DOWN  + MINI_MOD_FN
Fn + /  -> MINI_KEY_RIGHT + MINI_MOD_FN

Ctrl + ; -> MINI_KEY_EVENT_CHAR ';' + MINI_MOD_CTRL
Ctrl + . -> MINI_KEY_EVENT_CHAR '.' + MINI_MOD_CTRL
```

Therefore **do not change `adv_keyboard.cpp`** unless an actual hardware mapping
bug is discovered. T076 should remap these existing events in the resident ADV
console/editor path.

## Required behavior

In `platform/adv/adv_console.c` resident line editing:

### Ctrl scrollback

Intercept character events carrying `MINI_MOD_CTRL` for exactly:

```text
';' -> adv_display_console_scroll(+5)
'.' -> adv_display_console_scroll(-5)
```

These chords must not insert `;` or `.` into the command line.

Do not broadly reinterpret other Ctrl+character combinations in this task.

### Fn history

```text
Fn + MINI_KEY_UP    -> SHELL_EDIT_PREVIOUS
Fn + MINI_KEY_DOWN  -> SHELL_EDIT_NEXT
```

That corresponds physically to Fn+; and Fn+.

### Fn cursor movement

```text
Fn + MINI_KEY_LEFT  -> SHELL_EDIT_LEFT
Fn + MINI_KEY_RIGHT -> SHELL_EDIT_RIGHT
```

That corresponds physically to Fn+, and Fn+/.

Cursor movement must redraw only as needed and must not navigate history.

### Bare punctuation

Unmodified:

```text
, / ; .
```

remain normal printable input.

## Editing example

Given recalled command:

```text
cp foo.txt /sd
```

the user must be able to:

1. `Fn+;` recall it;
2. use repeated `Fn+,` to move left into `foo.txt`;
3. edit with Backspace/Delete/typing;
4. use `Fn+/` to move right;
5. press Enter to execute the edited command.

## Scrollback separation

Output scrollback remains the existing 50 physical rows and five-row step.

Only its trigger changes:

```text
old: Fn+; / Fn+.
new: Ctrl+; / Ctrl+.
```

History and cursor actions must not themselves change the retained output history.

Typing/editing may continue to return the view to the live tail as in T075.

## Architectural constraints

- Preserve the T075 shared `shell_editor_t` implementation unchanged unless a real
  editor bug is found.
- Preserve `include/minishell/api.h` and API v3.
- Preserve `adv_keyboard.cpp` byte-for-byte if possible.
- No persisted state.
- No heap allocation.
- No Linux behavior change.
- No change to 10-command capacity or draft semantics.
- No change to 50-row ADV output scrollback depth or five-row step.
- No new physical key chords beyond the architect-approved mapping above.

## Non-goals

Do not implement:

- command completion;
- pathname completion;
- Tab choices;
- persistent history;
- Ctrl-R/history search;
- Home/End chords on ADV;
- new Linux scrollback;
- `clear`;
- shell syntax changes.

## Acceptance criteria

- [x] Ctrl+; scrolls ADV console output up by five rows.
- [x] Ctrl+. scrolls ADV console output down by five rows.
- [x] Ctrl+;/Ctrl+. do not insert punctuation into the command.
- [x] Fn+; recalls the previous history entry.
- [x] Fn+. recalls the next history entry/draft.
- [x] Fn+, moves the edit cursor left.
- [x] Fn+/ moves the edit cursor right.
- [x] Fn+,/Fn+/ do not navigate history.
- [x] Bare `,`, `/`, `;`, `.` remain printable.
- [x] Recalled commands can be edited in the middle on ADV.
- [x] Draft restoration still works.
- [x] 10-entry history behavior is unchanged.
- [x] ADV wrapped-line redraw remains correct.
- [x] Linux history/edit behavior is unchanged.
- [x] `adv_keyboard.cpp` remains unchanged.
- [x] Public MiniShell API remains unchanged.
- [x] Full Linux CTest passes.
- [x] ADV firmware builds successfully.

## Automated tests

Update `tests/adv_console_scrollback_test.py` to exercise the exact event layer:

```text
Ctrl CHAR ';' -> scroll +5, editor unchanged
Ctrl CHAR '.' -> scroll -5, editor unchanged
Fn+UP         -> previous history
Fn+DOWN       -> next history
Fn+LEFT       -> cursor left
Fn+RIGHT      -> cursor right
bare ,/;.     -> inserted characters
```

Include a recalled-command middle edit, for example transform:

```text
cp foo.txt /sd
```

into another valid line by using Fn+Left, Backspace/typing, then Fn+Right.

Retain T075 tests for:

- draft restoration;
- wrapped 255-character redraw;
- scrollback history integrity;
- history capacity/eviction.

Add/check a source-boundary assertion that `platform/adv/adv_keyboard.cpp` did not
change if practical.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T076-unit
cmake --build /tmp/T076-unit -j"$(nproc)"
ctest --test-dir /tmp/T076-unit --output-on-failure

idf.py -C platform/adv build
git diff --check
```

## Manual / hardware validation

On ADV:

1. Enter several commands.
2. `Fn+;` -> previous command.
3. `Fn+.` -> next command.
4. Recall a command and use `Fn+,` / `Fn+/` to move the cursor into its middle.
5. Edit and execute it.
6. Generate enough output for scrollback; use `Ctrl+;` / `Ctrl+.` to scroll.
7. Verify bare `, / ; .` still type normally.

Also verify Linux Up/Down/Left/Right still behave exactly as T075.

No QMX/RF validation is required.

## Codex branch / handoff

Work on:

```text
codex/T076-adv-shell-key-remap
```

Start from current `main`.

Read T075 and this task before editing. Keep T076 to one small reviewable commit.
Do not merge to `main` and do not open a PR unless asked.

## Codex implementation notes

### Implementation summary

Implemented from current `origin/main` at
`9239ab171f397343461bda52ccf8ccf3cee14603` on the requested branch.
No scope or architecture deviations.

Only the ADV resident event consumer changes: Ctrl character events for `;` and
`.` call the existing scroll function with +5/-5 and are consumed before text
insertion. Other Ctrl characters retain their existing handling. Existing Fn
Up/Down events invoke shared Previous/Next actions; Fn Left/Right invoke shared
cursor Left/Right actions. Redraw remains gated by the shared editor's change
result, so movement at a cursor boundary does not redraw or emit USB output.

### Files changed

- `platform/adv/adv_console.c`: remap only `accept_key_event()`.
- `tests/adv_console_scrollback_test.py`: exact Ctrl character and Fn special
  event coverage, recalled middle editing/submission, cursor boundaries and
  unchanged navigation/history, draft cursor restoration, and wrapped movement.
- `README.md`, `docs/README.md`, `docs/api/console-api.md`,
  `platform/adv/README.md`: update current control descriptions.
- This packet: implementation and validation evidence.

### Invariants preserved

The shared editor, ten-entry storage/eviction/draft behavior, shell dispatcher,
Linux backend, public API v3, ADV keyboard normalization, and ADV display/output
history renderer are unchanged. The 50-row ring and five-row scroll operation
are unchanged; only scroll triggers move to Ctrl punctuation. No heap allocation,
persistence, new key chord, shell syntax, task or service changes.

Source-boundary check against the base returned no differences:

```bash
git diff --exit-code origin/main -- platform/adv/adv_keyboard.cpp \
  core/shell_editor.c core/shell_editor.h platform/linux \
  include/minishell/api.h platform/adv/adv_display.cpp
```

### Local tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R 'adv_console_scrollback|shell_editor|linux_shell_history'
# PASS: 3/3.
cmake -S tests/unit -B /tmp/T076-unit
cmake --build /tmp/T076-unit -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir /tmp/T076-unit --output-on-failure
# PASS: 28/28.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: 125/125 (59.30 seconds), without retries.
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: firmware 0x153040 bytes; app partition 78% free. No flashing.
git diff --check
# PASS
```

ADV host tests retain the original output ring, scroll clamping, Display handoff,
USB input, 255-character wrapped recall and draft restoration assertions. The
old chord expectations are replaced narrowly by the architect-approved mapping.
The host fixture now resets the console prompt-position flag along with display
state; the added bare-key case exposed that missing fixture reset. No product
reset or rendering behavior changed. The new event test transforms `cp foo.txt /sd` into `cp for.Txt /sd` using
Fn history/cursor keys, Backspace, Delete and typing, then submits it through
the real line reader. It verifies the original stored command is unchanged.
Bare punctuation is checked through both physical character events and USB
input. Ctrl scrollback preserves the entire editor state and emits no USB text; other
Ctrl characters retain text behavior. Cursor movement across wrapped rows leaves
retained text and history navigation unchanged. Existing pure editor tests retain
ten-entry eviction and duplicate coverage, and the unchanged Linux PTY suite
checks history, cursor editing and terminal handoffs.

### Manual/hardware validation still required

After review, run the ADV checklist above: Fn+`;` / Fn+`.` history, Fn+`,` /
Fn+`/` movement inside a recall, middle edits/Enter, Ctrl+`;` / Ctrl+`.` output
scrollback, and bare punctuation. Confirm wrapped-line usability and Linux keys
on the actual hosts. No device flashed or operated; no QMX/RF validation needed.

### Known limitations / risks

This task changes event consumption only. It retains T075's redraw/presentation
and USB preview behavior; no new cursor indicator or renderer behavior is added.
Host tests use actual event/edit/render code with hardware transport stubs, so
physical modifier delivery and operator usability still require ADV acceptance.

### Commit

One implementation commit titled `T076: remap ADV shell history and cursor keys`,
parent `9239ab171f397343461bda52ccf8ccf3cee14603`, on
`codex/T076-adv-shell-key-remap`. Exact pushed SHA is returned in the handoff.
No merge or PR.

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and test evidence.

## Architect test result

Record ADV remap validation here.