# T025 — MiniShell alias.txt command aliases

Status: TESTING

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

- [x] path is exactly `/flash/minishell/alias.txt`;
- [x] first `=` only is separator;
- [x] RHS preserves additional `=`;
- [x] alias may include default arguments;
- [x] user arguments append after alias defaults;
- [x] built-ins keep precedence;
- [x] alias replacement may target built-ins/apps;
- [x] exactly one alias expansion;
- [x] duplicate names: last wins;
- [x] comments/blank/invalid lines safely ignored;
- [x] missing file is silent/no-alias normal operation;
- [x] edits take effect on next lookup without restart;
- [x] overlong expansion is rejected, never truncated;
- [x] fixed buffers/no heap;
- [x] MiniShell FS only;
- [x] Linux tests pass;
- [x] architecture checks pass;
- [x] real ADV build passes;
- [x] no unrelated datetime/history changes.

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

Implemented resident-shell command aliases with pure line/expansion helpers and
MiniShell Filesystem lookup in the shell. Direct built-ins bypass alias lookup;
other command words get at most one replacement before the existing whitespace
split and dispatch. No application, public API, app-manager, or backend logic
changes. Help and README document the resident alias file.

### Files changed

- `core/alias.c`, `core/alias.h`: pure first-separator parser/name matcher and
  bounded replacement-plus-arguments builder.
- `core/shell.c`: built-in precedence, streaming lookup, one expansion, concise
  failure/overflow diagnostics and help line.
- `CMakeLists.txt`, `platform/adv/main/CMakeLists.txt`: shared resident source
  composition and two new Linux tests.
- `tests/shell_alias_test.c`: pure parser/boundary cases and production resident
  shell harness with injected MiniShell FS failures.
- `tests/linux_aliases.py`: real Linux shell/FS/portable-app integration in a
  temporary MINISHELL_ROOT, including live replacement of the alias file.
- `README.md`: shell alias behavior and Filesystem ownership/path.
- This task packet: implementation evidence and REVIEW status.

### Alias parser/lookup semantics

Path is exactly `/flash/minishell/alias.txt`. The first `=` alone separates name
and replacement; additional `=` characters, spaces and tabs on the RHS remain
unchanged until existing shell tokenization. Names must be nonempty and contain
none of the existing shell whitespace characters. Empty replacements, blank,
comment and invalid lines are ignored. LF/one terminal CR are normalized. A final
line without newline is processed. Last valid matching definition wins.

The shell streams 128-byte reads into a 256-byte line buffer, with a bounded
replacement buffer. Overlong or embedded-NUL logical lines are discarded through
the next newline, allowing later valid definitions. Physical alias records must
fit 255 bytes before LF (a CR counts toward that input bound). No file-size buffer,
heap allocation, resident alias table or cache is introduced.

The first input token alone is matched. Remaining user arguments are appended
with one separator space. Expansion must fit SHELL_LINE_MAX (256 bytes including
NUL); overflow prints exactly `alias: expansion too long` and skips dispatch.
The builder leaves its output untouched on failure. Direct exit/help/status/apps/
run remain authoritative, alias targets can dispatch to built-ins/apps, and
`run <app>` does not alias-expand its app argument. Aliases never recurse.

Missing file or unavailable FS is silent. Unexpected open/read/close failure
prints `alias: cannot read alias file` once for that lookup and dispatches the
original, unmodified command. A read/close failure discards even an earlier match;
every successful open receives a close attempt before dispatch. No directory or
file is created automatically.

### Reload behavior

Every non-built-in command lookup reopens/scans/closes the file. Linux integration
runs an alias, replaces alias.txt using the existing `cp` application, and proves
the very next alias invocation uses the new definition in the same shell process.
The resident harness also changes the backing file data between lookups and checks
new results without reinitializing shell state.

### Tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R 'shell_alias|linux_aliases'
# Focused parser/resident and Linux integration: 2/2 PASS
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# Full Linux CTest: 57/57 PASS, including smoke, portable apps, nano,
# directory, resources and architecture checker self-tests

cmake -S tests/unit -B /tmp/T025-build-unit
cmake --build /tmp/T025-build-unit -j8
ctest --test-dir /tmp/T025-build-unit --output-on-failure
# Portable units: 15/15 PASS

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . keyer
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . keyer
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/serial_protocol_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
# All exit 0. Diff inspection confirms resident alias logic uses only MiniShell FS,
# fixed buffers and portable C, with no public API or backend behavior changes.

cmake -S . -B /tmp/T025-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/T025-build-sanitize --target shell_alias_unit -j8
ctest --test-dir /tmp/T025-build-sanitize --output-on-failure -R shell_alias_unit
# ASan/UBSan/leak detection: 1/1 PASS outside the sandbox's known ptrace limitation

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# Real ESP32-S3 firmware PASS; minishell_adv.bin 0xbc3e0 bytes, 88% partition free

git diff --check
# PASS
```

Tests cover CRLF, comments/invalid records, duplicate resolution, arbitrary read
chunk splits, overlong-line recovery, EOF without newline, exact expansion bounds,
argument append and extra equals. Linux integration checks help/apps aliases,
portable cat/cp targets, defaults, built-in precedence including exit, explicit-run
bypass, no recursion, overflow without executing a partial command, and live reload.
The harness injects missing/unavailable FS, unexpected open failure, read failure
after a match, and close failure; all retain original-command dispatch and correct
close/diagnostic counts.

### Manual/hardware validation still required

After supervisor review, perform the task's pc-1 operator check with a/f aliases
and nano edits, and ADV `u=usbmsc` where available. No hardware, USB ownership test,
RF test or WinBook deployment was performed. WinBook deployment after acceptance
must include the reviewed shell executable and all runtime app modules.

### Known limitations / risks

Each lookup scans the entire file to honor last-definition-wins, so unusually
large files add command latency while using bounded memory. The existing 16-argument
whitespace parser is unchanged; no quoting, recursion or scripting is added.
Records beyond the documented input limit are skipped rather than truncated.
No datetime/history work or task-scope deviations.

### Commit

One implementation commit on `codex/T025-shell-aliases`, titled
`T025: add resident shell command aliases`. This packet is included in that commit;
the full pushed SHA is returned in the handoff. No PR or Actions wait.

## Supervisor review

Review exact diff from task head to implementation.


## Supervisor review — alias implementation accepted

PASS on `dab03df7c1778bd867234d4cd63445b6d55b76f1`.

Reviewed the single implementation commit from task head
`2d8d69575447a5b1de1c16b1b0e34129142f7342`.

Accepted ownership:

```text
resident shell
  -> MiniShell Filesystem
  -> /flash/minishell/alias.txt
```

No public API, platform backend, app-manager protocol, MiniFT8, datetime, or history
behavior was changed.

Accepted parser/dispatch behavior:

- first `=` is the only separator;
- additional `=` characters on the RHS are preserved;
- alias names containing shell whitespace are rejected;
- blank/comment/invalid records are ignored;
- duplicate valid aliases use the last definition;
- built-ins `exit/help/status/apps/run` retain precedence;
- alias targets may be built-ins or applications;
- user arguments append after alias defaults;
- expansion occurs exactly once and is never recursive;
- `run <app>` continues to bypass alias lookup for the app name;
- expansion overflow is rejected without partial execution.

Accepted reload/failure behavior:

- no resident alias table or heap allocation;
- every non-built-in lookup reopens/scans/closes the alias file;
- edits therefore take effect on the next command;
- missing/unavailable alias storage is silent;
- unexpected open/read/close errors emit one concise diagnostic and dispatch the
  original command;
- overlong/embedded-NUL logical records are skipped and later valid definitions
  remain discoverable.

Implementation uses fixed buffers and MiniShell Filesystem only. Linux and ADV
share the same resident source.

Accepted automated evidence:

```text
Linux CTest          57/57 PASS
portable units       15/15 PASS
ASan/UBSan alias     PASS
architecture checks  PASS
real ADV build       PASS
git diff --check     PASS
```

No blocking software finding. T025 is TESTING for architect/operator validation.

## Architect test result
