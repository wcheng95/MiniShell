# T074 — Unix-style cp/mv directory destinations

Status: REVIEW

## Architect intent

Reduce typing for normal file management now that MiniShell has an accepted
session CWD and relative paths.

Accepted examples:

```text
cp /flash/ft8/RT260925.txt /sd
cp /flash/ft8/RT260925.txt /sd/
cp /flash/ft8/RT260925.txt /sd/.
```

must all target:

```text
/sd/RT260925.txt
```

The same directory-destination rule applies to `mv`.

Relative paths from T072/T073 must work naturally, for example:

```text
cd /flash/ft8
cp RT260925.txt /sd
mv old.txt archive
```

where an existing directory destination means “place the source file inside
that directory under its source basename.”

## Objective

Add familiar single-file Unix-style directory-destination semantics to portable
`cp` and `mv` without changing the public Filesystem API or its rename/copy
primitives.

## Current context

Read before editing:

```text
AGENTS.md
README.md
docs/README.md
docs/api/filesystem-api.md
docs/project/codex/T072-shell-cwd-relative-paths.md
docs/project/codex/T073-natural-ls-cd-defaults.md
apps/cp/main/cp.c
apps/cp/main/cp_copy.c
apps/cp/main/cp_copy.h
apps/mv/main/mv.c
tests/unit/test_cp_copy.c
tests/linux_cwd.py
platform/adv/main/cp_static.c
platform/adv/main/mv_static.c
platform/adv/main/CMakeLists.txt
```

Current behavior:

- `cp` accepts exactly one regular-file source and one destination path;
- if `cp` destination already exists as a directory, `cp_copy_file()` rejects it;
- `mv` accepts exactly one regular-file source and one destination path;
- `mv` passes the destination directly to Filesystem `rename()`;
- Filesystem `rename()` is regular-file focused and replaces an existing regular
  destination, but rejects replacing a directory;
- T072 makes all source/destination path spellings absolute-or-relative at the
  Filesystem boundary;
- T073 changes only bare `ls` / bare `cd`; it does not affect `cp`/`mv`.

## Architecture decision

Directory-destination expansion belongs in the **portable `cp` / `mv` app layer**.

Do not change Filesystem `rename()` semantics and do not add a public path-join,
basename, canonicalize, or CWD API.

Rationale:

- the Filesystem service should continue to interpret a supplied path literally
  after relative resolution/normalization;
- deciding that a directory operand means `directory/source-basename` is command
  policy, not a generic filesystem primitive;
- both apps should share the same small portable destination-resolution helper
  so `cp` and `mv` cannot drift.

A shared helper under an app-private/common location is preferred. It may be
header-only or a small C module, as long as Linux and ADV compositions both use
the same implementation and it is not exposed through `include/minishell/api.h`.

## Required destination semantics

For a regular-file source `SOURCE` and operand `DEST`:

1. `stat(DEST)`.
2. If `DEST` does not exist, use `DEST` unchanged as the effective destination.
3. If `DEST` is an existing regular file, use `DEST` unchanged.
4. If `DEST` is an existing directory, effective destination is:

```text
DEST / basename(SOURCE)
```

5. Then perform the existing copy or rename behavior using that effective path.

Examples:

```text
cp /flash/a.txt /sd            -> /sd/a.txt
cp /flash/a.txt /sd/           -> /sd/a.txt
cp /flash/a.txt /sd/.          -> /sd/a.txt
cp a.txt .                     -> ./a.txt
cp a.txt ../backup             -> ../backup/a.txt   # when ../backup is a directory

mv /flash/a.txt /flash/archive -> /flash/archive/a.txt
mv a.txt .                     -> ./a.txt
```

Relative `DEST` is interpreted by the public Filesystem API exactly as in T072.
The helper must not query or duplicate the private CWD.

## Source basename

The basename is the source file's final filename component under MiniShell path
syntax.

Requirements:

- work for absolute and relative source spellings;
- ignore repeated/trailing `/` separators;
- tolerate a trailing `.` component consistent with T072 normalization;
- source must already resolve/stat as a regular file before a directory target
  is acted on;
- never invent HOME/platform path rules;
- reject an unusable/empty basename rather than constructing an invalid target.

Examples whose basename is `a.txt`:

```text
/flash/a.txt
/flash//a.txt
./a.txt
dir/../a.txt
a.txt/
a.txt/.
```

The implementation does not need a general public canonical-path routine merely
to obtain this filename component.

## Path construction and bounds

Build the effective destination with a fixed bounded buffer; no heap allocation.

The existing MiniShell normalized path capacity is 512 bytes internally and the
resident shell line is smaller, but portable apps must still reject any joined
operand that does not fit their bounded buffer. Never truncate.

It is acceptable for the app helper to use a private app-level bound matching
the current MiniShell path capacity. Do not expose that bound as a new public API
constant in this task.

Joining must avoid accidental malformed text:

```text
/sd     + a.txt -> /sd/a.txt
/sd/    + a.txt -> /sd/a.txt
/sd/.   + a.txt -> /sd/./a.txt   # Filesystem normalizes this correctly
.       + a.txt -> ./a.txt
```

Exact textual spelling is less important than the final Filesystem target.

## cp behavior

`cp` remains single-file, non-recursive copy.

After resolving a directory destination to an effective child path:

- existing destination child regular file is replaced using current copy behavior;
- missing destination child is created using current copy behavior;
- destination child that resolves to a directory fails;
- source directories remain rejected;
- existing read/write/sync/close error handling remains intact.

Do not weaken `cp_copy_file()` safety. A self-copy, including one reached through
a directory operand such as `cp file .`, must fail safely and must never truncate
or alter the source. An exact diagnostic match is less important than preserving
the file, but keep the existing same-path diagnostic where the implementation can
identify it cleanly.

The 1 KiB copy buffer and partial-read/partial-write handling remain unchanged.

## mv behavior

`mv` remains a regular-file rename command.

After resolving a directory destination to an effective child path:

- existing destination child regular file is replaced according to existing
  Filesystem `rename()` semantics;
- missing destination child is renamed normally;
- destination child directory fails;
- source directories remain rejected.

Important: **do not add copy+delete fallback for cross-filesystem moves.**

If the effective source/destination cannot be renamed by the existing backend
(for example `/flash/file` -> `/sd/file` on ADV), preserve the existing rename
failure/unsupported behavior. T074 is only about interpreting a directory
destination, not broadening `mv` into a cross-volume transfer engine.

## Diagnostics

Keep existing command usage:

```text
usage: cp <source> <destination>
usage: mv <source> <destination>
```

Add or adjust concise errors only as needed for:

- destination stat failure;
- effective destination path too long/invalid;
- unusable source basename;
- destination child being a directory.

Do not expose backend-specific errors.

## Architectural constraints

- Preserve `MINISHELL_API_VERSION 3` and `include/minishell/api.h`.
- Do not change Filesystem service path/CWD semantics from T072.
- Do not change Filesystem `rename()` contract.
- Do not add a public basename/path-join/canonical-path function.
- Keep `cp` and `mv` portable: no POSIX/FATFS/ESP-IDF calls.
- No heap allocation for destination resolution.
- Preserve ADV static packaging and Linux `.so` app packaging.
- Keep source-directory operations unsupported.

## Non-goals

Do not implement:

- recursive `cp` or `mv`;
- moving directories;
- multiple source operands;
- `cp -r`, `mv -i`, force/prompt flags, metadata preservation, or permissions;
- cross-filesystem `mv` copy+delete fallback;
- globbing/wildcards;
- command history;
- pathname completion / Tab choices;
- `clear`;
- Filesystem API/version changes.

## Acceptance criteria

- [x] `cp file /existing/dir` creates/replaces `/existing/dir/file`.
- [x] `cp file /existing/dir/` has the same result.
- [x] `cp file /existing/dir/.` has the same result.
- [x] `mv file /existing/dir` renames to `/existing/dir/file` on the same filesystem.
- [x] Relative source and directory destinations work under a non-root CWD.
- [x] Existing explicit missing destination path behavior is unchanged.
- [x] Existing explicit regular-file destination replacement is unchanged.
- [x] Existing destination child regular file is replaceable for both `cp` and `mv`.
- [x] Destination child directory is rejected.
- [x] Source directory is rejected.
- [x] Self-copy through a directory operand cannot truncate/modify the source.
- [x] Cross-filesystem `mv` does not silently become copy+delete.
- [x] Public Filesystem API and T072 CWD implementation are unchanged.
- [x] Full Linux CTest passes.
- [x] ADV firmware builds successfully.

## Automated tests

Add focused portable/helper tests for at least:

1. source basename extraction from absolute path;
2. relative source basename;
3. repeated/trailing separators;
4. trailing `.` source spelling;
5. destination missing -> unchanged operand;
6. destination regular file -> unchanged operand;
7. destination directory -> child path;
8. directory operands `/dir`, `/dir/`, `/dir/.`;
9. relative directory operand `.` and `../dir`;
10. joined path at the supported bound;
11. joined path overflow;
12. source directory rejection;
13. destination child directory rejection;
14. existing child file replacement;
15. cp self-copy via directory target preserves source;
16. mv same-filesystem directory destination;
17. mv cross-filesystem failure remains a failure, not copy+delete.

Extend Linux runtime integration using the real portable apps and CWD. Include
examples such as:

```text
cd /flash/ft8
cp setting.txt /sd
cp setting.txt .
mkdir archive
cp setting.txt archive
mv copy.txt archive
```

where fixtures make each expected target unambiguous.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T074-unit
cmake --build /tmp/T074-unit -j"$(nproc)"
ctest --test-dir /tmp/T074-unit --output-on-failure

idf.py -C platform/adv build
git diff --check
```

## Manual / hardware validation

After supervisor review, architect/tester validates on ADV with simple files.

Example:

```text
M$> cd /flash
M$> mkdir t074
M$> cp minishell/alias.txt t074
M$> ls t074
alias.txt
```

Then same-filesystem `mv`:

```text
M$> mv t074/alias.txt .
M$> ls
# alias.txt now exists in /flash
```

Also verify directory spelling variants if convenient:

```text
cp minishell/setting.txt /sd
cp minishell/setting.txt /sd/
cp minishell/setting.txt /sd/.
```

using cleanup/recreation between copies as needed.

No QMX/RF validation is required.

## Codex branch / handoff

Work on:

```text
codex/T074-cp-mv-directory-destinations
```

Start from current `main`.

Before coding, read `AGENTS.md`, `docs/README.md`, T072/T073, and this task packet.

Keep T074 to one reviewable implementation commit. Do not merge to `main` and
do not open a PR unless asked.

## Codex implementation notes

### Implementation summary

Implemented from current `origin/main` at
`170a8d3800131aade3c4a1c445e74c6e96a3df8e` on the requested branch.
No task or architecture deviations.

Both commands use the same app-private header-only destination helper. After
source regular-file validation, it stats the destination, preserves missing/file
operands, and expands directory operands with the source basename. The reverse
component scan ignores separators/dots and cancels parent components without
querying CWD or constructing a canonical path. Joining uses a 512-byte stack
buffer and rejects overflow before writing anything.

`cp_copy_file()` validates the effective child path and retains its original
read-before-write ordering, 1 KiB copy buffer, partial-I/O loop and cleanup.
Exact textual self-copies retain their existing diagnostic; other normalized
self-aliases are blocked by existing Filesystem writer exclusion before truncate.
`mv` passes the effective destination to the existing rename API, with no fallback.

### Files changed

- `apps/common/file_destination.h`: shared portable bounded destination helper.
- `apps/cp/main/cp_copy.c`, `cp_copy.h`, `cp.c`: destination expansion and path
  length diagnostic while retaining existing copy/error handling.
- `apps/mv/main/mv.c`: destination expansion, stat/path and child-directory errors.
- `tests/unit/test_cp_copy.c`: helper cases/bounds and actual service-backed copy
  replacement, source/child directory rejection and self-copy protection.
- `tests/unit/test_mv.c`: actual portable command with fake backend; same-volume
  moves, replacement, self-rename, directory rejection and cross-volume failure.
- `tests/unit/CMakeLists.txt`, `test_main.c`, `test_support.h`: register mv coverage.
- `tests/linux_cwd.py`: actual Linux app/CWD integration, spelling variants,
  create/replace, explicit filename regressions and self-copy preservation.
- `README.md`, `docs/api/filesystem-api.md`: replace stale explicit-destination
  notes with directory operand semantics and the unchanged rename boundary.
- This task packet: implementation and validation evidence.

### Invariants preserved

Public API v3, `include/minishell/api.h`, all Filesystem service/CWD code, resident
shell and ADV packaging are byte-for-byte unchanged from the base. The shared
header is compiled through the existing Linux `.so` and ADV static app sources;
no additional build-source wiring is needed. Apps use only public Filesystem
calls, with no private CWD access, backend calls, heap allocation, recursive
operations, multiple sources, or cross-volume copy/delete fallback. Source and
child directories remain unsupported. Usage text remains unchanged.

### Local tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R 'linux_cwd|linux_file|linux_directory|linux_utility'
# PASS: 2/2 matching tests (linux_directory, linux_cwd).
cmake -S tests/unit -B /tmp/T074-unit
cmake --build /tmp/T074-unit -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir /tmp/T074-unit --output-on-failure
# PASS: final unit run 27/27, including new mv group.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# First full run: 122/123; unchanged linux_serial_unit PTY assertion failed.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R '^linux_serial_unit$'
# First focused rerun PASS 1/1.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# Second full run: 122/123, same serial assertion.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R '^linux_serial_unit$'
# Second focused rerun failed at the same assertion.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: final full run 123/123 (59.00 seconds).
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: firmware 0x152c80 bytes; app partition 78% free. No flashing.
git diff --check
# PASS
```

The first unit run exposed an incomplete new mv fixture: `fake_full_port()` has
no Console callback and its default namespace lacks `/flash`. The test now
supplies both; production API validation was retained. The old cp assertion
rejecting a directory operand was narrowly replaced with child creation and
replacement checks; child-directory rejection is separately verified.

The first two full Linux runs hit `tests/linux_serial_test.c:67`, the existing
PTY write timeout/zero-byte assertion also recorded in earlier tasks. The first
focused rerun passed; the second failed at the same assertion. Serial
implementation/tests are unchanged; no assertion was weakened. The third full
run passed all 123 tests.

Helper tests cover absolute/relative basenames, repeated/trailing slashes and
dots, parent cancellation, unusable basenames, missing/file operands unchanged,
directory variants including root, a 511-byte joined path and overflow, and stat
failure propagation. Service-backed tests check copy content, self-copy survival,
rename replacement and an injected cross-volume unsupported result with no file
opens/deletion and unchanged destination content. Linux integration exercises
real portable apps under non-root CWD, including `/sd`, `/sd/`, `/sd/.`, relative
archive variants, explicit filenames and source/child directory rejection.

### Manual/hardware validation still required

After supervisor review, run the ADV examples above for cp into an existing
directory and same-filesystem mv, then the `/sd` spelling variants. Actual ADV
cross-volume rename failure remains a hardware check; unit tests inject that
backend result. No device was flashed or tested here. No QMX/RF validation needed.

### Known limitations / risks

The private 512-byte bound applies to the joined operand text, so a very long
redundant spelling can fail even if service normalization could shorten it.
Relative paths still must fit the service bound after CWD resolution. Both are
intentional bounded failures, with no truncation. Cross-volume moves remain
backend-dependent rename operations. Hardware acceptance remains pending.

### Commit

One implementation commit titled `T074: support cp and mv directory destinations`,
parent `170a8d3800131aade3c4a1c445e74c6e96a3df8e`, on
`codex/T074-cp-mv-directory-destinations`. Exact pushed SHA is returned in the
handoff. No merge or PR.

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and local test evidence.

## Architect test result

Record ADV cp/mv directory-destination validation here.