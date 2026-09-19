# T025 — MiniShell alias.txt command aliases

Status: READY

## Architect intent

Implement MiniShell command aliases first. Defer the other resident-shell TODOs:

```text
compact date/time     DEFERRED
command history       DEFERRED
```

Alias file location is fixed:

```text
/flash/minishell/alias.txt
```

Example:

```text
u=usbmsc
f=ft8 --cat serial:/dev/ttyACM0
q=ft8 --profile adv --cat serial:/dev/serial/by-id/usb-QRP_Labs_QMX_Transceiver-if00
```

The first `=` is the only separator. Everything after the first `=` is the alias
replacement text, including any additional `=` characters.

Examples:

```text
x=app --arg=a=b=c
```

must preserve:

```text
app --arg=a=b=c
```

exactly as the replacement text.

## Baseline

Start from current `main`:

```text
c3bd773b834986755301104c8ba3a0c2508b19a4
```

Only `main` exists after repository consolidation.

## Objective

Add resident-shell alias expansion without adding a separate alias application.

The shell command path becomes:

```text
input line
  -> existing direct built-in check
  -> alias lookup for non-built-in command word
  -> one replacement expansion
  -> existing split_args()
  -> built-in/app dispatch
```

Aliases are MiniShell-owned resident configuration, not application configuration.

## Ownership

Implement in resident MiniShell shell/core code.

Allowed ownership:

```text
core/shell.c
core/alias.[ch]              optional/preferred helper
MiniShell Filesystem service
```

Do not put alias parsing in:

- Linux backend;
- ADV backend;
- app_manager;
- an application;
- MiniFT8;
- public MiniShell API.

Use MiniShell Filesystem semantics only. No POSIX/native file I/O in shell alias
logic.

## Alias file semantics

Path:

```text
/flash/minishell/alias.txt
```

### Line format

Each logical line:

```text
<name>=<replacement>
```

Rules:

1. Split on the **first** `=` only.
2. Alias name must be non-empty and one shell token:
   - no spaces;
   - no tabs;
   - no CR/LF;
   - no `=`.
3. Replacement must be non-empty after newline normalization.
4. Everything after the first `=` belongs to the replacement.
5. Additional `=` characters are ordinary replacement characters.
6. Strip line-ending `\n`; strip one terminal `\r` for CRLF input.
7. Blank lines are ignored.
8. Lines whose first non-whitespace character is `#` are comments and ignored.
9. Invalid lines are ignored.
10. Duplicate alias names: **last definition wins**.

Do not implement shell quoting/escaping in T025. Existing MiniShell whitespace tokenization
still applies after expansion.

### Missing/unreadable file

If the file does not exist:

```text
normal shell operation, no aliases
```

This is the normal default.

If the Filesystem service is unavailable, behave the same way.

If the file exists but a read/open/close operation fails for an unexpected reason:

- do not crash;
- do not prevent built-ins/apps from working;
- emit one concise diagnostic for that command lookup;
- dispatch the original command unchanged.

Do not create the directory/file automatically.

## Reload behavior

Do **not** keep a resident alias table.

Look up aliases from `/flash/minishell/alias.txt` at command-dispatch time.

Reason:

- edits made with `nano /flash/minishell/alias.txt` take effect on the next command;
- no reload command is needed;
- no long-lived alias heap/cache;
- Linux and ADV share identical behavior.

Use fixed-size buffers and no heap allocation.

Prefer streaming/line-by-line lookup so file size does not require a whole-file
resident buffer.

A logical alias line longer than the supported shell command buffer may be skipped
safely.

## Expansion behavior

Aliases apply only to the first command token.

Example file:

```text
u=usbmsc
f=ft8 --cat serial:/dev/ttyACM0
x=cat /flash/a=b.txt
```

Commands:

```text
u
f
f --profile adv
x
```

resolve to:

```text
usbmsc
ft8 --cat serial:/dev/ttyACM0
ft8 --cat serial:/dev/ttyACM0 --profile adv
cat /flash/a=b.txt
```

User-supplied arguments are appended to the replacement, separated by one space.

The combined expanded command must fit the existing shell command-line limit
(`SHELL_LINE_MAX`). If it does not fit:

```text
alias: expansion too long
```

and do not execute a partial/truncated command.

## Built-ins

Existing built-in names retain precedence and cannot be overridden:

```text
exit
help
status
apps
run
```

So an `exit=...` line does not replace the real `exit` command.

An alias replacement **may target a built-in**:

```text
h=help
a=apps
```

and should work.

`run <app>` remains the existing explicit app path and does not perform alias lookup
on `<app>`.

## Recursion

T025 performs exactly **one alias expansion**.

Do not recursively expand aliases.

Example:

```text
a=b
b=ls
```

`a` expands to command `b`, and then normal built-in/app dispatch is attempted for
`b`; it is not expanded again.

This avoids loops and keeps behavior deterministic.

## Parser/helper design

A small pure parser helper is preferred, for example:

```text
core/alias.c
core/alias.h
```

Possible responsibilities:

- validate/parse one line;
- compare alias name;
- build replacement + appended user arguments safely.

Filesystem traversal/reading remains shell/resident orchestration.

No heap allocation.

Do not expose alias functionality through `include/minishell/api.h`.

## Help/documentation

Update shell help with one concise line, for example:

```text
aliases           /flash/minishell/alias.txt
```

Update README Shell/Filesystem documentation to mention:

```text
/flash/minishell/alias.txt    MiniShell resident command aliases
```

Do not add datetime/history documentation in this task.

## Tests

### Pure parser tests

Add focused tests for:

- `u=usbmsc`;
- replacement with spaces;
- first-`=` behavior:
  `x=app --arg=a=b=c`;
- CRLF;
- blank/comment lines;
- invalid empty name;
- invalid whitespace in name;
- invalid empty replacement;
- duplicate semantics via lookup/integration;
- exact boundary and overlong expansion;
- user argument append.

### Linux integration

Use a temporary `MINISHELL_ROOT`.

Create:

```text
/flash/minishell/alias.txt
```

with aliases to existing portable apps/built-ins.

Prove:

1. missing file leaves normal shell behavior unchanged;
2. `h=help` invokes help;
3. `a=apps` invokes apps;
4. an app alias executes the target app;
5. alias replacement default arguments work;
6. additional user arguments append after defaults;
7. additional `=` in RHS is preserved;
8. built-in names cannot be overridden;
9. duplicate names use the last definition;
10. recursive alias expansion does not occur;
11. overlong expansion is rejected without running a partial command.

### Live-reload behavior

Prove a change to `alias.txt` is observed by the next alias lookup without restarting
MiniShell.

This may be tested by using an existing app to replace/copy the file between shell
commands or by a focused resident-shell harness.

### Regression

Keep existing shell/application tests green:

```text
linux_smoke
linux_portable_apps
linux_nano
linux_directory
linux_resources
all current CTest
```

ADV build must continue to pass.

## Architecture constraints

- no native/POSIX filesystem calls in alias implementation;
- no Linux/ADV conditional alias behavior;
- no heap allocation;
- no public API change;
- no app-manager protocol change;
- no command history;
- no quoting/parser redesign;
- no environment-variable expansion;
- no pipes/redirection/globbing;
- no recursive aliases.

## Acceptance criteria

- [ ] path is exactly `/flash/minishell/alias.txt`;
- [ ] first `=` only is separator;
- [ ] RHS preserves additional `=`;
- [ ] alias may include default arguments;
- [ ] user arguments append after alias defaults;
- [ ] built-ins keep precedence;
- [ ] alias replacement may target built-ins/apps;
- [ ] exactly one alias expansion;
- [ ] duplicate names: last wins;
- [ ] comments/blank/invalid lines safely ignored;
- [ ] missing file is silent/no-alias normal operation;
- [ ] edits take effect on next lookup without restart;
- [ ] overlong expansion is rejected, never truncated;
- [ ] fixed buffers/no heap;
- [ ] MiniShell FS only;
- [ ] Linux tests pass;
- [ ] architecture checks pass;
- [ ] real ADV build passes;
- [ ] no unrelated datetime/history changes.

## Manual acceptance

On pc-1 create:

```text
/flash/minishell/alias.txt
```

containing at least:

```text
a=apps
u=usbmsc
f=ft8
```

Verify:

```text
M$> a
M$> f
```

and on ADV, where available:

```text
M$> u
```

Edit the file with `nano`, then verify the next command uses the new definition
without restarting MiniShell.

## WinBook deployment after acceptance

WinBook Linux apps are runtime modules under:

```text
build-linux/runtime/apps/*.so
```

After pc-1 rebuilds accepted T025, copy the shell executable and **all** runtime app
modules to WinBook. This is deployment only, not T025 implementation logic.

## Branch workflow

Use:

```text
codex/T025-shell-aliases
```

Codex:

1. implement this task only;
2. run full Linux CTest;
3. run architecture checks;
4. run real ADV build;
5. set Status to REVIEW;
6. record exact files/tests/behavior in this task;
7. commit and push one reviewable implementation commit;
8. return SHA;
9. no PR;
10. no Actions wait.

## Codex implementation notes

### Implementation summary

### Files changed

### Alias parser/lookup semantics

### Reload behavior

### Tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Review exact diff from task head to implementation.

## Architect test result
