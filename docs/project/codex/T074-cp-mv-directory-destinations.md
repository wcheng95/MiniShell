# T074 — Unix-style cp/mv directory destinations

Status: READY

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

- [ ] `cp file /existing/dir` creates/replaces `/existing/dir/file`.
- [ ] `cp file /existing/dir/` has the same result.
- [ ] `cp file /existing/dir/.` has the same result.
- [ ] `mv file /existing/dir` renames to `/existing/dir/file` on the same filesystem.
- [ ] Relative source and directory destinations work under a non-root CWD.
- [ ] Existing explicit missing destination path behavior is unchanged.
- [ ] Existing explicit regular-file destination replacement is unchanged.
- [ ] Existing destination child regular file is replaceable for both `cp` and `mv`.
- [ ] Destination child directory is rejected.
- [ ] Source directory is rejected.
- [ ] Self-copy through a directory operand cannot truncate/modify the source.
- [ ] Cross-filesystem `mv` does not silently become copy+delete.
- [ ] Public Filesystem API and T072 CWD implementation are unchanged.
- [ ] Full Linux CTest passes.
- [ ] ADV firmware builds successfully.

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

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and local test evidence.

## Architect test result

Record ADV cp/mv directory-destination validation here.