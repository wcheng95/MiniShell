# T080 — Resident `clear` built-in

Status: TESTING

## Architect intent

Finish this resident-shell usability round with one small built-in:

```text
clear
```

The short form remains an ordinary user alias, for example:

```text
c=clear
```

in:

```text
/flash/minishell/alias.txt
```

Do **not** hard-code `c`, and do **not** add Ctrl+L.

## Objective

Add `clear` as a resident MiniShell built-in that clears the resident console
surface and retained output history while preserving shell/session state.

Required user-visible behavior:

```text
M$> clear

# resident console becomes empty
M$> 
```

The next prompt appears at the top-left of a clean console.

## Semantic boundary

`clear` clears **console output history**, not **command history**.

Preserve:

- the T075 10-command history ring;
- current history navigation/draft semantics;
- T072 session CWD;
- aliases and settings;
- foreground application state/lifecycle;
- Filesystem handles/resources owned by the runtime;
- T076 key mapping;
- T077 cursor behavior;
- T078/T079 Tab pathname behavior.

Therefore, after executing `clear`, Previous/Up may still recall `clear` and
earlier commands.

## Resident architecture

`clear` is a shell built-in, like `cd`, `pwd`, `help`, `status`, `apps`, `run`
and `exit`.

Add it to the built-in-name table so an alias named `clear` cannot override it.

Add one private platform operation, for example:

```c
void minishell_platform_console_clear(void);
```

to `core/platform_backend.h`.

This remains private resident-shell infrastructure. Do not add anything to
`include/minishell/api.h`; public API version remains 3.

## Shell command behavior

Accept exactly:

```text
clear
```

With extra arguments:

```text
M$> clear anything
usage: clear
```

and do not clear the console.

`clear` executes through the normal `execute_line()` path, so it also works if
used in the existing startup sequence:

```text
startup=clear;ft8
```

No special startup handling is added.

## Linux semantics

For an interactive terminal, clear both the visible screen and terminal
scrollback using the existing ANSI-terminal assumption already used by the
resident editor.

A simple fixed sequence is acceptable, for example the equivalent of:

```text
CSI 2 J    clear visible screen
CSI 3 J    clear terminal scrollback
CSI H      cursor home
```

Do not add terminfo/ncurses.

After `clear` returns, the shell loop prints the next normal `M$> ` prompt.

When stdout is not a TTY, `clear` should be a silent no-op:

- emit no ANSI escape bytes;
- do not disturb redirected output;
- return normally so later redirected commands continue.

Do not change Linux raw-mode ownership. `clear` executes after interactive line
submission, when the resident line reader has already restored terminal mode.

## ADV semantics

ADV `clear` resets the **resident console** to a fresh state:

- retained 50-row console history becomes one blank row;
- console column becomes 0;
- console scroll offset becomes 0;
- console mode becomes active;
- no stale edit/cursor overlay remains;
- TFT is redrawn blank;
- USB resident-console mirror receives the same ANSI clear/home intent so a
  connected terminal also appears cleared.

Then the normal shell loop prints a fresh `M$> ` prompt at row 0/column 0.

Do not implement this through the public application Display `clear()` call.
`clear` owns resident-console state, so use a private ADV console/display helper.

The command itself need not remain in the 50-row **output** history after clear;
that history is intentionally erased. It still remains in T075 **command**
history.

## Interaction with T079 choice listing

If pathname choices were just listed, `clear` removes them along with all other
retained console output.

After clear:

- Tab completion still works;
- candidate lists can populate the fresh scrollback normally;
- Ctrl+`;` / Ctrl+`.` see no older pre-clear output.

## Alias rule

Do not ship a compiled alias `c`.

Documentation may show the recommended optional user alias:

```text
c=clear
```

Existing T025 alias parsing/precedence remains unchanged.

Because `clear` is a built-in, this:

```text
clear=something-else
```

must not override the built-in.

## Help / documentation

Update resident help and current shell documentation to include:

```text
clear             clear resident console and output history
```

Document optional:

```text
c=clear
```

only as a user alias example, not as built-in behavior.

Update at minimum as appropriate:

```text
README.md
docs/api/console-api.md
platform/adv/README.md
docs/project/progress.md   # only when supervisor advances status
```

## Public/private invariants

- `MINISHELL_API_VERSION` remains 3.
- `include/minishell/api.h` unchanged.
- T072 Filesystem/CWD code unchanged.
- T075 editor/history code unchanged.
- T076 ADV keyboard mapping unchanged.
- T078/T079 completion core unchanged unless tests require only call-site adaptation.
- No application Display API change.
- No heap allocation.
- No worker/task/timer.
- No persistence.

## Non-goals

Do not implement:

- Ctrl+L;
- hard-coded alias `c`;
- a general terminal-control service;
- public Console clear API;
- application Display clearing changes;
- command-history clearing;
- CWD reset;
- alias reset;
- filesystem remount/reset;
- terminal-size probing beyond what already exists;
- customizable clear sequences.

## Acceptance criteria

- [x] `clear` is a resident built-in.
- [x] Built-in precedence prevents an alias from overriding `clear`.
- [x] `clear` with arguments prints `usage: clear` and does not clear.
- [x] Linux interactive `clear` clears screen + scrollback and homes cursor.
- [x] Linux redirected/non-TTY `clear` emits no ANSI escapes.
- [x] ADV `clear` removes all retained 50-row console output.
- [x] ADV console offset resets to live tail/zero.
- [x] ADV edit/cursor overlay state is clean after clear.
- [x] Next ADV prompt starts at a fresh top-left console.
- [x] USB mirror receives clear/home behavior.
- [x] T075 command history survives clear.
- [x] Previous/Up can recall commands after clear.
- [x] CWD survives clear.
- [x] aliases/settings survive clear.
- [x] T076 scrollback controls remain functional after new output appears.
- [x] T077 cursor remains normal at the new prompt.
- [x] T078/T079 Tab completion/listing still work after clear.
- [x] startup `clear` uses the normal built-in path.
- [x] Public API and protected service/editor/keymap boundaries remain unchanged.
- [x] Full Linux CTest passes except any explicitly documented unrelated known flake.
- [x] ADV firmware builds successfully.

## Automated tests

Extend shell/builtin tests to cover:

1. `clear` dispatch;
2. `clear extra` usage/no clear;
3. built-in precedence over alias `clear=...`;
4. startup `clear` uses normal dispatch;
5. command history still contains submitted commands after clear;
6. CWD unchanged by clear.

Linux PTY integration:

- produce visible output;
- execute `clear`;
- verify clear-screen + clear-scrollback + home ANSI sequence;
- verify fresh prompt;
- recall prior command with Up after clear;
- verify normal cursor/history/Tab behavior still works;
- redirected `clear\npwd\nexit\n` contains no ANSI escapes.

ADV host-console tests:

- populate more than 7 and preferably 50 retained rows;
- move into scrollback;
- execute private resident clear through the real shell/console path;
- verify history resets to one blank/current prompt row;
- verify pre-clear rows are unreachable with Ctrl scrollback;
- verify USB clear/home sequence appears once;
- verify cursor overlay is clean and new prompt/editing works;
- verify Up/Fn-history still recalls commands because shell history is separate;
- verify T079 choice listing works after clear.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T080-unit
cmake --build /tmp/T080-unit -j"$(nproc)"
ctest --test-dir /tmp/T080-unit --output-on-failure

idf.py -C platform/adv build
git diff --check
```

## Manual / hardware validation

On ADV:

```text
M$> help
M$> status
M$> clear
```

Verify only a fresh prompt remains. Ctrl+`;` should not reveal pre-clear output.

Then verify:

```text
Fn+;             # previous command history still works
pwd              # CWD unchanged
cat RT<Tab>      # T078/T079 still behaves normally in a directory with fixtures
```

On pc-1:

- run a few commands;
- `clear` visibly clears the terminal and scrollback;
- Up still recalls command history;
- CWD remains unchanged;
- Tab completion/listing still works.

Optional operator setup after acceptance:

```text
echo 'c=clear' >> /flash/minishell/alias.txt
```

Use WebFS/nano instead if preferred; this alias is not part of T080 code.

No QMX/RF validation is required.

## Codex branch / handoff

Work on:

```text
codex/T080-resident-clear
```

Start from current `main`.

Read T075-T079 and this packet before editing. Keep the change small: resident
dispatch + one private platform clear operation + focused platform/tests/docs.

Keep T080 to one reviewable implementation commit. Do not merge to `main` and
do not open a PR unless asked.

## Codex implementation notes

### Implementation summary

Added `clear` to the resident built-in table, normal dispatch and help. Extra
arguments print `usage: clear` without invoking the platform operation. Built-in
precedence prevents an alias named `clear` from overriding it; `c` remains an
ordinary optional user alias. Startup uses the same dispatch path.

Added one private platform clear operation. Linux checks stdout TTY status, then
emits screen-clear, scrollback-clear and cursor-home once and resets prompt line
tracking; redirected output is untouched. ADV removes its edit overlay, clears
retained console cells and the edit snapshot, resets history to one blank row and
zero column/offset, redraws the TFT, and sends clear/home once to the USB mirror.
The next normal prompt starts at the top-left. No deviations from the task.

### Files changed

- `core/shell.c`: built-in precedence, argument validation, dispatch and help.
- `core/platform_backend.h`: private console-clear declaration.
- `platform/linux/linux_console.c`: TTY-only terminal clear and prompt tracking.
- `platform/adv/adv_console.c`: cursor cleanup, resident clear and USB clear/home.
- `platform/adv/adv_display.cpp`, `platform/adv/adv_internal.h`: private resident
  history/snapshot reset and blank redraw, without public Display calls.
- `tests/shell_alias_test.c`: dispatch, usage, built-in precedence, optional alias,
  absence of a compiled `c` alias, and startup dispatch.
- `tests/resident_boot_test.c`: private clear stub for the existing runtime test.
- `tests/linux_shell_history.py`: real PTY clear sequence, fresh prompt, history,
  CWD/aliases, argument rejection, choice listing after clear, redirected and
  startup clear; corrected Ctrl-C submission helper.
- `tests/adv_console_scrollback_test.py`: links actual shell/alias code so clear
  dispatch reaches the real ADV console/display path; checks full retained-ring
  reset, overlay/snapshot cleanup, USB output, history recall and fresh choices.
- `README.md`, `docs/api/console-api.md`, `platform/adv/README.md`: clear semantics
  and optional user alias example.
- This packet: implementation and validation evidence.

### Invariants preserved

Public API/version, Filesystem/CWD implementation, editor/history primitives,
ADV keyboard mapping and T078/T079 completion core are unchanged. No runtime
resource, application lifecycle, alias/settings or command-history reset occurs.
The normal cursor and Tab paths remain. No Ctrl+L, compiled `c`, heap allocation,
worker/task/timer or persistence. Public application Display behavior is unchanged.

### Local tests run

- `cmake -S . -B build-linux`: passed.
- `cmake --build build-linux -j8`: passed after adding the new private operation
  stub required by the existing resident boot fixture.
- `PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux -R
  'shell_alias_unit|resident_boot_unit|linux_shell_history|adv_console_scrollback'
  --output-on-failure`: final run passed 4/4.
- `PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure`:
  passed 125/126. The documented unrelated `linux_serial_unit` PTY saturation
  assertion at line 67 failed (`MINI_ERR_TIMEOUT && n == 0`).
- Focused `ctest --test-dir build-linux -R '^linux_serial_unit$' --output-on-failure`:
  failed at the same assertion. No serial test/implementation
  changes were made; this known failure remains the full-suite exception.
- `cmake -S tests/unit -B /tmp/T080-unit` and
  `cmake --build /tmp/T080-unit -j8`: passed.
- `PYTHONDONTWRITEBYTECODE=1 ctest --test-dir /tmp/T080-unit --output-on-failure`:
  passed 29/29.
- `source /home/wei/projects/esp-idf/export.sh` then
  `idf.py -C platform/adv build`: passed. Firmware size `0x1539a0` bytes;
  78% application partition free. Existing SDK/dependency warnings remain.
- `git diff --check`: passed.
- Protected-boundary diff against `origin/main` for public API, Filesystem/CWD,
  editor/history, completion core and ADV keyboard: passed.
- Serial target inputs (test, serial/common platform code, platform header,
  service sources, public headers and CMake definition) unchanged from main.

The initial PTY test exposed an existing helper assumption: appending Enter after
Ctrl-C queued an extra empty command and could desynchronize prompt reads. The
helper now sends Ctrl-C without an additional newline; the shell implementation
and assertions remain intact. Focused Linux rerun passed afterward. Acceptance
checkboxes reflect software evidence, with the documented serial-test exception;
physical acceptance remains pending.

### Manual/hardware validation still required

Architect to check ADV and pc-1: visible screen/scrollback clearing, fresh prompt,
no pre-clear rows via Ctrl scrollback, preserved command recall/CWD, normal cursor,
and Tab expansion/listing afterward. No device was flashed. Optional `c=clear`
operator setup is not shipped or written by this task. No QMX/RF test required.

### Known limitations / risks

Linux clearing relies on the existing ANSI terminal assumption; actual scrollback
behavior should be checked on pc-1. ADV TFT readability and connected USB terminal
behavior still need hardware validation. The known Linux serial PTY assertion
remains intermittent and failed in this run, as detailed above.

### Commit

One review commit on `codex/T080-resident-clear`, titled
`T080: add resident clear built-in`, based on current main
`41e1eee7136a934446e0546f65cc67e79afaa4fe`. Exact implementation SHA is supplied
in the Codex handoff; this packet is included in that commit.

## Supervisor review

Reviewed `main..9cea4380dc4a735e8dee1a244dd40a625ad2215e` against T080.

Result: **PASS for software review; advanced to TESTING.**

Findings:

- `clear` is a true resident built-in and is protected from alias override;
- `clear extra` prints `usage: clear` and does not invoke the platform clear;
- optional `c=clear` remains an ordinary user alias; no compiled `c` exists;
- Linux emits `CSI 2J`, `CSI 3J`, `CSI H` only when stdout is a TTY;
- redirected/non-TTY Linux clear is a silent no-op with no ANSI bytes;
- ADV clears retained resident output history, scroll offset, column, edit snapshot, and cursor overlay, then leaves the next normal prompt at a fresh top-left console;
- ADV USB mirror receives the clear/home sequence exactly once;
- T075 command history is independent and preserved;
- CWD, aliases/settings, app lifecycle and runtime resources are preserved;
- T078/T079 pathname completion/listing remains unchanged;
- startup uses the normal `execute_line()` path.

Protected boundaries verified unchanged:

```text
include/minishell/api.h                  13ce3b15fb5b4e1b0047d9559eb9aca91e6312a0
core/shell_editor.c                      b17fa770aa9c4bba2702b30a6eabbc1a5cf61cff
core/shell_editor.h                      a4f15ffc468eb5fc16942cd648588cfff99749b9
core/minishell_services/filesystem_service.c
                                         315854e9b7c73c5dead0542d084221ad5feeeb4b
core/shell_completion.c                  4ee514a14cc71530b6c4264ccab8affd01a0a2d8
platform/adv/adv_keyboard.cpp            5afceeb97b3fee479b2a30238286dc0083607b44
```

Accepted local evidence:

```text
focused T080 tests: 4/4 PASS
portable unit tests: 29/29 PASS
ADV ESP-IDF build: PASS
Linux full CTest: 125/126; linux_serial_unit timeout only
git diff --check: PASS
```

The failing `linux_serial_unit` target is outside the T080 diff and matches the
known PTY saturation timeout issue. Serial implementation/tests and their inputs
are unchanged, so this does not block T080 manual validation.

`main` was fast-forwarded to:

```text
9cea4380dc4a735e8dee1a244dd40a625ad2215e
```

Remaining gate: manual ADV and pc-1 validation of visible clear behavior,
scrollback reset, preserved command history/CWD, and normal Tab behavior.

## Architect test result

Record ADV/pc-1 `clear` validation here.