# T075 — 10-command editable resident shell history

Status: TESTING

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

- [x] History stores at most 10 non-empty interactive submitted commands.
- [x] 11th command evicts the oldest.
- [x] Duplicate commands are retained.
- [x] Startup commands do not enter history.
- [x] History survives foreground app return but not reboot/new session.
- [x] Previous/Next navigation order is correct.
- [x] Next past newest restores the original draft.
- [x] Editing a recalled command does not mutate the old entry.
- [x] Executing an edited recalled command stores the edited line as a new entry.
- [x] ADV `Fn+,` navigates previous history.
- [x] ADV `Fn+/` navigates next history.
- [x] ADV `Fn+;` / `Fn+.` scrollback behavior is unchanged.
- [x] Bare `,`, `/`, `;`, `.` remain printable input.
- [x] ADV recall/redraw works for wrapped command lines.
- [x] Linux Up/Down navigates history on a real TTY.
- [x] Linux Left/Right and Backspace/Delete remain usable while editing.
- [x] Non-TTY Linux stdin remains line-oriented and regression-free.
- [x] Public MiniShell API is unchanged.
- [x] Full Linux CTest passes.
- [x] ADV firmware builds successfully.

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

Implemented from current `origin/main` at
`6354464cdf6d33f1ba840b316d1c286a43a6a3b4` on the requested branch.
No architecture deviations: one core-owned fixed editor/history instance serves
both backends. The private line reader takes that instance and reports whether
input was interactive. Only the shell stores submissions, before alias expansion
or argument splitting; startup and redirected input never store entries.

The editor retains ten commands with FIFO eviction, duplicates, clamped
Previous/Next navigation, original draft/cursor restoration, and independent
editable recall. Printable ASCII insertion, cursor movement, Backspace and
forward Delete retain a maximum 255-character payload. New prompts reset only
the draft/navigation; a new shell session initializes the complete instance.

Linux TTY input uses the existing terminal parser with its own callback and
reuses the exclusive terminal-mode lease. One-byte reads avoid consuming the
next app's input. Raw state is released before returning on submission, EOF or
error; Ctrl-C cancels the draft locally instead of terminating in raw mode.
Non-TTY input retains `fgets()`. Long interactive lines pan horizontally within
a single terminal row; Enter commits the full line to terminal scrollback.

ADV consumes existing Fn+Left/Right as Previous/Next, leaving Fn+Up/Down output
scrollback and keyboard normalization unchanged. A fixed snapshot of the retained
prompt rows lets the TFT redraw the complete editable region, including wrapped
255-character lines. Shrinking a draft restores temporarily displaced output
rows; repeated navigation does not append prompts or consume output history.
USB redraw escapes go only to the mirror, never the TFT parser. Forward Delete
now removes at the edit cursor as specified (at the ADV end cursor it is a no-op;
Backspace remains the normal trailing-character erase).

A private prompt operation puts interactive prompts on a new row after output
without a final newline. Linux Display writes invalidate the known cursor row so
full-screen app return also gets a correctly positioned prompt. Redirected Linux
prompt/output behavior remains unchanged.

### Files changed

- `core/shell_editor.c`, `shell_editor.h`: pure bounded editor and ten-entry ring.
- `core/shell.c`, `platform_backend.h`: core session ownership, pre-expansion
  submission recording and private prompt/line-reader contract.
- `platform/linux/linux_console.c`, `linux_terminal.c`, `linux_internal.h`:
  parser-backed interactive editor, raw-mode lease reuse, cursor redraw and
  prompt positioning; retain redirected input and application Input lifecycle.
- `platform/adv/adv_console.c`, `adv_display.cpp`, `adv_internal.h`: Fn event
  mapping, shared editor calls, wrapped prompt snapshot/redraw and USB preview.
- Root `CMakeLists.txt`, `platform/adv/main/CMakeLists.txt`,
  `tests/unit/CMakeLists.txt`: compile the shared engine and register tests.
- `tests/shell_editor_test.c`: pure navigation, storage and editing bounds.
- `tests/linux_shell_history.py`: real ANSI/PTY navigation, editing, app return,
  narrow-terminal behavior, prompt placement, mode restoration and pipe input.
- `tests/adv_console_scrollback_test.py`: actual ADV renderer/event/reader paths
  with hardware stubs, wrapped recall, history keys, draft restoration and USB.
- `tests/shell_alias_test.c`, `resident_boot_test.c`: private reader adaptations;
  alias tests also prove history is unexpanded, startup/noninteractive exclusion,
  edited recall storage, foreground return and new-session reset.
- `README.md`, `docs/README.md`, `docs/api/console-api.md`,
  `platform/adv/README.md`: controls, lifecycle, editing and redraw behavior.
- This task packet: implementation and validation evidence.

### Invariants preserved

`include/minishell/api.h`, API v3, all Filesystem/CWD service code, all portable
applications and `adv_keyboard.cpp` are byte-for-byte unchanged from the base.
No persisted state, heap allocation, worker task, public capability or app access
to history. The existing dispatcher still executes exactly the submitted line;
aliases, startup-only semicolons, cd/pwd, bare defaults, cp/mv and foreground
synchronization are preserved. ADV output history remains 50 physical rows with
five-row Fn+Up/Down steps and independent foreground Display ownership. The ADV
input polling interval remains 5 ms. Linux PageUp/PageDown do not navigate history.

On 32-bit ADV, the editor instance occupies 3,096 bytes of static storage;
the prompt snapshot is 1,000 bytes plus 12 bytes of bookkeeping. Prompt placement
adds one boolean. These are fixed allocations (plus linker alignment), not an
extra task stack or heap usage. The former stack-local shell input line is now
part of the resident editor instance.

### Local tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R 'shell|linux_input|linux_nano|resident_boot|adv_console'
# PASS: final focused run 7/7.
cmake -S tests/unit -B /tmp/T075-unit
cmake --build /tmp/T075-unit -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir /tmp/T075-unit --output-on-failure
# PASS: 28/28.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: initial full run 125/125 (59.42 seconds).
# Two post-fix full runs: 124/125 each, unchanged linux_serial_unit assertion.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R '^linux_serial_unit$'
# PASS: focused retry between those runs, 1/1.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure --repeat until-pass:3
# PASS: 125/125 (60.11 seconds); every test passed on its first attempt.
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: final firmware 0x153010 bytes; app partition 78% free. No flashing.
git diff --check
# PASS
```

Also ran the ADV host renderer test and Linux PTY history test directly while
implementing. Early PTY fixture assertions were corrected to reflect existing
behavior: names containing `/` fail app-name validation, and `hello` waits for
user input. The completed PTY fixture explicitly exits hello and retains its
Display-return regression. No existing product test was weakened. ADV builds
retain existing SDK/FreeRTOS/ELF-loader pedantic warnings.

The post-fix full runs encountered the previously recorded Serial PTY assertion
at `tests/linux_serial_test.c:67` (write timeout and zero bytes). Its focused retry
passed. The final full run allowed up to three attempts per failing test to bound
this existing flake. All 125 tests passed on their first attempt in that final
run (125 test starts, no failed attempts), so no per-test repeats were needed.
Serial code/tests are unchanged, and no test assertion was relaxed.

The full suite covers existing aliases/startup, CWD, cp/mv, Linux Input and nano,
ADV output scrollback/Display handoff, public exports and application regressions.
New tests cover the specified pure history/editor cases and real ANSI controls,
unexpanded aliases, ten-entry eviction, maximum payload, Ctrl-C/EOF, a forced
stdout error, and exact terminal-attribute restoration after exit/error and after
foreground Input use. They also verify prompt alignment after non-newline Console
output and foreground Display return. ADV tests exercise both scrollback Fn keys,
both history Fn keys, bare punctuation, USB input, 255-character wrapped redraw,
repeated grow/shrink at the 50-row capacity, and draft restoration without newlines.

### Manual/hardware validation still required

After supervisor review, run the ADV and pc-1 checklists above. Verify physical
Fn+`,` / Fn+`/` navigation, recalled edits, eviction, wrapped TFT redraw, and
unchanged Fn+`;` / Fn+`.` output scrollback. Check the USB mirror on an ANSI host
terminal. No device was flashed or operated here; no QMX/RF validation required.

### Known limitations / risks

History is bounded to ten commands and 255 characters, uses printable ASCII
editing, and is not persisted. Linux long input pans within the terminal width.
The ADV USB mirror has no host-size negotiation: its single-row preview shows
up to 74 trailing command characters and assumes an ANSI terminal at least 80
columns wide; Enter prints the full submitted line. The TFT always retains the
complete wrapped command. Raw-mode restoration is tested for normal submission,
app handoff, EOF and I/O failure; abrupt external process termination is not a
new signal-cleanup feature. Physical key delivery, TFT usability and USB display
still require hardware acceptance.

### Commit

One implementation commit titled `T075: add editable resident shell history`,
parent `6354464cdf6d33f1ba840b316d1c286a43a6a3b4`, on
`codex/T075-shell-history`. Exact pushed SHA is returned in the handoff.
No merge or PR.

## Supervisor review

Reviewed `main..8029950648c0230a84aecb12abc3a56681960637` against T075,
`AGENTS.md`, the T051 scrollback behavior, and the current resident/private
console boundaries.

Result: **PASS for software review; advanced to TESTING.**

Findings:

- exactly one bounded implementation commit, one commit ahead of the T075 task baseline;
- history/edit semantics are centralized in one core-owned fixed-size
  `shell_editor_t`; Linux and ADV do not maintain independent history rings;
- history is initialized only when the interactive shell begins, so boot
  `startup=` commands are excluded by construction;
- only interactive submissions (`read_line == 2`) are remembered; redirected
  non-TTY lines preserve the old line-oriented behavior and are not stored;
- the ring retains ten non-empty lines, allows duplicates, evicts FIFO, preserves
  the pre-navigation draft/cursor, and never mutates old history entries in place;
- Linux reuses the existing terminal parser rather than introducing a second CSI
  decoder; Up/Down navigate history and Left/Right/Home/End/Backspace/Delete edit;
- the Linux shell raw-mode lease is released before `execute_line()`, so a
  foreground app acquires its normal independent Input terminal lease; EOF/error/
  Ctrl-C paths also restore termios;
- ADV consumes the already-normalized Fn+Left/Fn+Right events as history
  Previous/Next, while existing Fn+Up/Fn+Down output scrollback remains unchanged;
- `adv_keyboard.cpp` is byte-for-byte unchanged (blob
  `5afceeb97b3fee479b2a30238286dc0083607b44`);
- ADV edit redraw uses a snapshot of the committed console history, so wrapped
  recalled lines can grow/shrink without appending prompts or corrupting the
  retained 50-row output history;
- ordinary `,`, `/`, `;`, and `.` remain printable; interactive semicolon
  parsing is unchanged;
- the public API is byte-for-byte unchanged (blob
  `13ce3b15fb5b4e1b0047d9559eb9aca91e6312a0`).

Accepted local evidence:

```text
Linux CTest: 125/125 PASS
portable unit tests: 28/28 PASS
ADV ESP-IDF build: PASS
git diff --check: PASS
```

The documented serial PTY failures are the existing intermittent
`linux_serial_unit` flake; serial implementation/assertions were not changed and
the final full suite passed.

No blocking review finding. `main` was fast-forwarded to
`8029950648c0230a84aecb12abc3a56681960637`.

Remaining gate: architect manual validation on ADV and pc-1. On ADV verify
Fn+,/Fn+/ history navigation, draft restoration, edited recall, >10-command
eviction, printable punctuation, and unchanged Fn+;/Fn+. scrollback. On pc-1
verify Up/Down plus ordinary cursor editing and terminal restoration around a
foreground app. No QMX/RF validation is required.

## Architect test result

Record ADV/Linux command-history validation here.