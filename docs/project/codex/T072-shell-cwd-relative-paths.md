# T072 — Resident CWD, cd/pwd, and relative filesystem paths

Status: READY

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

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and local test evidence.

## Architect test result

Record ADV CWD/relative-path validation here.