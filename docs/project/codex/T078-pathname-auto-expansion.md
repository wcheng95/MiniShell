# T078 — Eager pathname longest-prefix expansion

Status: BLOCKED

## Architect intent

Add fast pathname entry to the resident MiniShell editor without turning MiniShell
into a general command-completion shell.

The desired behavior is eager **pathname-only** expansion while typing:

```text
cd /f
-> cd /flash

cd /flash/f
-> cd /flash/ft8
```

When multiple matching pathnames remain, expand only to their longest common
unambiguous prefix and stop:

```text
directory contains:
  RT260925.txt
  RT260926.txt
  RxTxLog.txt

typing:
  RT

becomes:
  RT26092
```

No command-name completion, alias-name completion, application-name completion,
or Tab choice UI is part of T078.

## Objective

Implement shared resident-shell pathname expansion that:

- queries directory entries through the existing public Filesystem API;
- expands only the active pathname component;
- inserts the longest additional prefix common to all matches;
- works with absolute and T072 relative paths;
- behaves identically on Linux interactive TTY and ADV;
- preserves T075-T077 history/edit/cursor behavior;
- adds no public MiniShell API.

## Current context

Read before editing:

```text
AGENTS.md
README.md
docs/README.md
docs/api/filesystem-api.md
docs/api/console-api.md
docs/project/codex/T072-shell-cwd-relative-paths.md
docs/project/codex/T075-shell-history.md
docs/project/codex/T076-adv-shell-key-remap.md
docs/project/codex/T077-adv-edit-cursor.md
core/shell.c
core/shell_editor.c
core/shell_editor.h
core/platform_backend.h
platform/linux/linux_console.c
platform/adv/adv_console.c
tests/shell_editor_test.c
tests/linux_shell_history.py
tests/adv_console_scrollback_test.py
include/minishell/api.h
```

Current relevant contracts:

- shell parsing is whitespace-only; there is no quoting/globbing syntax;
- shell payload is at most 255 characters plus NUL;
- the shared editor owns line/cursor/history semantics;
- Linux and ADV platform readers both feed successful printable-character edits
  into the same core editor;
- Filesystem API already supports relative `dir_open()` through the session CWD;
- public directory iteration is `dir_open` / `dir_read` / `dir_close`;
- `mini_fs_dir_entry_t.name` is a single child name, max 255 bytes;
- Tab choice display is planned separately for T079.

## Architecture decision

Path-completion policy belongs to **resident core**, not Linux or ADV.

Create one shared core completion module, for example:

```text
core/shell_completion.c
core/shell_completion.h
```

or an equivalently bounded core-owned implementation.

The module may obtain/use the public Filesystem service, but must not call POSIX,
FATFS, ESP-IDF, host directory APIs, or private Filesystem/CWD helpers.

Linux and ADV may each make one small call into the shared completion logic after
a successful printable-character insertion. They must not implement their own
directory matching or command-context policy.

Do not put filesystem traversal into `shell_editor.c`; keep the editor's existing
history/cursor primitives reusable and platform/filesystem independent.

Do not alter `include/minishell/api.h`.

## Trigger semantics

Expansion is **eager**, but only after a successful interactive printable ASCII
character insertion.

Do not trigger merely because of:

- history Previous/Next;
- cursor Left/Right/Home/End;
- Backspace/Delete;
- prompt creation;
- startup commands;
- redirected/non-TTY Linux input;
- Tab.

After insertion, expansion is eligible only when the cursor is at the end of the
active token:

```text
cursor == token end
```

where token end is end-of-line or immediately before shell whitespace.

If the user inserts into the middle of a token with characters still to the
right, do not auto-expand on that keystroke. This avoids surprising mid-line
rewrites.

After auto-expansion, redraw once using the final expanded line and normal cursor
position. T077 ADV cursor becomes visible/restarts normally because the line
changed through the interactive edit path.

## What counts as a pathname token

The first shell token is always the command name and is **never** expanded.

For later tokens, use two eligibility classes.

### 1. Explicit path-looking token — eligible under any command

A non-command token is explicitly path-looking if it:

- starts with `/`; or
- starts with `./`; or
- starts with `../`; or
- contains `/` anywhere.

Examples:

```text
app /fl
app ./se
app ../lo
app dir/fi
```

These may be pathname-expanded even if the command/app itself is unknown.

### 2. Bare pathname token — only in known filesystem-command path positions

A token with no `/` may expand only for these literal commands/operand positions:

```text
cd      argv[1]
ls      argv[1]
cat     argv[1]
df      argv[1]
nano    argv[1]
mkdir   argv[1]
rm      argv[1]
rmdir   argv[1]
cp      argv[1], argv[2]
mv      argv[1], argv[2]
```

This table is resident shell completion policy only; it does not change any app.

Examples:

```text
cat sett       -> may expand setting.txt
cp sett /sd    -> may expand source
cp setting.txt bac -> may expand destination

ft8 ran        -> do NOT expand bare `ran`
run ft         -> do NOT expand app name
unknown foo    -> do NOT expand bare `foo`
```

Do not resolve aliases just to decide completion context. For example, if
`c=cat`, bare `c sett` need not auto-expand `sett`; `c ./sett` remains eligible
because it is explicitly path-looking.

This conservative rule is deliberate: pathname convenience without arbitrary
argument rewriting.

## Active token parsing

Use the same simple whitespace definition as the resident shell:

```text
' '  '\t'  '\r'  '\n'
```

No quote/backslash parser is added.

Determine:

- command token;
- active token index;
- active token start/end;
- cursor position.

Leading whitespace is allowed.

If the active token is empty (for example immediately after a space or immediately
after typing `/`), do not enumerate/expand until at least one component character
has been typed.

## Component-wise path lookup

Complete only the final component of the active token.

Examples:

```text
/fl              directory=/       prefix=fl
/flash/f         directory=/flash  prefix=f
./se             directory=.       prefix=se
../min           directory=..      prefix=min
logs/RT          directory=logs    prefix=RT
setting          directory=.       prefix=setting
```

Use the typed directory spelling with public `dir_open()` and let T072 CWD/path
normalization resolve it. Do not query the private CWD.

For a token whose last slash is the first character, parent directory is `/`.
For a bare token, parent directory is `.`.

Do not canonicalize or rewrite earlier path components in T078.

## Matching rule

Directory-entry matching is:

- byte-for-byte / case-sensitive;
- prefix matching only;
- no fuzzy matching;
- no sorting requirement;
- no directory-first/file-first priority.

Skip `.` and `..` directory entries if a backend exposes them.

Hidden-name rule:

- if the typed component begins `.`, hidden names may match;
- otherwise skip entries whose names begin `.`.

Because MiniShell has no quoting syntax, skip entry names containing shell
whitespace. Do not auto-insert a pathname that the current parser cannot submit
as one token.

Files and directories participate equally. Do **not** append `/` automatically
for a directory.

Examples:

```text
only match: flash
typed: f
-> flash

matches: foo, foobar
typed: f
-> foo

matches: RT260925.txt, RT260926.txt
typed: RT
-> RT26092

matches: red, read
typed: r
-> r       # no additional common prefix
```

If the typed prefix is already the longest common prefix, make no change.

## Longest-unambiguous-prefix algorithm

Stream directory entries; do not build an unbounded list.

A bounded algorithm should:

1. find each eligible entry whose name begins with the typed component;
2. initialize a fixed candidate/common-prefix buffer from the first match;
3. shrink that common prefix against each later match;
4. after enumeration, append only the suffix beyond the already-typed component.

No heap allocation is needed.

The common-prefix buffer may use `MINI_FS_NAME_MAX + 1`.

## Bounds and mutation safety

Never partially expand.

Before modifying the editor line, verify:

```text
editor.length + expansion_bytes <= SHELL_LINE_MAX - 1
```

If the extension does not fit, make no change.

Do not truncate directory names or the command line.

Compute the result first, close the directory handle, and only then mutate the
editor. Any `dir_open`, `dir_read`, or `dir_close` failure must leave the typed
line unchanged.

Typing-time filesystem failures are silent. Do not print completion diagnostics
into the console while the user is editing.

Every successfully opened directory must be closed on all paths.

## Interaction with editing/history

Auto-inserted characters become ordinary visible editor text.

Therefore:

- cursor advances to the end of the inserted expansion;
- Left/Right/Backspace/Delete can edit it normally;
- submitting stores the expanded visible command in history;
- old history entries remain immutable;
- draft restoration preserves whatever expanded text was in the draft;
- recalling history does not itself trigger fresh filesystem expansion;
- moving to the token end does not itself trigger expansion;
- a later printable insertion at token end may trigger expansion again.

## ADV behavior

T076 controls remain unchanged:

```text
Ctrl+; / Ctrl+.   output scrollback
Fn+;   / Fn+.     history
Fn+,   / Fn+/     cursor left/right
```

T077 cursor behavior remains unchanged except that successful eager expansion is
part of the edit update: the final cursor position must be visible immediately
and its blink phase restarts exactly like any other successful text edit.

Do not add separate completion rendering logic to ADV.

## Linux behavior

Interactive TTY completion uses the same shared core matching logic.

Up/Down/Left/Right/Home/End and terminal-mode lifecycle remain unchanged.

Redirected/non-TTY stdin remains literal line-oriented input: no eager expansion.

## Filesystem resource behavior

Completion uses public directory handles transiently while editing.

Requirements:

- no leaked handles;
- no retained directory handle between keystrokes;
- no app lifecycle ownership change;
- no backend-specific path conversion;
- no extra resident worker/task.

## Documentation terminology

Use **pathname expansion** or **pathname auto-expansion** for T078.

Do not call it command completion.

Tab choice display remains T079.

## Architectural constraints

- Preserve `MINISHELL_API_VERSION 3` and `include/minishell/api.h`.
- Preserve Filesystem/CWD implementation from T072.
- Preserve T075 history capacity and semantics.
- Preserve T076 key mapping.
- Preserve T077 cursor renderer/lifecycle.
- Completion matching exists once in shared core.
- Linux/ADV must not duplicate directory matching policy.
- Public Filesystem API only.
- No heap allocation.
- No persistence.
- No command/alias/app-name completion.
- No quoting/globbing added.

## Non-goals

Do not implement in T078:

- Tab output or candidate listing (T079);
- Tab cycling;
- command-name completion;
- alias-name completion;
- app-name completion;
- option completion;
- fuzzy completion;
- case-insensitive completion;
- automatic trailing `/` for directories;
- quoting/escaping filenames with spaces;
- wildcard/glob expansion;
- HOME / `~` expansion;
- persistent completion cache;
- filesystem watcher/cache;
- `clear`.

## Acceptance criteria

- [ ] `cd /f` eagerly becomes `cd /flash` when `/flash` is the only matching root entry.
- [ ] `cd /flash/f` expands the final component from `/flash`.
- [ ] Relative CWD lookup works (`cat se` -> `setting.txt` when unambiguous).
- [ ] Multiple matches expand only to longest common prefix.
- [ ] Ambiguity with no longer common prefix makes no change.
- [ ] Files and directories both match.
- [ ] No automatic trailing slash is inserted.
- [ ] Hidden entries are ignored unless component begins `.`.
- [ ] Entry names containing shell whitespace are skipped.
- [ ] Command token never expands.
- [ ] Bare arbitrary app arguments never expand.
- [ ] Explicit path-looking arguments may expand under arbitrary commands.
- [ ] `cp`/`mv` both pathname operands support bare expansion.
- [ ] Alias expansion is not consulted to infer bare-path context.
- [ ] Mid-token insertion with text to the right does not auto-expand.
- [ ] History recall/cursor movement alone does not auto-expand.
- [ ] Backspace/Delete alone do not auto-expand.
- [ ] Redirected Linux input does not auto-expand.
- [ ] Overlong expansion is a no-op, never partial.
- [ ] FS enumeration/close failure is a silent no-op.
- [ ] Directory handles are always closed.
- [ ] Expanded text is editable and stored normally in command history.
- [ ] ADV cursor ends at the expanded insertion point and remains usable.
- [ ] Linux and ADV produce the same expansion semantics.
- [ ] Public API and T072 CWD implementation remain unchanged.
- [ ] Full Linux CTest passes.
- [ ] ADV firmware builds successfully.

## Automated tests

Add focused shared completion tests with a fake/public FS directory API covering
at least:

1. unique root component `/f` -> `/flash`;
2. nested `/flash/f`;
3. bare CWD component under `cat`;
4. longest common prefix across two/many matches;
5. ambiguous no-extension case;
6. exact name plus longer sibling (`foo`, `foobar`);
7. files and directories together;
8. hidden filtering and explicit dot prefix;
9. skip names containing whitespace;
10. no matches;
11. command token never expands;
12. arbitrary bare app arg never expands;
13. arbitrary explicit path arg does expand;
14. each known bare-path command/operand position, especially both `cp` and `mv` operands;
15. leading whitespace;
16. relative `./`, `../`, and `dir/prefix` parents;
17. empty final component after `/` -> no lookup/expansion;
18. mid-token cursor -> no expansion;
19. line-capacity overflow -> no mutation;
20. `dir_open` / `dir_read` / `dir_close` failure -> no mutation and proper close;
21. directory iteration order does not change result.

Extend Linux interactive PTY coverage to prove eager visible expansion through
the real shell editor and real Filesystem backend. Include fixtures such as:

```text
/flash
/sd
/flash/ft8/setting.txt
/flash/ft8/RT260925.txt
/flash/ft8/RT260926.txt
```

Exercise absolute, relative-CWD, ambiguity and an arbitrary app bare argument.
Verify redirected input remains literal.

Extend ADV host console test to prove a physical/USB printable character can
trigger the same shared expansion and that T077 cursor/redraw lands after the
expanded text without changing T076 controls.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T078-unit
cmake --build /tmp/T078-unit -j"$(nproc)"
ctest --test-dir /tmp/T078-unit --output-on-failure

idf.py -C platform/adv build
git diff --check
```

## Manual / hardware validation

On ADV, prepare a directory with a few known names and test:

```text
M$> cd /f
# should eagerly show /flash

M$> cd /flash/f
# should expand only when the component prefix is unambiguous

M$> cd /flash/ft8
M$> cat se
# should eagerly expand toward setting.txt
```

Create/use two similar filenames and verify longest-prefix behavior, for example:

```text
RT260925.txt
RT260926.txt
```

Typing `RT` should expand only to their shared longer prefix and stop before the
differing digit.

Also verify:

- Fn cursor/history controls still work;
- Ctrl scrollback still works;
- blinking cursor ends after expanded text;
- an arbitrary bare non-path app argument is not rewritten;
- explicit `./prefix` still expands.

On pc-1, perform the analogous quick interactive check.

No QMX/RF validation is required.

## Codex branch / handoff

Work on:

```text
codex/T078-pathname-auto-expansion
```

Start from current `main`.

Before coding, read T072 and T075-T077 plus this task. Keep the matching policy
shared in core and keep platform changes to minimal trigger/redraw integration.

Keep T078 to one reviewable implementation commit. Do not merge to `main` and
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

Reviewed `main..20c58587c5a45c1a102d9af4fd0d1896b1d0fabf` against T078.

Result: **BLOCKED — paste-safety correction required before merge.**

The implementation architecture is otherwise clean:

- one shared core pathname matcher;
- public Filesystem `dir_open/dir_read/dir_close` only;
- no command/alias/app-name completion;
- bounded streaming longest-common-prefix algorithm;
- no heap allocation;
- Linux/ADV use the same matcher;
- public API, T072 CWD service, shared editor/history, ADV keyboard mapping and
  T077 renderer are byte-for-byte unchanged.

Blocking issue:

Interactive pasted text is currently processed one character at a time, and
completion runs immediately after every printable insertion. Therefore pasting a
full pathname can duplicate a suffix that was eagerly inserted earlier.

Example:

```text
paste:  cd /flash

processing:
  typed /f
  auto-expands to /flash
  remaining pasted "lash" is then inserted literally

result:
  cd /flashlash
```

That behavior is too surprising for pc-1 and the ADV USB console and is not
accepted as a T078 limitation.

### Required correction

Make eager expansion **paste-safe** while keeping normal human typing effectively
instant.

Preferred solution: use a short input-idle debounce (approximately 20–30 ms):

1. after a successful eligible printable insertion, mark completion pending;
2. do not enumerate the filesystem immediately;
3. each subsequent printable byte before the deadline refreshes the deadline;
4. when the input stream is idle for the short interval, run the existing shared
   `shell_completion_expand()` once;
5. redraw only if expansion changed the line.

This preserves the intended behavior for human typing while allowing a pasted
full path to arrive as one burst before completion is attempted.

Constraints:

- no new task/thread/timer;
- Linux should fold the pending-completion deadline into its existing `poll()`
  timeout;
- ADV should use the existing 5 ms input loop and monotonic clock;
- completion matching/policy remains in shared core;
- redirected/non-TTY Linux input remains literal;
- history/cursor/Backspace/Delete semantics remain unchanged;
- T076/T077 behavior remains unchanged;
- no public API change.

An equivalent bounded solution is acceptable if it is shared in behavior and
does not rely on a platform-specific paste protocol.

### Required regression tests

Add real interactive tests for at least:

```text
paste "cd /flash"
    -> exactly "cd /flash"
paste "cat /flash/ft8/setting.txt"
    -> exact pasted path, no duplicated suffix
paste relative "cat setting.txt"
    -> exact pasted text
human-style delayed "cd /f"
    -> still eagerly expands to "cd /flash"
```

Cover both Linux PTY and ADV USB-input host fixtures. Also verify that completion
still fires after the debounce with the expected longest-prefix result.

Do not merge `20c58587c5a45c1a102d9af4fd0d1896b1d0fabf` as-is. Amend/fix the
T078 branch and return a new review SHA.

## Architect test result

ADV hardware validation passed on 2026-09-24 for the implemented eager pathname
behavior itself.

Verified on real Cardputer ADV:

- absolute pathname expansion;
- relative pathname expansion;
- longest-common-prefix behavior;
- T076 history/cursor controls remain usable;
- T077 blinking cursor lands correctly after expanded text;
- Ctrl scrollback remains usable.

Result for ADV typed-entry behavior: **PASS**.

T078 remains **BLOCKED** overall because supervisor review found the separate
interactive paste-safety issue: a pasted full pathname can duplicate a suffix
after an earlier eager expansion. That issue must be corrected before merge.

After the paste-safe fix, re-run the pc-1 interactive check and, if the fix
touches ADV input timing, a short ADV regression check.