# T072 — Resident CWD, cd/pwd, and relative filesystem paths

Status: COMPLETE

## Architect intent

Make MiniShell filesystem use less typing on Cardputer ADV while preserving the
existing portable Filesystem boundary.

Accepted operator behavior:

```text
M$> cd /flash/ft8
M$> pwd
/flash/ft8
M$> ls .
M$> nano setting.txt
M$> cp RT260925.txt /sd/.
```

This task adds a single MiniShell-session current working directory and relative
path resolution. It does **not** add Unix `cp`/`mv` directory-destination
semantics; that is a separate follow-up task.

## Objective

Add:

- resident shell built-ins `cd <path>` and `pwd`;
- one normalized session CWD, default `/`;
- relative path support for every public MiniShell Filesystem operation;
- inheritance of that CWD by the one foreground application;
- no public API struct/version change.

## Current context

Read before editing:

```text
AGENTS.md
README.md
docs/README.md
docs/api/filesystem-api.md
docs/architecture/architecture.md
docs/project/progress.md
docs/project/codex/T071-drop-brightness.md
core/shell.c
core/shell.h
core/minishell_services/minishell_services.h
core/minishell_services/filesystem_service.c
core/minishell_services/filesystem_paths.c
core/minishell_services/filesystem_internal.h
tests/unit/test_filesystem.c
tests/shell_alias_test.c
tests/resident_boot_test.c
```

Current behavior:

- the Filesystem service currently accepts only absolute logical paths;
- `filesystem_path_normalize()` rejects any path not beginning with `/`;
- `.` and `..` are already normalized inside absolute logical paths;
- backends receive normalized MiniShell absolute paths;
- one foreground app runs synchronously at a time;
- resident aliases and T069 startup commands use the normal shared shell dispatcher;
- `docs/api/filesystem-api.md` explicitly lists working directory / `cd` as deferred.

T072 intentionally changes that last point because the architect now has a concrete
ADV usability requirement.

## Architecture decision

The CWD belongs to the **portable MiniShell Filesystem service/session**, not to
Linux, FATFS, the ADV backend, or an application.

Rationale:

- shell-only argv rewriting is not sufficient: MiniShell cannot know which
  arbitrary application arguments are filesystem paths;
- applications already use the public Filesystem API, so relative paths must be
  resolved at that semantic boundary;
- backends should continue receiving only normalized absolute MiniShell paths;
- one foreground app at a time means one session CWD is sufficient and KISS.

The shell owns the user commands that change/query CWD. The Filesystem service
owns storage of the normalized CWD and resolution of relative paths.

Use private core/service functions for shell `cd` / `pwd`; do not add public CWD
function pointers to `mini_fs_api_t`.

## CWD lifetime

- Default CWD is `/` when the Filesystem service is configured for a new
  MiniShell session.
- `cd` changes it for the remainder of that session.
- Launching/exiting foreground apps does **not** reset it.
- Startup commands share the same CWD as the later interactive prompt.
- Service/runtime reconfiguration for a fresh session resets it to `/`.
- CWD is RAM-only; do not persist it in `setting.txt`.

Example:

```text
startup=cd /flash/ft8;ft8
```

must launch FT8 with `/flash/ft8` as the active Filesystem CWD and leave the
interactive prompt in `/flash/ft8` after FT8 exits.

## Relative path semantics

Absolute paths retain their existing behavior byte-for-byte at the backend
boundary.

For a relative path, resolve it against the current CWD, then apply the existing
MiniShell normalization rules before invoking the backend.

Examples with CWD `/flash/ft8`:

```text
setting.txt        -> /flash/ft8/setting.txt
./setting.txt      -> /flash/ft8/setting.txt
../minishell       -> /flash/minishell
.                  -> /flash/ft8
/sd/log.txt        -> /sd/log.txt
```

All path-taking public Filesystem operations must use the same resolver:

```text
open
stat
rename (both old and new paths)
remove_file
mkdir
rmdir
dir_open
space
```

Backend callbacks in `minishell_services_port_t` must still receive normalized
absolute logical paths only. Do not move CWD behavior into Linux/ADV backends.

Preserve the existing normalized path maximum and error behavior. Relative paths
that cannot be represented safely within the logical root/path bound must fail;
never truncate.

Do not weaken the existing protection against escaping the logical `/` namespace.

## Private service interface

A small private interface such as the following is appropriate:

```c
mini_result_t minishell_filesystem_cwd_set(const char *path);
mini_result_t minishell_filesystem_cwd_get(char *out, size_t capacity);
```

Exact private names may differ, but:

- `cwd_set` must resolve the supplied absolute or relative path;
- it must verify that the resolved target exists and is a directory;
- failure must leave the old CWD unchanged;
- `cwd_get` returns the canonical absolute logical path;
- no heap allocation is required.

Keep these functions core-private; applications continue to see only
`mini_fs_api_t`.

## Shell built-ins

Add built-ins:

```text
cd <path>
pwd
```

`cd` requirements:

- exactly one path argument;
- path may be absolute or relative;
- successful target must exist and be a directory;
- failed `cd` leaves CWD unchanged;
- no HOME concept and no bare-`cd` shortcut in this task;
- concise diagnostics are sufficient, but distinguish usage from a failed
  directory change.

`pwd` requirements:

- takes no arguments;
- prints the canonical absolute CWD plus newline.

Add both names to built-in precedence so aliases cannot override `cd` or `pwd`.

Update resident `help` accordingly.

Because startup and interactive input already share one dispatcher, `cd` and
`pwd` must work through both paths without a startup-only implementation.

## Public Filesystem contract

`mini_fs_api_t` layout and `MINISHELL_API_VERSION` remain unchanged.

However, this is an intentional semantic extension of the Filesystem path
contract: application paths may now be absolute or relative.

Update `docs/api/filesystem-api.md` to document:

- one MiniShell session CWD;
- default `/`;
- relative resolution against CWD;
- app inheritance;
- backends still receive absolute normalized paths;
- `cd`/`pwd` remain resident shell commands rather than public FS function
  pointers.

Remove `working directory / cd` from the deliberately-deferred list.

## Important invariants

- Existing absolute-path applications must behave unchanged.
- Existing path hashing/exclusive-writer behavior must hash the resolved absolute
  path so relative and absolute aliases collide correctly.
- Example: with CWD `/sd`, `open("read.txt")` and `open("/sd/read.txt")`
  identify the same logical path for handle ownership.
- Quota/accounting must use resolved absolute paths exactly as before.
- Root destructive-operation protections remain unchanged.
- Filesystem handles still belong to the foreground app and are reclaimed on
  app exit; CWD itself is resident session state and survives app exit.
- Alias file and resident settings paths remain absolute and are unaffected.
- WebFS remains absolute-path based and unaffected.

## Scope examples

Expected after `cd /flash/ft8`:

```text
cat setting.txt
nano setting.txt
ls .
mkdir tmp
rm old.txt
mv a.txt b.txt
cp a.txt ../backup.txt
```

The last two still use their **existing** source/destination semantics; T072 only
makes relative paths resolve.

## Non-goals

Do not implement in T072:

- Unix-style `cp file DIR` / `mv file DIR` destination expansion;
- command history;
- pathname auto-completion;
- Tab choice display;
- `clear`;
- command-name completion;
- globbing/wildcards;
- environment variables or `$PWD`;
- `~` / HOME expansion;
- per-app or per-process CWD;
- persistent CWD;
- directory stack (`pushd`/`popd`);
- shell scripting beyond the already-accepted startup `;` sequencing;
- `vi`, `view`, or `umount`.

Do not add generic shell quoting or argument parsing changes.

## Acceptance criteria

- [ ] New session CWD is `/`.
- [ ] `pwd` initially prints `/`.
- [ ] `cd /flash/ft8` changes CWD when the directory exists.
- [ ] `cd ..` resolves relative to the current CWD using normal path rules.
- [ ] `cd .` is harmless.
- [ ] `cd` to a file or missing path fails without changing CWD.
- [ ] `cd`/`pwd` built-ins cannot be shadowed by aliases.
- [ ] Startup `cd` changes the same CWD later used by apps/prompt.
- [ ] App begin/end does not reset CWD.
- [ ] Fresh service/session configure resets CWD to `/`.
- [ ] Every public Filesystem path-taking operation accepts relative paths.
- [ ] Relative and absolute aliases resolve to the same backend absolute path.
- [ ] Relative/absolute aliases share path-hash writer exclusion correctly.
- [ ] Backends still receive only normalized absolute paths.
- [ ] Existing absolute path normalization/root protection is preserved.
- [ ] `MINISHELL_API_VERSION` and public struct layout are unchanged.
- [ ] Existing WebFS/startup behavior is unchanged.
- [ ] Full Linux CTest passes.
- [ ] ADV firmware builds successfully.

## Automated tests

Extend focused Filesystem unit coverage for at least:

1. initial CWD `/`;
2. private cwd get/set;
3. set absolute directory;
4. set relative child/parent directory;
5. failed set to missing path;
6. failed set to regular file;
7. CWD unchanged after failed set;
8. relative `open`;
9. relative `stat`;
10. relative `rename` old/new;
11. relative remove/mkdir/rmdir;
12. relative `dir_open`;
13. relative `space`;
14. `.` and `..` components;
15. name/path length boundary;
16. root-escape rejection;
17. writer alias collision between relative and absolute spellings;
18. CWD survives app end/begin;
19. CWD resets on fresh service configuration.

Add shell/runtime integration coverage for:

```text
pwd
cd /flash/ft8
pwd
cd ..
pwd
failed cd
alias attempting to override cd/pwd
startup=cd /flash/ft8;<app using relative path>
```

Use actual Linux runtime integration where useful to prove a portable app can
open a relative path without app-specific changes.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
idf.py -C platform/adv build
git diff --check
```

## Manual / hardware validation

After supervisor review, architect/tester validates on ADV:

```text
M$> pwd
/
M$> cd /flash/ft8
M$> pwd
/flash/ft8
M$> ls .
M$> nano setting.txt
```

Exit nano and confirm:

```text
M$> pwd
/flash/ft8
```

Also verify a failed `cd` leaves the previous directory unchanged.

No QMX/RF validation is required.

## Codex branch / handoff

Work on:

```text
codex/T072-shell-cwd-relative-paths
```

Start from current `main`.

Before coding, read `AGENTS.md`, `docs/README.md`, the Filesystem API document,
and this task packet.

Keep T072 to one reviewable implementation commit. Do not merge to `main` and
do not open a PR unless asked.

## Codex implementation notes

### Implementation summary

Implemented from current `origin/main` at
`1070b389303839f707560fd1729cecdb71fbbf46` on the requested branch.

The portable Filesystem service owns a canonical session CWD. It resets to `/`
on configuration, survives foreground app begin/end, and is accessed by private
core get/set functions. Set resolves its argument and validates an existing
directory before replacing CWD. Every public path-taking operation uses the same
resolver, including both rename paths, before backend access and path hashing.
Absolute paths retain the existing normalization path; relative normalization is
seeded with CWD without constructing an unbounded intermediate string.

The shared shell dispatcher implements `cd <path>` and `pwd`, with exact argument
counts, concise errors, help text and built-in alias precedence. Startup uses that
same dispatcher. No shell argv rewriting or public API extension.

One minimal application adjustment was necessary for the packet's explicit
`nano setting.txt` requirement: nano previously rejected non-absolute arguments
before calling FS. It now accepts a nonempty path and delegates all resolution to
the Filesystem service. No application-specific CWD or path resolver was added.
This implements the required scope example, not an architecture deviation.

### Files changed

- `core/minishell_services/filesystem_paths.c`, `filesystem_internal.h`: common
  absolute/relative normalization and shared private path bound.
- `core/minishell_services/filesystem_service.c`, `minishell_services.h`: session
  CWD state, private get/set and resolution in all path-taking operations.
- `core/shell.c`: shared-dispatch `cd`/`pwd`, help and built-in precedence.
- `apps/nano/main/nano.c`: allow nonempty relative path arguments; update usage.
- `tests/unit/test_filesystem.c`: CWD operations/lifetime, relative operations,
  ownership aliases, root protection and path bounds; update two obsolete
  relative-path rejection expectations to normal backend not-found results.
- `tests/linux_cwd.py`, root `CMakeLists.txt`: real runtime/app integration test.
- `tests/linux_nano.py`: preserve absolute save test and additionally exercise
  relative save and CWD after nano exit.
- `tests/linux_export_boundary.py`: include private CWD symbols in export checks.
- `tests/shell_alias_test.c`, `resident_boot_test.c`: private CWD stubs for existing
  isolated shell/runtime tests; real semantics covered by FS and Linux tests.
- `README.md`, `docs/README.md`, `docs/api/filesystem-api.md`: public semantic
  extension, resident commands, lifecycle, examples and unchanged destination rules.
- This task packet: implementation and handoff evidence.

### Invariants preserved

`include/minishell/api.h` is unchanged: API v3 and table layouts are identical.
Backend Filesystem callbacks continue to receive canonical absolute logical
paths. The hash and quota layers still see resolved absolute paths; readers and
writers cannot bypass ownership via relative/absolute spellings. Root destructive
operation checks and root-escape rejection remain intact. The 512-byte path buffer
includes the terminator; no truncation or heap allocation was added.

CWD adds one 512-byte resident buffer. It is not app-owned, persisted, or exposed
as a public application capability. Alias/settings files remain absolute. App
handle cleanup remains unchanged. WebFS production code, startup sequencing,
foreground blocking and alias expansion depth remain unchanged. No platform
backend changes. `cp`/`mv` directory destinations, interactive shell parsing,
HOME expansion, history and completion remain outside scope.

### Local tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R 'linux_cwd|linux_nano|linux_export|alias|resident_|linux_startup|adv_webfs'
# PASS: 14/14 focused tests.
cmake -S tests/unit -B /tmp/T072-unit
cmake --build /tmp/T072-unit -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir /tmp/T072-unit --output-on-failure
# PASS: 27/27, including expanded Filesystem service unit tests.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# Initial run: 122/123; serial PTY timeout assertion failed at linux_serial_test.c:67.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R '^linux_serial_unit$'
# PASS: immediate focused rerun 1/1.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: final full run 123/123 (59.52 seconds).
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: real firmware 0x1527d0 bytes; app partition 78% free. No flashing.
git diff --check
# PASS
```

ADV compilation retained existing SDK/M5/ELF-loader pedantic warnings and
completed successfully.

The initial full suite hit the same PTY full-output-queue timeout/zero-byte
assertion recorded during T069. Its focused rerun passed immediately. Serial
implementation/test code is unchanged; no assertion was relaxed.

Focused coverage checks default/reset/preserved CWD, failed changes, get-buffer
bounds, every relative FS operation, both rename paths, quota and native space
queries, 511-byte paths and overflow, dot/parent/root behavior, and writer alias
collisions in both directions. Linux integration verifies built-in precedence,
aliases targeting cd/pwd, startup CWD inheritance, portable cat/ls/cp/mv/mkdir/rm/
rmdir, command usage errors, file/missing cd failure preservation, app returns and
new-session reset. The nano PTY test covers actual relative editing/save/exit as
well as its original absolute-path flow.

### Manual/hardware validation still required

After supervisor review, on ADV verify initial `pwd`, `cd /flash/ft8`, `pwd`,
`ls .`, and `nano setting.txt`; exit nano and confirm CWD remains `/flash/ft8`.
Confirm failed cd leaves it unchanged. No hardware flashed or exercised here;
no QMX/RF validation required.

### Known limitations / risks

CWD is a single session path, not an open directory handle. Removing its directory
or losing its volume can make subsequent relative operations fail until a valid
absolute cd; no new volume/mount policy was added. Shell input remains limited to
255 bytes even though the Filesystem path bound is 511 bytes. Apps with their own
path policies may still choose absolute paths. `ls` without arguments retains
its existing root default; use `ls .` for CWD. Hardware acceptance is pending.

### Commit

One implementation commit titled `T072: add session CWD and relative filesystem paths`,
parent `1070b389303839f707560fd1729cecdb71fbbf46`, on
`codex/T072-shell-cwd-relative-paths`. Exact pushed SHA is returned in the handoff.
No merge or PR.

## Supervisor review

Reviewed `main..76995dcc790e3be7a564a83dd6221125f50f9bdf` against T072,
`AGENTS.md`, the Filesystem ownership contract, and the current shell/runtime
architecture.

Result: **PASS for software review; advanced to TESTING.**

Findings:

- exactly one bounded implementation commit, one commit ahead of the T072 task
  baseline;
- CWD is owned by the portable Filesystem service, not by Linux/ADV backends or
  individual applications;
- one canonical 512-byte session CWD defaults to `/`, survives app begin/end,
  and resets only on service/session reconfiguration;
- every public path-taking Filesystem operation resolves relative paths before
  backend access, including both sides of rename and `space()`;
- backends continue receiving normalized absolute MiniShell logical paths only;
- path hashing and writer-exclusion operate on the resolved absolute path, so
  relative/absolute aliases cannot bypass ownership;
- root-escape and destructive-root protections remain intact;
- `cd` / `pwd` are resident built-ins, participate in the shared startup/
  interactive dispatcher, and retain built-in precedence over aliases;
- the nano change only removes its old absolute-path precondition and delegates
  resolution to the Filesystem service; it does not implement a second CWD model;
- existing `cp` / `mv` destination semantics are unchanged, as required;
- no platform backend implementation changed;
- `include/minishell/api.h` is byte-for-byte unchanged (blob
  `13ce3b15fb5b4e1b0047d9559eb9aca91e6312a0` on parent and implementation).

Accepted local evidence:

```text
focused tests: 14/14 PASS
portable unit tests: 27/27 PASS
final Linux CTest: 123/123 PASS
ADV ESP-IDF build: PASS
git diff --check: PASS
```

The initial `linux_serial_unit` PTY timeout occurred in unchanged serial code
and passed immediately on focused rerun and in the final full suite; no serial
test or implementation was altered.

No blocking review finding. `main` was fast-forwarded to
`76995dcc790e3be7a564a83dd6221125f50f9bdf`.

Remaining gate: architect ADV validation of `pwd`, `cd`, relative `ls`/nano,
CWD persistence after app exit, and failed-`cd` preservation. No QMX/RF testing
is required.

## Architect test result

ADV hardware validation passed on 2026-09-24.

Verified:

```text
pwd
cd /flash/ft8
pwd
ls .
nano setting.txt
```

CWD remained `/flash/ft8` after nano exited, and a failed `cd` left the current
directory unchanged.

Result: **PASS. T072 COMPLETE.**

Follow-up usability feedback: now that CWD exists, bare `ls` should behave as
`ls .` rather than retaining its historical `ls /` default. That is handled
separately by T073.