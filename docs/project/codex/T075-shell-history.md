# T075 — 10-command editable resident shell history

Status: READY

## Architect intent

Add a small RAM-only command history that is practical on both Cardputer ADV and
Linux without stealing the existing console-scrollback shortcuts.

Accepted key mapping:

```text
ADV physical keyboard
  Fn + ,    previous command     (keyboard event: Fn + Left)
  Fn + /    next command         (keyboard event: Fn + Right)
  Fn + ;    console scroll up    (unchanged)
  Fn + .    console scroll down  (unchanged)
  , / ; .   ordinary characters when Fn is not held

Linux interactive terminal
  Up        previous command
  Down      next command
  Left      cursor left
  Right     cursor right
  PgUp/PgDn are NOT command-history keys
```

The mapping is intentionally asymmetric on ADV because there are no bare
dedicated arrow keys. `Fn+,` / `Fn+/` are acceptable history controls;
`Fn+;` / `Fn+.` remain reserved for the existing 50-row console scrollback.

## Objective

Implement a resident editable command-history line editor with:

- 10 history entries;
- RAM-only session lifetime;
- previous/next navigation;
- recalled command editing before Enter;
- draft restoration after navigating back past the newest history entry;
- Linux Up/Down bindings;
- ADV `Fn+,` / `Fn+/` bindings;
- no public MiniShell API changes.

## Current context

Read before editing:

```text
AGENTS.md
README.md
docs/README.md
docs/project/progress.md
docs/project/codex/T051-*.md
docs/project/codex/T072-shell-cwd-relative-paths.md
docs/project/codex/T073-natural-ls-cd-defaults.md
core/shell.c
core/platform_backend.h
platform/adv/adv_console.c
platform/adv/adv_display.cpp
platform/adv/adv_keyboard.cpp
platform/linux/linux_console.c
platform/linux/linux_terminal.c
platform/linux/linux_terminal_parser.c
platform/linux/linux_terminal_parser.h
tests/adv_console_scrollback_test.py
tests/shell_alias_test.c
```

Important current behavior:

- shell command lines are bounded by `SHELL_LINE_MAX == 256`;
- the resident shell currently writes `M$> ` then calls the private
  `minishell_platform_console_read_line()`;
- ADV already has a physical-key event path and a simple append/backspace line editor;
- ADV keyboard normalization already maps:

```text
Fn + ; -> MINI_KEY_UP    + MINI_MOD_FN
Fn + , -> MINI_KEY_LEFT  + MINI_MOD_FN
Fn + . -> MINI_KEY_DOWN  + MINI_MOD_FN
Fn + / -> MINI_KEY_RIGHT + MINI_MOD_FN
```

- ADV `Fn+Up/Down` currently drives five-row console scrollback;
- Linux resident shell currently uses `fgets()` in canonical mode;
- Linux already has a tested raw terminal parser for application Input events,
  including Up/Down/Left/Right/Home/End/Delete/PageUp/PageDown;
- foreground applications and the resident shell share the terminal but have
  separate lifecycle phases.

## Architecture decision

History semantics belong to the **resident shell/core**, not independently to
the Linux and ADV platform backends.

Key decoding and physical redraw mechanics remain platform-private.

Implement one shared bounded resident line-editor/history engine, or an equivalent
core-owned module, and keep platform code limited to:

- obtaining normalized key/input events;
- entering/leaving any platform-specific shell edit mode;
- rendering/re-rendering the current editable line;
- retaining the already-existing ADV output-scrollback operation.

Do **not** implement two separate Linux/ADV history rings.

Do **not** move history into the public Input API or expose it to applications.

A small private platform-console event/render interface may replace or augment
`minishell_platform_console_read_line()` if that is the cleanest design. Keep it
strictly private in `core/platform_backend.h`; `include/minishell/api.h` must not
change.

## History storage

Keep exactly 10 submitted command lines, each within the existing shell line
bound.

Requirements:

- fixed-size storage; no heap allocation;
- RAM only;
- starts empty each MiniShell session/boot;
- blank/whitespace-only submitted lines are not stored;
- interactive submitted lines are stored after editing;
- startup `startup=...` segments are **not** stored in command history;
- duplicate commands are allowed and stored normally;
- when the 11th command is stored, discard the oldest;
- history is not persisted to `/flash` or any settings file;
- foreground app execution/return does not clear history.

An edited recalled line is a new submitted command. Example:

```text
history contains:  cp a.txt /sd
recall -> edit to: cp b.txt /sd
Enter
```

After execution, both lines may exist in history according to normal ring order.

## Navigation semantics

At a fresh prompt:

- history navigation begins just after the newest entry;
- first Previous recalls the newest command;
- repeated Previous walks toward the oldest and stops there;
- Next walks toward newer entries and stops after the newest;
- moving Next past the newest restores the draft that existed before history
  navigation began.

Example:

```text
M$> cp dra
# press Previous
M$> ls
# press Next
M$> cp dra
```

The draft restoration must work even when the draft is empty.

Starting a new prompt resets only navigation position/draft state; it does not
clear the history ring.

## Editing semantics

Recalled text must be editable before execution.

Shared minimum behavior:

- printable ASCII inserts into the current edit buffer;
- Backspace removes the character before the cursor;
- Delete removes the character at the cursor where the platform exposes it;
- Enter submits the currently edited line;
- buffer never exceeds the existing 255-character command payload bound;
- edits never mutate an older history entry in place.

Linux interactive TTY behavior:

- Left/Right move the cursor;
- Home/End should retain their normal parser meaning if inexpensive;
- Up/Down navigate history;
- Backspace/Delete edit at cursor;
- redraw the line/cursor correctly.

ADV physical keyboard behavior:

- `Fn+,` (Fn+Left event) navigates Previous;
- `Fn+/` (Fn+Right event) navigates Next;
- `Fn+;` / `Fn+.` remain output scrollback only;
- ordinary comma, slash, semicolon and period remain printable characters;
- recalled text may be edited using the ordinary available ADV editing controls;
  at minimum printable input plus Backspace/Delete at the active edit position;
- do not require a new physical cursor-motion chord in this task.

Do not reinterpret bare `;` as interactive command sequencing. T069 startup-only
semicolon behavior remains unchanged.

## ADV display / redraw requirement

History recall must visibly replace the editable command text; it must not merely
change a hidden buffer.

Do not implement recall by printing a fresh prompt/newline for every Previous/Next
press. Navigation should edit the current prompt line/command region in place.

The redraw must remain correct for commands that wrap across the 20-column ADV
console. Do not regress the existing 255-character shell line bound merely to
make redraw easy.

It is acceptable to add a small private ADV console/display edit primitive that
tracks the current prompt/input region, provided:

- retained 50-row output scrollback remains coherent;
- USB console mirror remains coherent;
- foreground application Display ownership is untouched;
- ordinary shell output behavior is unchanged outside active line editing.

## Linux terminal requirement

For a real interactive TTY, the resident shell must receive arrow/edit keys
immediately rather than waiting for Enter.

Reuse/refactor the existing Linux terminal parser/raw-mode machinery where
practical. Avoid writing a second ANSI/CSI parser in `linux_console.c`.

Critical compatibility rule:

- when stdin is **not a TTY** (pipes, test input, redirected scripts), preserve
  the current line-oriented behavior and do not require terminal escape handling.

Raw terminal state must always be restored before launching a foreground app and
after shell line input completes/fails. Do not leave the user's terminal in raw
mode on normal exit/error.

Do not break the existing application Input raw-mode lifecycle.

## Scrollback separation

Command history and output scrollback are independent features.

ADV:

```text
Fn + , / Fn + /    command history previous/next
Fn + ; / Fn + .    retained output scrollback up/down by five rows
```

History navigation must not alter `s_console_offset` except insofar as editing a
command already returns the console view to the live tail as existing typing does.

Linux:

Up/Down are history keys. PgUp/PgDn are not history keys.

Do not build a new Linux output-history subsystem in T075. Preserve existing
terminal behavior for PageUp/PageDown as far as the current platform allows.

## Prompt / command semantics

The shell still executes exactly the submitted line through the existing
`execute_line()` path.

Preserve:

- aliases;
- `cd` / `pwd`;
- T073 bare `ls`/`cd` defaults;
- T074 `cp`/`mv` behavior;
- startup sequencing;
- synchronous foreground app lifecycle;
- diagnostics;
- `exit` behavior.

History contains what the user submitted, not the alias-expanded command.

Example:

```text
M$> f
# alias expands f=ft8
```

History stores `f`, not `ft8`.

## Public/private boundaries

- Preserve `MINISHELL_API_VERSION 3`.
- Do not edit `include/minishell/api.h`.
- History is resident shell state, not a public service.
- Applications cannot read or mutate shell history.
- No filesystem persistence.
- No POSIX/ESP-IDF calls from core history/editor logic.
- Platform-specific key/terminal/display mechanics remain below the private
  platform boundary.

## Non-goals

Do not implement in T075:

- persistent command history;
- history search;
- Ctrl-R;
- command completion;
- pathname completion;
- Tab choice display;
- command-name completion;
- `clear`;
- shell quoting/globbing/pipes/redirection;
- changing console output scrollback depth;
- changing T072/T073/T074 filesystem semantics;
- a new Linux output scrollback database;
- new ADV physical cursor-navigation chords.

## Acceptance criteria

- [ ] History stores at most 10 non-empty interactive submitted commands.
- [ ] 11th command evicts the oldest.
- [ ] Duplicate commands are retained.
- [ ] Startup commands do not enter history.
- [ ] History survives foreground app return but not reboot/new session.
- [ ] Previous/Next navigation order is correct.
- [ ] Next past newest restores the original draft.
- [ ] Editing a recalled command does not mutate the old entry.
- [ ] Executing an edited recalled command stores the edited line as a new entry.
- [ ] ADV `Fn+,` navigates previous history.
- [ ] ADV `Fn+/` navigates next history.
- [ ] ADV `Fn+;` / `Fn+.` scrollback behavior is unchanged.
- [ ] Bare `,`, `/`, `;`, `.` remain printable input.
- [ ] ADV recall/redraw works for wrapped command lines.
- [ ] Linux Up/Down navigates history on a real TTY.
- [ ] Linux Left/Right and Backspace/Delete remain usable while editing.
- [ ] Non-TTY Linux stdin remains line-oriented and regression-free.
- [ ] Public MiniShell API is unchanged.
- [ ] Full Linux CTest passes.
- [ ] ADV firmware builds successfully.

## Automated tests

Add a pure/shared editor-history unit test covering at least:

1. empty history;
2. one command Previous/Next;
3. 10 entries;
4. 11th evicts oldest;
5. duplicates;
6. blank/whitespace lines excluded;
7. draft save/restore;
8. edit after recall;
9. edited recall submitted as new entry;
10. cursor/backspace/delete boundaries;
11. maximum line length;
12. startup/noninteractive path does not add history.

Extend ADV console tests to cover:

```text
Fn+,  -> history previous
Fn+/  -> history next
Fn+;  -> scrollback up unchanged
Fn+.  -> scrollback down unchanged
, / ; . -> printable
wrapped recalled command redraw
draft restoration
```

Verify `adv_keyboard.cpp` mapping itself remains unchanged unless an actual bug
is found; T075 should consume its existing Fn+Left/Fn+Right events.

Add Linux PTY integration tests that send real ANSI arrow sequences and verify:

- Up/Down recall;
- Left/Right edit;
- Backspace/Delete;
- draft restoration;
- ten-entry eviction;
- terminal mode restored after shell exit and after a foreground app;
- redirected/non-TTY command input still works.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T075-unit
cmake --build /tmp/T075-unit -j"$(nproc)"
ctest --test-dir /tmp/T075-unit --output-on-failure

idf.py -C platform/adv build
git diff --check
```

## Manual / hardware validation

After supervisor review, validate on ADV:

```text
M$> pwd
M$> ls
M$> cd /flash
M$> ls
```

Then:

1. Press `Fn+,` repeatedly and verify commands walk backward.
2. Press `Fn+/` and verify they walk forward.
3. Type a partial draft, navigate back once, then forward to newest and verify
   the draft returns.
4. Recall a command, edit it with normal typing/backspace, Enter, and verify the
   edited line executes.
5. Verify bare `,`, `/`, `;`, `.` still type normally.
6. Verify `Fn+;` / `Fn+.` still scroll the 50-row console output.

Also test more than ten commands and verify navigation no longer reaches the
oldest evicted line.

On pc-1/Linux interactive terminal, verify Up/Down and ordinary cursor editing.

No QMX/RF validation is required.

## Codex branch / handoff

Work on:

```text
codex/T075-shell-history
```

Start from current `main`.

Before coding, read `AGENTS.md`, `docs/README.md`, T051, T072-T074, and this task
packet. Inspect the existing Linux terminal parser before choosing the private
console-event refactor.

Keep T075 to one reviewable implementation commit. Do not merge to `main` and
do not open a PR unless asked.

## Codex implementation notes

### Implementation summary

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and local test evidence.

## Architect test result

Record ADV/Linux command-history validation here.