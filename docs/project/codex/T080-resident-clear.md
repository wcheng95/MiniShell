# T080 — Resident `clear` built-in

Status: READY

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

- [ ] `clear` is a resident built-in.
- [ ] Built-in precedence prevents an alias from overriding `clear`.
- [ ] `clear` with arguments prints `usage: clear` and does not clear.
- [ ] Linux interactive `clear` clears screen + scrollback and homes cursor.
- [ ] Linux redirected/non-TTY `clear` emits no ANSI escapes.
- [ ] ADV `clear` removes all retained 50-row console output.
- [ ] ADV console offset resets to live tail/zero.
- [ ] ADV edit/cursor overlay state is clean after clear.
- [ ] Next ADV prompt starts at a fresh top-left console.
- [ ] USB mirror receives clear/home behavior.
- [ ] T075 command history survives clear.
- [ ] Previous/Up can recall commands after clear.
- [ ] CWD survives clear.
- [ ] aliases/settings survive clear.
- [ ] T076 scrollback controls remain functional after new output appears.
- [ ] T077 cursor remains normal at the new prompt.
- [ ] T078/T079 Tab completion/listing still work after clear.
- [ ] startup `clear` uses the normal built-in path.
- [ ] Public API and protected service/editor/keymap boundaries remain unchanged.
- [ ] Full Linux CTest passes except any explicitly documented unrelated known flake.
- [ ] ADV firmware builds successfully.

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

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and test evidence.

## Architect test result

Record ADV/pc-1 `clear` validation here.