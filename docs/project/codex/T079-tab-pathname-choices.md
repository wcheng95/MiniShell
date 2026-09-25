# T079 — Tab shows ambiguous pathname choices

Status: REVIEW

## Architect intent

Build the second stage of the accepted T078 pathname-completion behavior.

T078 is now explicit Tab-only:

```text
typing / paste    literal
Tab               expand pathname to longest common prefix
```

T079 extends that behavior only when Tab cannot grow the pathname any farther
because multiple matching pathnames remain.

Example:

```text
directory contains:
  RT260925.txt
  RT260926.txt

M$> cat RT<Tab>
M$> cat RT26092

# another Tab: no longer common prefix exists
M$> cat RT26092<Tab>
RT260925.txt
RT260926.txt
M$> cat RT26092
```

The editable command and cursor remain exactly where they were after the list.

## Objective

On an interactive Tab request:

1. preserve T078's pathname-only longest-prefix expansion;
2. if the Tab request changes the line, redraw and stop — do not list choices;
3. if the line cannot grow and **two or more** valid pathname matches remain,
   print those remaining choices;
4. restore the same editable prompt, line, cursor, history-navigation state and
   ADV blinking cursor;
5. if zero or one match remains, do nothing.

No command/app/alias completion is added.

## Current context

Read before editing:

```text
AGENTS.md
docs/api/console-api.md
docs/api/filesystem-api.md
docs/project/codex/T075-shell-history.md
docs/project/codex/T076-adv-shell-key-remap.md
docs/project/codex/T077-adv-edit-cursor.md
docs/project/codex/T078-pathname-auto-expansion.md
core/shell_completion.c
core/shell_completion.h
platform/linux/linux_console.c
platform/adv/adv_console.c
platform/adv/adv_display.cpp
tests/shell_completion_test.c
tests/linux_shell_history.py
tests/adv_console_scrollback_test.py
```

Current accepted invariants:

- typing/paste never triggers pathname completion;
- `MINI_KEY_TAB` is the only completion trigger;
- command token never completes;
- explicit path-looking non-command tokens are eligible under any command;
- bare tokens are eligible only in T078's known filesystem operand positions;
- matching is case-sensitive, final-component-only, no fuzzy matching;
- hidden names require a typed leading `.`;
- names containing shell whitespace are skipped;
- no automatic trailing slash is inserted into the command;
- shared matcher uses only public Filesystem `dir_*` operations;
- no heap, cache, task, worker or public API change.

## Exact Tab behavior

Treat each Tab independently.

### Case A — Tab can expand

```text
M$> cd /f<Tab>
M$> cd /flash
```

Behavior:

- run T078 expansion;
- if `shell_completion_expand()` changes the editor line, redraw once;
- stop processing this Tab;
- do **not** print candidate choices even if `/flash...` would still be ambiguous.

Therefore one Tab performs at most one conceptual action: **expand OR list**.

### Case B — Tab cannot expand and multiple matches remain

```text
M$> cat RT26092<Tab>
RT260925.txt
RT260926.txt
M$> cat RT26092
```

Behavior:

- line remains byte-for-byte unchanged;
- print all matching eligible choices;
- restore the prompt and same edit state.

### Case C — zero or one match

Silent no-op.

Examples:

```text
M$> cat absent<Tab>       # no matches: no output
M$> cat setting.txt<Tab>  # one exact match: no output
```

### Case D — non-path completion context

Silent no-op and no filesystem output.

Examples:

```text
M$> ft<Tab>               # command token
M$> unknown bare<Tab>     # arbitrary bare app argument
```

## Shared completion architecture

Candidate discovery/filtering belongs in `core/shell_completion.*` beside the
T078 matcher. Do not duplicate pathname-context parsing or filtering in Linux
and ADV.

Refactor the T078 internals if useful so expansion and candidate listing share:

- active-token eligibility;
- parent/final-component extraction;
- directory iteration;
- hidden-name filtering;
- shell-whitespace filtering;
- prefix matching.

A reasonable private core interface is conceptually:

```c
typedef void (*shell_completion_emit_fn)(const mini_fs_dir_entry_t *entry, void *ctx);

bool shell_completion_expand(shell_editor_t *editor);
size_t shell_completion_list(const shell_editor_t *editor,
                             shell_completion_emit_fn emit, void *ctx);
```

Exact private signatures are up to the implementation.

The list operation must **not mutate** the editor.

Do not expose this through `include/minishell/api.h`.

## Candidate set

The displayed choices must be exactly the same valid matches T078 considers for
the active pathname component at the current cursor position.

Examples:

```text
active token: /flash/ft8/RT26092
display final components only:
  RT260925.txt
  RT260926.txt

active token: ../min
display children matching `min` in parent `..`
```

Do not print the parent path repeatedly.

Skip:

- `.` and `..` entries;
- hidden names unless typed component starts `.`;
- names containing shell whitespace;
- entries that do not match the typed final-component prefix.

Files and directories both participate.

## Directory marker

For display only, append `/` to a candidate whose `entry.type` is
`MINI_FS_TYPE_DIRECTORY`.

Example:

```text
ft8/
setting.txt
```

This slash is **visual only**. T079 does not insert `/` into the command line and
does not change T078's no-auto-slash rule.

If the backend reports an unknown/non-directory type, display the name without
a slash.

## Candidate order

Use the backend/public-Filesystem enumeration order.

Do not add sorting, buffering, or a candidate cache in T079. This keeps the
implementation bounded and avoids allocating a pathname table on ADV.

The result must be independent of ordering for the decision to list; only the
presentation order may follow the backend.

## Number of choices

List all valid matches. Do not impose a candidate-count cap in T079.

This is intentionally simple: users should type a longer prefix before Tab when
a directory has many matches. ADV's existing 50-row console scrollback remains
the output-history mechanism; no completion-specific pager is added.

## Linux presentation

When choices are listed on an interactive Linux TTY:

1. terminate/move below the current editable prompt line;
2. print each candidate on its own line;
3. print `M$> ` again;
4. redraw the unchanged editor line at its unchanged cursor position;
5. remain in the current shell raw/input session.

Do not submit the line, leave raw mode, create a history entry, or start a new
shell command.

The candidate listing becomes ordinary terminal scrollback.

Long candidate names may wrap according to the host terminal.

## ADV presentation

On ADV, candidate listing uses the normal retained resident console, not
application Display ownership and not a new full-screen UI.

Required lifecycle:

1. temporarily remove/hide the T077 edit cursor overlay;
2. commit the listing as ordinary resident console output;
3. print each candidate followed by newline;
4. print a fresh `M$> ` prompt;
5. start a new private edit snapshot from that prompt;
6. redraw the original editor line/cursor;
7. restart the visible T077 cursor/blink phase.

The command line itself must not enter console history as submitted output;
only the normal prompt redraw/edit region is restored after the choices.

Candidate output **does** become part of the existing 50 physical rows of console
scrollback. Ctrl+`;` / Ctrl+`.` must be able to review it normally.

Do not replay candidate listing periodically to USB. One Tab listing should
produce one ordinary listing on both TFT and USB mirror.

## Editor/history invariants

Listing must preserve the complete `shell_editor_t` byte-for-byte:

- `line`;
- `length`;
- `cursor`;
- draft;
- history ring;
- navigation index.

Only a preceding successful T078 expansion may mutate the editor.

After listing:

- normal typing continues at the same cursor;
- Backspace/Delete work normally;
- Fn/arrow cursor motion works normally;
- history navigation remains at the same logical position;
- Enter submits only the editable command, not the choices.

## Repeated Tab

Repeated Tab at the same ambiguous prefix may list the choices again.

No double-Tab timing state, suppression flag, cycling, or menu selection is
required.

This deliberately avoids timing-sensitive terminal semantics.

## Errors

Use a two-pass bounded listing strategy.

### Validation pass

The first pass determines that the request is valid and that at least two matching
choices exist. If `dir_open`, `dir_read`, or `dir_close` fails during this pass:

- print nothing;
- preserve the editor;
- return to normal editing unchanged.

### Output pass

After a successful validation pass, reopen the directory and stream matching
choices directly to the console.

If `dir_open` fails before any output, print nothing and restore the editor.

If `dir_read` or `dir_close` fails after one or more choices have already been
printed:

- stop listing immediately;
- do not print a completion-specific error diagnostic;
- leave already printed choices visible as ordinary console output;
- restore the unchanged prompt/editor/cursor cleanly.

This partial-output behavior is accepted because T079 deliberately has no
candidate-count cap and no unbounded candidate buffer. The editor state remains
authoritative and must never be partially modified.

Do not retain directory handles across input events.

## Resource constraints

- no heap allocation;
- no candidate cache;
- no new task/thread/timer;
- no persistent state;
- no large fixed table of all candidate names;
- every successfully opened directory is closed;
- public Filesystem API only.

A two-pass directory enumeration for listing is acceptable and preferred over
storing an unbounded/bulky candidate table.

## Public/private boundaries

- `MINISHELL_API_VERSION` remains 3;
- `include/minishell/api.h` unchanged;
- T072 CWD service unchanged;
- T075 history engine unchanged;
- T076 key mapping unchanged;
- T077 ADV renderer semantics preserved;
- pathname completion remains resident private shell behavior.

## Non-goals

Do not implement:

- command-name completion;
- alias-name completion;
- app-name completion;
- option completion;
- candidate selection/cycling;
- fuzzy/case-insensitive matching;
- completion pager;
- columns/grid layout;
- automatic trailing `/` insertion;
- quoting/escaping;
- globbing;
- persistent completion history/cache;
- double-Tab timing semantics;
- `clear`.

## Acceptance criteria

- [x] First Tab that grows a pathname performs only T078 expansion.
- [x] A subsequent Tab at an ambiguous longest prefix lists 2+ matching choices.
- [x] Zero matches produces no listing.
- [x] One exact/remaining match produces no listing.
- [x] Command token Tab produces no listing.
- [x] Arbitrary bare non-path argument Tab produces no listing.
- [x] Explicit path-looking arbitrary argument can list matches.
- [x] Bare filesystem-command operands can list matches.
- [x] Candidate filtering exactly matches T078 rules.
- [x] Directories display with a visual trailing `/`; files do not.
- [x] Candidate order follows backend enumeration order; no sorting/cache.
- [x] All valid matches are listed.
- [x] Editor state is preserved byte-for-byte by listing.
- [x] Linux redraw restores same line and cursor.
- [x] ADV redraw restores same line and cursor with visible blinking cursor.
- [x] Candidate output enters normal ADV 50-row console scrollback.
- [x] Ctrl+`;` / Ctrl+`.` can review candidate output afterward.
- [x] Repeated Tab may list again without state corruption.
- [x] Validation-pass FS failure prints no choices and preserves editor.
- [x] Output-pass failure stops immediately, may leave already printed choices visible, and restores the unchanged editor.
- [x] No directory-handle leaks.
- [x] Typing and paste remain literal.
- [x] T078 expansion behavior remains unchanged.
- [x] Public API/CWD/history/keymap invariants remain unchanged.
- [x] Full Linux CTest passes except documented unrelated known flakes.
- [x] ADV firmware builds successfully.

## Automated tests

Extend shared completion tests to cover candidate enumeration without editor
mutation:

1. 0 matches;
2. 1 exact match;
3. 2+ ambiguous matches;
4. exact name plus longer sibling (`foo`, `foobar`);
5. hidden filtering;
6. whitespace-name filtering;
7. files + directories and directory display type;
8. arbitrary explicit path context;
9. known bare-path command positions;
10. invalid command/bare argument context;
11. mid-token cursor;
12. validation-pass read/close failure -> no output;
13. output-pass open failure -> no output;
14. output-pass mid-read/close failure -> already printed choices may remain, then editor restores;
15. enumeration order;
16. all handles closed;
17. editor byte-for-byte unchanged after list query.

Linux PTY integration must cover:

```text
type `cat RT`
Tab -> line expands to common prefix only, no list
Tab -> choices printed, then prompt + same line restored

type full path/paste -> remains literal

Tab on command/non-path -> no list
```

Verify cursor position after list restoration and terminal mode restoration on
eventual app/exit.

ADV host console test must cover physical and USB Tab paths, including:

- expansion-only first Tab;
- listing second Tab;
- T077 cursor hidden during output and restored afterward;
- editor byte-for-byte preservation;
- choices recorded in resident console history;
- Ctrl scrollback interaction;
- repeated listing;
- no USB duplicate/replay;
- validation-pass failure produces no listing;
- output-pass failure stops safely and may leave already printed choices visible.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T079-unit
cmake --build /tmp/T079-unit -j"$(nproc)"
ctest --test-dir /tmp/T079-unit --output-on-failure

idf.py -C platform/adv build
git diff --check
```

## Manual / hardware validation

Prepare two similar names, for example:

```text
/flash/ft8/RT260925.txt
/flash/ft8/RT260926.txt
```

On ADV and pc-1:

```text
M$> cd /flash/ft8
M$> cat RT<Tab>
# becomes the longest common prefix

<Tab>
# prints both remaining choices
# then restores M$> cat <common-prefix> with cursor intact
```

Also verify:

- typing/paste never auto-completes;
- a unique pathname expands on Tab without printing a list;
- Tab on command name does nothing;
- candidate directory names show `/` only in the list;
- ADV blinking cursor returns correctly;
- Ctrl scrollback can review the candidate output;
- Enter after listing executes only the command line.

No QMX/RF validation is required.

## Codex branch / handoff

Work on:

```text
codex/T079-tab-pathname-choices
```

Start from current `main`.

Read T075-T078 and this task before editing. Keep pathname parsing/filtering
shared in `core/shell_completion.*`; keep Linux/ADV code limited to Tab/list
presentation and edit-region restoration.

Keep T079 to one reviewable implementation commit. Do not merge to `main` and
do not open a PR unless asked.

## Codex implementation notes

### Implementation summary

Implemented one shared Tab operation that expands OR lists. The validation pass
uses shared context parsing, filtering and public FS traversal, computes the
longest prefix, and counts matches (saturating at two). A changed prefix expands
and returns without listing. An unchanged line with multiple matches triggers a
second streaming pass in backend order, including when an extension cannot fit
the 255-byte payload. No candidate table, count cap or cache is used.

Presentation begins only at the first emitted choice. Validation, output-open,
and output-read-before-first-choice failures leave the screen/editor untouched.
Later read/close failures preserve already printed choices and restore the editor,
as authorized by task update `f3720ec`. Every acquired directory is closed.

Linux keeps its raw input session and redraws the same prompt/line/cursor. ADV
ends the transient cursor overlay and discards the draft back to the captured
prompt snapshot before ordinary output. After listing it captures a fresh prompt
snapshot and redraws the original editor with the normal visible blink restart.
The draft is never committed as a submitted command by listing. No deviations.

### Files changed

- `core/shell_completion.c/.h`: shared context/filter/walker, validation and
  expansion-or-list result/callback; original expansion-only wrapper retained.
- `platform/linux/linux_console.c`: lazy list presentation and prompt restoration.
- `platform/adv/adv_console.c`: physical/USB Tab presentation and cursor lifecycle.
- `platform/adv/adv_display.cpp`, `platform/adv/adv_internal.h`: private draft
  discard operation restoring the prompt snapshot before retained console output.
- `tests/shell_completion_test.c`: immutable listing, match contexts/filter/order,
  display types, 100 streamed choices, full-line fallback, handle closure and
  both-pass failures.
- `tests/linux_shell_history.py`: expansion-only first Tab, repeated choice lists,
  raw-mode retention, directory markers and cursor restoration before another token.
- `tests/adv_console_scrollback_test.py`: physical/USB Tab, complete editor/draft/
  history preservation, output cursor suppression, prompt/cursor restoration,
  retained choices, scrollback, no USB replay, output failures and a 255-byte draft.
- `docs/api/console-api.md`, `platform/adv/README.md`: current listing behavior.
- This packet: implementation and validation notes.

### Invariants preserved

Public API/version, T072 Filesystem/CWD, T075 editor/history, T076 keyboard mapping,
normal T077 rendering/blink and application Display ownership are preserved.
T078 matching rules and no-auto-slash behavior remain. Typing/paste are literal;
only Tab requests completion. Listing preserves the complete editor byte-for-byte.
No heap, task/thread/timer, persisted state, private backend directory access,
candidate sorting/caching or pager. Only display-private snapshot restoration
was added; candidate output uses the existing retained console and USB route.

### Local tests run

- `cmake -S . -B build-linux`: passed.
- `cmake --build build-linux -j8`: passed.
- `build-linux/shell_completion_unit`: passed.
- `PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux -R
  'shell_completion_unit|linux_shell_history|adv_console_scrollback'
  --output-on-failure`: passed 3/3.
- `PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure`:
  initial full run passed 126/126. Final rerun after the full-line fallback: passed 126/126 without retries.
- `cmake -S tests/unit -B /tmp/T079-unit` and
  `cmake --build /tmp/T079-unit -j8`: passed.
- `PYTHONDONTWRITEBYTECODE=1 ctest --test-dir /tmp/T079-unit --output-on-failure`:
  initial run passed 29/29; final rerun: passed 29/29.
- `source /home/wei/projects/esp-idf/export.sh` then
  `idf.py -C platform/adv build`: passed initially and after the final core change.
  Final firmware size `0x153890` bytes; 78% app partition space free.
  Existing SDK/dependency warnings remain.
- `git diff --check`: passed.
- Public API, Filesystem/CWD, editor/history and keyboard boundary diff against
  `origin/main`: passed.

The prior T078 ambiguous-prefix no-op assertions were updated narrowly to expect
T079 listings. Zero/one-match, invalid-context and mid-token no-op coverage remains.
Acceptance checkboxes reflect automated evidence, not physical acceptance.

### Manual/hardware validation still required

Architect to run the ADV/pc-1 checklist: first Tab expands, next Tab lists,
repeated listing, visual directory slash, restored cursor and middle edits,
Ctrl scrollback, literal pastes and Enter submitting only the command. No device
was flashed. No QMX/RF test is required.

### Known limitations / risks

Large directories are streamed synchronously with no pager; only the existing
50 physical ADV rows remain in scrollback. Directory contents may change between
validation and output; the second pass reflects the then-current enumeration.
Late output failures may leave partial choices visible, as explicitly accepted;
no completion-specific diagnostic is emitted. Real-device listing latency and
readability still require manual validation.

### Commit

One review commit on `codex/T079-tab-pathname-choices`, titled
`T079: list ambiguous pathname choices on Tab`, based on current main
`f3720ece5b443b18cb42a99c03045118fdc78880`. Exact implementation SHA is supplied
in the Codex handoff; this packet is part of that commit.

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and test evidence.

## Architect test result

Record ADV/pc-1 candidate-list validation here.