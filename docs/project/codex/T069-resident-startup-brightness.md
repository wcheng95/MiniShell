# T069 — MiniShell resident startup and brightness settings

Status: COMPLETE (startup accepted; brightness superseded by T071)

## Final state — T071

Startup is hardware-accepted and retained unchanged. The architect dropped
brightness; T071 removes its implementation, tests and supported-setting docs.
The brightness requirements and failure record below are historical and no
longer require implementation or hardware validation.

## Architect intent

Extend the existing MiniShell-owned resident settings file:

```text
/flash/minishell/setting.txt
```

with two small resident behaviors:

```text
brightness=100
startup=d;b
```

The goal is to make ADV boot directly into a useful operator workflow while
keeping the mechanism deliberately simpler than a shell startup script.

Example:

```text
startup=ft8;b
```

must launch FT8 during boot, wait normally for the foreground FT8 app to exit,
then execute `b`, and finally leave the operator at the ordinary `M$>` prompt.

## Objective

Add portable MiniShell resident parsing/execution for `startup=` and resident
display brightness policy for `brightness=`, without changing the public
MiniShell API or introducing a scripting language.

## Current context

Read before editing:

```text
AGENTS.md
README.md
docs/README.md
docs/architecture/configuration.md
docs/project/progress.md
docs/project/codex/T025-shell-aliases.md
docs/project/codex/T035-webfs-softap-settings.md
docs/project/codex/T068-webfs-inline-editor.md
core/minishell_runtime.c
core/shell.c
core/platform_backend.h
platform/adv/adv_display.cpp
platform/adv/adv_webfs_settings.c
```

Current relevant behavior:

- `/flash/minishell/setting.txt` is the canonical resident settings file.
- WebFS currently reads `SSID=` and `PW=` from that file.
- WebFS T068 can edit `setting.txt` in the browser and is COMPLETE.
- aliases are loaded live from `/flash/minishell/alias.txt`;
- the resident shell currently dispatches built-ins/apps only from its prompt loop;
- foreground application execution is synchronous, so normal app blocking already
  provides the required `ft8;next-command` sequencing;
- ADV display initialization currently uses the board/library default brightness;
- Linux remains a maintained MiniShell target and must continue to build/test.

The new settings must coexist with the existing WebFS credentials. Do not make
WebFS own startup or brightness semantics.

## Resident setting contract

The supported resident settings now include:

```text
SSID=<WebFS SoftAP name>
PW=<WebFS WPA2 passphrase>
brightness=<1..100>
startup=<MiniShell command>[;<MiniShell command>...]
```

Keys are case-sensitive.

Unknown keys continue to be ignored by features that do not own them.

A missing `setting.txt` must remain a normal condition and must not impair boot.

For duplicate `brightness=` or `startup=` definitions, use the last valid
definition encountered. This matches the project's simple last-definition-wins
configuration style and avoids retaining multiple startup programs.

The existing WebFS SSID/PW parsing and fallback behavior must remain unchanged
when the file also contains `brightness=` and/or `startup=`.

Keep resident settings loading bounded. The current WebFS setting-file limit is
1024 bytes; do not introduce an unbounded whole-file allocation or a larger
resident boot parser merely for these two settings.

## brightness= behavior

Accepted syntax:

```text
brightness=1
brightness=50
brightness=100
```

Requirements:

- decimal integer only;
- accepted range is 1 through 100 inclusive;
- missing or invalid `brightness=` leaves the existing platform/default
  brightness unchanged;
- apply a valid setting once during MiniShell boot after the display exists and
  resident settings are readable, before normal startup-command execution;
- ADV maps the percentage to the native display brightness range monotonically,
  with 100 meaning full/native maximum brightness;
- do not expose this as a new public Display API capability;
- Linux may implement the private resident brightness operation as a no-op /
  unsupported platform behavior; the setting must not break Linux startup.

Brightness is persistent configuration, not a new interactive shell command in
this task.

## startup= behavior

Examples:

```text
startup=d;b
startup=ft8;b
startup=
```

Requirements:

1. `startup=` contains zero or more command segments separated only by literal
   `;`.
2. Execute non-empty segments once, in order, after MiniShell platform/services
   initialization during boot and before the first normal interactive prompt.
3. Each startup segment must pass through the same normal resident-shell command
   execution path used by an interactively entered line.
4. Alias lookup/expansion from `/flash/minishell/alias.txt` therefore applies
   normally to startup commands.
5. Built-ins and application lookup must retain their normal semantics.
6. A foreground application blocks normally. In:
   ```text
   startup=ft8;b
   ```
   the second command must not run until FT8 returns.
7. If one command fails (not found, launch failure, nonzero app return, alias
   expansion failure, etc.), show the same normal shell diagnostic and continue
   with the next startup segment.
8. Missing `startup` or an empty `startup=` executes nothing.
9. Empty segments caused by leading/trailing/repeated semicolons may simply be
   skipped; do not invent syntax or quoting around them.
10. After startup sequencing is complete, enter the ordinary resident shell and
    display the normal `M$>` prompt.
11. Startup runs once per MiniShell boot/session only. Returning from a foreground
    app must not re-run startup.

Whitespace around each semicolon-delimited segment may be handled exactly as
normal shell leading/trailing whitespace is handled. Do not add quoting,
escaping, command substitution, or semicolon parsing anywhere else in the
interactive shell.

The implementation should factor one normal command-dispatch function out of
the existing prompt loop and reuse it for both interactive lines and startup
segments. Do not build a second startup-only command dispatcher.

If the existing `exit` built-in representation requires a dispatcher return
value, keep that control flow explicit. Startup sequencing must still satisfy
the architect requirement that normal boot ends at the interactive prompt;
do not let an accidental startup `exit` create a second scripting/control
language. A minimal "exit request is meaningful only to the interactive loop"
treatment is acceptable.

## Architectural constraints

- Preserve the public MiniShell API at `MINISHELL_API_VERSION 3`.
- Do not add a public Config service.
- Resident settings meaning remains owned by MiniShell core/platform code.
- WebFS only edits/uses its existing credential subset; it does not become the
  startup/brightness runtime owner.
- Applications remain unaware of startup policy.
- Alias semantics from T025 remain unchanged: first `=` separator, one-level
  expansion, built-ins win, duplicate aliases last-wins, live reload.
- Foreground app execution remains synchronous through the existing app manager.
- Keep parsing bounded and suitable for ADV.
- Do not add heap allocation solely for startup parsing.
- Do not move application behavior into MiniShell.
- Platform-specific display mechanics remain below the private platform boundary.

A small private platform hook for applying resident display brightness is allowed
if needed. Do not add brightness to `include/minishell/api.h`.

## Implementation scope

Expected areas include, but are not limited to:

```text
core/minishell_runtime.c
core/shell.c
core/shell.h
core/platform_backend.h
new small core resident-settings parser/module if useful
platform/adv/adv_display.cpp and/or ADV backend glue
platform/linux/... private no-op brightness glue if required
tests/
docs/architecture/configuration.md
README.md and/or docs/README.md where current resident setting examples are listed
```

Prefer a small shared resident-settings parser in core over coupling startup to
the ADV-only `adv_webfs_settings.*` parser. Existing WebFS credential parsing
may remain separate if merging them would broaden the task or risk T035/T068.

## Non-goals

Do **not** implement any of the later shell-usability roadmap in T069:

- relative filesystem paths;
- `cd` / `pwd`;
- Unix-style directory destinations for `cp` / `mv`;
- 10-command editable history;
- pathname automatic longest-unambiguous completion;
- Tab display of remaining pathname choices;
- `clear`;
- command-name auto-completion;
- `vi`;
- generic image `view`;
- `umount`;
- pipes, redirection, variables, `&&`, `||`, quoting, shell functions, or
  any other shell scripting feature;
- changes to WebFS editor behavior;
- application settings migrations;
- public MiniShell API changes.

## Acceptance criteria

- [ ] Existing `SSID=` / `PW=` WebFS behavior still works when the same file
      also contains `brightness=` and `startup=`.
- [ ] Missing `setting.txt` boots normally with no startup commands and existing
      default brightness.
- [ ] Empty `startup=` performs no startup command.
- [ ] `startup=cmd1;cmd2` dispatches both once and in order before the first prompt.
- [ ] Startup commands use normal alias expansion from `alias.txt`.
- [ ] A synchronous foreground app delays the next startup command until it exits.
- [ ] A failed startup command prints its normal diagnostic and later commands run.
- [ ] After startup completes, MiniShell remains at the ordinary interactive prompt.
- [ ] Startup does not run again after a foreground app returns.
- [ ] No startup-only `&&`, `||`, pipe, variable, quoting, or other parser exists.
- [ ] `brightness=1`, a middle value, and `brightness=100` are accepted.
- [ ] Invalid/out-of-range brightness leaves the platform/default value unchanged.
- [ ] ADV applies valid brightness before startup apps run.
- [ ] Linux build/runtime behavior remains valid.
- [ ] Public MiniShell API is unchanged.
- [ ] Existing full Linux CTest suite passes.
- [ ] Real ADV firmware builds successfully.

## Automated tests

Add focused tests for at least:

1. resident settings parsing with existing SSID/PW/unknown keys present;
2. missing file;
3. empty startup;
4. one startup command;
5. multiple startup commands in order;
6. leading/trailing/repeated `;` empty segments;
7. startup alias expansion through the normal alias path;
8. failure of one startup command followed by successful later command;
9. synchronous app-return ordering;
10. startup runs only once before prompt processing;
11. valid brightness boundaries 1 and 100 plus a middle value;
12. invalid decimal/range brightness values;
13. duplicate resident settings use last valid definition;
14. setting-file size/bounded-read edge behavior;
15. no regression to existing WebFS settings tests.

At minimum run:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
```

Also build the real ADV target using the repository's current ESP-IDF environment:

```bash
idf.py -C platform/adv build
```

Run `git diff --check` before handoff.

## Manual / hardware validation

After supervisor review, architect/tester validates on Cardputer ADV.

Use a known-good `alias.txt` and verify examples such as:

```text
startup=ft8;b
```

Expected:

1. boot completes MiniShell initialization;
2. FT8 starts without an intermediate interactive command;
3. FT8 behaves normally as a foreground application;
4. exiting FT8 runs `b` through normal alias/command dispatch;
5. after `b` returns, `M$>` appears and remains interactive.

Also validate:

```text
startup=missing;b
```

The normal `missing: command not found` diagnostic must appear and `b` must
still run.

For brightness, test at least one visibly low value and `brightness=100`,
rebooting between edits to confirm boot-time persistence. Confirm WebFS remains
usable with the combined settings file.

No QMX-specific RF validation is required beyond ensuring startup FT8 reaches its
normal UI/foreground lifecycle when used for the sequencing test.

## Codex branch / handoff

Work on:

```text
codex/T069-resident-startup-brightness
```

Start from current `main`.

Before coding, read `AGENTS.md`, `docs/README.md`, and this task packet.

Keep T069 to one reviewable implementation commit. Do not merge to `main` and
do not open a PR unless the coordinator asks.

Before handoff, fill in the required sections below and push the commit.

## Codex implementation notes

### Implementation summary

Implemented from current `origin/main` at
`6d9b0aa4b8d7c80daf5cef94361ee2a0bc41931e` on the requested branch.
No scope deviations.

A private core loader reads at most 1,024 bytes through MiniShell Filesystem,
probes EOF at the exact limit, closes the handle, and only then parses/applies
settings. Missing/read/close/oversize failures leave defaults. Last valid
`brightness` and `startup` definitions win independently; an empty startup
clears an earlier definition. Unknown keys, including WebFS credentials, remain
outside this parser's ownership.

Boot applies brightness after service initialization and before startup.
ADV uses rounded `(percent * 255 + 50) / 100`; Linux is a private no-op.
The shell's existing dispatch logic is factored into one function used by the
interactive loop and semicolon-separated startup segments. Alias expansion,
built-ins, app manager lifecycle and diagnostics are shared. Startup ignores
exit requests and continues after command failures; interactive exit is unchanged.

### Files changed

- `core/resident_settings.[ch]`: bounded parser and FS loader, no added heap allocation.
- `core/minishell_runtime.c`: once-per-session settings application before prompt.
- `core/shell.[ch]`: shared normal dispatcher and startup segment sequencing.
- `core/platform_backend.h`: private resident brightness operation.
- `platform/adv/adv_backend.c`, `adv_internal.h`, `adv_display.cpp`: private
  brightness routing, ready/range guards and native display mapping.
- `platform/linux/linux_backend.c`: brightness no-op.
- Root and ADV `CMakeLists.txt`: compile the core module and register focused tests.
- `tests/resident_settings_test.c`: parser and fault-injected bounded loader.
- `tests/resident_boot_test.c`: real runtime/shell/app-manager over fake platform,
  including foreground return, live alias reload and failure/exit sequencing.
- `tests/linux_startup.py`: actual Linux runtime, Filesystem and utility apps.
- Existing ADV display/scrollback tests: brightness-capable M5 stubs; display
  test covers all 100 percentages, readiness, invalid ranges and maximum.
- Existing WebFS settings test: combined credentials/startup/brightness file.
- `README.md`, `docs/README.md`, `docs/architecture/configuration.md`: setting
  examples, limits, ownership, defaults and boot behavior.
- This task packet: implementation evidence and review handoff.

### Invariants preserved

Public API v3 and `include/minishell/api.h` are unchanged. No public Config or
Display capability. WebFS production code and credential semantics are unchanged.
Aliases retain first-`=` parsing, live lookup, one expansion, built-in precedence
and last-definition-wins. Applications remain unaware of startup policy and run
synchronously through the existing app manager. Interactive shell syntax is
unchanged; only the boot value is split on semicolons. No relative paths,
completion, history, quoting, scripting or application-setting migration.

The new loader uses a 1,024-byte automatic input buffer; parsed state is 1,028
bytes (1,024-byte startup storage and brightness). There is no new permanent
settings buffer, heap allocation, or task-stack-size change. The loader frame
returns before startup apps execute; boot settings state returns before the
interactive loop. Display mapping is guarded by readiness and valid range.

### Local tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R 'resident_|linux_startup|alias|adv_webfs|adv_display|adv_console'
# PASS: 13/13
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# Initial run: 121/122; unchanged linux_serial_unit PTY timeout assertion at line 67 failed.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R '^linux_serial_unit$'
# PASS: immediate focused rerun, 1/1.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: final full rerun 122/122 (59.75 seconds).
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: real firmware 0x152440 bytes; 78% app partition free. No flashing.
git diff --check
# PASS
```

The initial serial failure was its full-PTY write timeout/zero-byte assertion,
while the ADV build was also running. That test links unchanged services and
Linux serial/common sources, not T069 code. It passed immediately when rerun;
no serial code or assertion was changed. ADV compilation retained existing SDK
`#include_next` and ELF-loader pedantic warnings.

Focused coverage includes all brightness values, malformed decimals, independent
last-valid definitions, CRLF, empty startup, NUL-invalid startup, exact 1,024-byte
EOF, 1,025-byte rejection, partial reads, every partial-read failure position,
failed EOF probe and close failure. Runtime tests assert settings load only once,
brightness before apps, closed settings handles before app entry, app end before
the next command, live alias changes between commands, failed launch/nonzero app
return/not-found/alias overflow continuation, startup exit followed by later
commands and prompt, and no interactive semicolon splitting. Real Linux tests
verify startup copy/read ordering and no startup replay at interactive prompts.

### Manual/hardware validation still required

No device was flashed. On ADV verify `startup=ft8;b`: FT8 starts during boot,
exits through its ordinary lifecycle, then `b` runs and the prompt appears.
Verify `startup=missing;b` diagnostic/continuation, a low brightness and 100
across reboots, and WebFS with the combined settings file. Runtime stack
high-water and physical display brightness remain unmeasured.

### Known limitations / risks

The shared resident file must remain at most 1,024 bytes; larger files disable
boot settings, as they already disable configured WebFS credentials. Startup
segments retain the existing 255-byte command and 16-argument bounds. Overlong
segments are diagnosed and skipped, never truncated. Boot commands intentionally
have no timeout/bypass: a foreground app must return before later commands and
the prompt. Brightness/startup edits take effect next boot/session, whereas
WebFS credentials retain next-WebFS-launch timing. Hardware acceptance is pending.

### Commit

One implementation commit titled `T069: add resident startup and brightness settings`,
parent `6d9b0aa4b8d7c80daf5cef94361ee2a0bc41931e`, on
`codex/T069-resident-startup-brightness`. Exact pushed SHA is returned in the
handoff. No merge or PR.

## Supervisor review

Reviewed `main..6be131b9784372b4639d9a48cd75fc6fb3c6801d` against T069, `AGENTS.md`,
the resident configuration ownership rule, and the public API boundary.

Result: **PASS for software review; advanced to TESTING.**

Findings:

- one bounded implementation commit, exactly one commit ahead of the T069 task
  baseline and no unrelated feature work;
- resident startup parsing is core-owned, bounded to 1,024 bytes, and introduces
  no heap allocation or public Config service;
- interactive and startup commands share the same normal dispatcher, preserving
  alias semantics, built-ins, synchronous app lifecycle and normal diagnostics;
- startup-only behavior is limited to literal `;` sequencing; interactive shell
  syntax did not gain semicolon parsing or other scripting features;
- foreground app return ordering, failure continuation, empty/repeated segments,
  startup `exit` handling, alias live reload and overlong-command rejection are
  covered by focused tests;
- brightness remains a private platform boot policy; ADV performs the 1..100 to
  native display mapping and Linux intentionally ignores it;
- `include/minishell/api.h` has the identical blob SHA on the reviewed parent and
  implementation commit (`13ce3b15fb5b4e1b0047d9559eb9aca91e6312a0`);
- WebFS production code is unchanged and its combined-settings regression test
  passes;
- the initial `linux_serial_unit` PTY timeout is not in a changed code path and
  passed immediately on focused rerun; the final full suite passed 122/122.

Accepted local evidence:

```text
focused tests: 13/13 PASS
final Linux CTest: 122/122 PASS
ADV ESP-IDF build: PASS
git diff --check: PASS
```

No blocking review finding. `main` was fast-forwarded to
`6be131b9784372b4639d9a48cd75fc6fb3c6801d`.

Remaining gate: architect hardware validation on ADV exactly as specified in the
Manual / hardware validation section.

## Architect test result

ADV hardware validation on 2026-09-24:

- `startup=` sequencing: **PASS**. Startup commands execute during boot and the
  normal prompt is reached afterward.
- `brightness=`: **FAIL / not accepted**. Changing the configured value did not
  produce an observable LCD backlight change on the real Cardputer ADV.

The startup portion is accepted. The brightness failure originally blocked
T069; the architect subsequently dropped brightness. T071 supersedes and removes
that portion, so no brightness fix or revalidation remains required.
