# T069 — MiniShell resident startup and brightness settings

Status: READY

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

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and
local test evidence.

## Architect test result

Record real ADV validation and final acceptance here.
