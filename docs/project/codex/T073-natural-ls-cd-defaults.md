# T073 — Natural bare ls and cd defaults

Status: REVIEW

## Architect intent

Follow up the hardware-accepted T072 CWD work with two small command defaults that
feel natural on ADV:

```text
ls       == ls .
cd       == cd /
```

The user should not need to type `ls .` merely because a CWD now exists, and with
no HOME concept the simplest useful meaning of bare `cd` is returning to the
MiniShell logical root.

## Objective

Change only the default behavior of:

- portable `ls` with no pathname argument;
- resident `cd` with no pathname argument.

Do not change the Filesystem service, CWD architecture, public API, explicit-path
behavior, or any other shell semantics.

## Current context

Read before editing:

```text
AGENTS.md
docs/README.md
docs/api/filesystem-api.md
docs/project/codex/T072-shell-cwd-relative-paths.md
core/shell.c
apps/ls/main/ls.c
tests/linux_cwd.py
```

T072 is COMPLETE and hardware-accepted. Current behavior is:

```text
ls          -> ls /
ls .        -> current CWD
cd <path>   -> change CWD
cd          -> usage error
```

Architect-approved behavior after T073:

```text
ls          -> ls .
ls /        -> explicit root listing, unchanged
cd          -> cd /
cd <path>   -> unchanged
```

## ls behavior

In `apps/ls/main/ls.c`, change the no-argument default pathname from `/` to `.`.

Requirements:

- `ls` and `ls .` must enumerate the same current directory;
- `ls /` must retain its current explicit-root behavior;
- `ls <other-path>` remains unchanged;
- usage remains `ls [path]`;
- hidden-name filtering remains unchanged;
- directory suffix/presentation remains unchanged for non-explicit-root listings.

Important presentation detail:

The existing special root presentation is keyed by an explicit `/` argument:

```text
ls /
```

may continue to show root entries in its established complete MiniShell-path form
such as `/flash` and `/sd`.

Bare `ls`, even when CWD happens to be `/`, is semantically `ls .` and should use
the ordinary current-directory presentation rather than being rewritten back to
the explicit-root special case.

Do not teach `ls` how to query the private CWD. It should simply pass `.` through
the public Filesystem API and let T072 resolution do the work.

## cd behavior

In the resident shell:

```text
cd
```

must behave exactly as:

```text
cd /
```

Requirements:

- `cd` with zero path arguments calls the existing private CWD setter with `/`;
- `cd <path>` is unchanged;
- `cd <a> <b>` remains a usage error;
- failures from the existing CWD setter retain the normal `cd` diagnostic;
- built-in precedence and startup behavior remain unchanged;
- `startup=cd;...` therefore returns to `/` before the next startup command;
- no HOME, `~`, previous-directory, or environment-variable behavior.

Update help from:

```text
cd <path>
```

to an accurate compact form such as:

```text
cd [path]
```

where omitted path means `/`.

## Architectural constraints

- Do not edit `include/minishell/api.h`.
- Do not change the Filesystem CWD implementation from T072.
- Do not add new private service APIs.
- Do not add app-specific path resolution.
- Do not alter aliases, startup `;` sequencing, command parsing, or app lifecycle.
- No heap allocation.
- Linux and ADV behavior must remain identical at the MiniShell level.

## Non-goals

Do not implement:

- Unix-style `cp file DIR` / `mv file DIR` destination behavior (next task);
- command history;
- pathname auto-completion;
- Tab choice display;
- `clear`;
- HOME or `~`;
- `cd -`;
- `pushd` / `popd`;
- changes to explicit `ls /` presentation;
- Filesystem API changes.

## Acceptance criteria

- [ ] After `cd /flash/ft8`, bare `ls` lists `/flash/ft8` contents.
- [ ] `ls` and `ls .` produce equivalent directory contents/presentation.
- [ ] Explicit `ls /` retains the existing root presentation.
- [ ] At initial CWD `/`, bare `ls` is still semantically `ls .`, not rewritten to `ls /`.
- [ ] Bare `cd` changes CWD to `/`.
- [ ] Bare `cd` from `/` is harmless.
- [ ] `cd <path>` remains unchanged.
- [ ] `cd one two` remains a usage error.
- [ ] `startup=cd;...` uses the same bare-cd behavior.
- [ ] Built-in alias precedence remains unchanged.
- [ ] Public MiniShell API is unchanged.
- [ ] Full Linux CTest passes.
- [ ] ADV firmware builds successfully.

## Automated tests

Update/add focused tests covering:

1. `cd /flash/ft8`; bare `ls` lists the FT8 directory;
2. `ls` and `ls .` equivalence;
3. explicit `ls /` retains its established presentation;
4. bare `cd` returns `/flash/ft8` -> `/`;
5. bare `cd` while already at `/`;
6. `cd one two` still errors;
7. startup `cd` with no argument resets to root;
8. alias definitions named `cd` still cannot override the built-in.

Prefer extending the existing Linux CWD integration test rather than building a
new framework.

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
M$> cd /flash/ft8
M$> ls
# should list current /flash/ft8 contents

M$> cd
M$> pwd
/
```

Also confirm:

```text
M$> ls /
```

still behaves as the explicit-root listing.

No QMX/RF validation is required.

## Codex branch / handoff

Work on:

```text
codex/T073-natural-ls-cd-defaults
```

Start from current `main`.

Before coding, read `AGENTS.md`, `docs/README.md`, T072, and this task packet.

Keep T073 to one reviewable implementation commit. Do not merge to `main` and
do not open a PR unless asked.

## Codex implementation notes

### Implementation summary

Implemented from current `origin/main` at
`1118bc2f0660912a8701c9764ed14e735d66c5ca` on the requested branch.
No scope deviations.

Bare `ls` now passes `.` to the public Filesystem API. Explicit `/` retains
its existing root-presentation branch. Bare `cd` passes `/` to the existing
private CWD setter; explicit paths and failure diagnostics are unchanged.
More than one path argument remains a usage error. Help/usage now show `cd [path]`.

### Files changed

- `apps/ls/main/ls.c`: change only the omitted-path default to `.`.
- `core/shell.c`: omitted cd path defaults to `/`; update help and usage.
- `tests/linux_cwd.py`: bare/explicit ls equivalence and presentation, hidden
  filtering, directory suffixes, bare cd from child/root, excess arguments,
  alias precedence and startup bare cd.
- `tests/linux_directory.py`: preserve the established root-presentation check
  by invoking explicit `ls /`, as required by the new default contract.
- `README.md`, `docs/README.md`, `docs/api/filesystem-api.md`: document defaults.
- This task packet: implementation and test evidence.

### Invariants preserved

Filesystem service/CWD implementation, private service interface, public API v3
and `include/minishell/api.h` are unchanged. No backend or application CWD query,
argv rewriting, heap allocation, HOME/tilde behavior, or new shell parsing.
Explicit paths, hidden filtering, directory suffixes, alias precedence, startup
sequencing and app lifecycle retain their existing behavior. `cp`/`mv` destination
semantics remain outside scope.

### Local tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R 'linux_cwd|linux_directory|alias|resident_|linux_startup'
# PASS: 7/7 focused tests.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# Initial full run: 122/123; linux_serial_unit failed its PTY timeout assertion.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R '^linux_serial_unit$'
# First focused rerun failed; rerun after ADV build finished passed 1/1.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: final full run 123/123 (59.53 seconds).
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: firmware 0x1527e0 bytes; app partition 78% free. No flashing.
git diff --check
# PASS
```

The unchanged serial test failed at `tests/linux_serial_test.c:67` (full-PTY
write timeout with zero bytes), the intermittent assertion recorded in T069/T072.
The first focused retry also failed while the ADV build was running; the retry
after that build passed. Serial test/implementation and all Filesystem service
code are unchanged. No test assertion was weakened. ADV build completed with
existing SDK/ELF-loader pedantic warnings.

The previous bare-cd usage assertion was replaced with the new root-reset
checks; the multi-argument usage assertion remains. Existing relative-path
operations, startup inheritance and failed explicit cd tests remain covered.
Root-format expectations still apply to explicit `ls /`; bare root listing is
now separately checked for ordinary `flash/` and `sd/` presentation.

### Manual/hardware validation still required

On ADV after review: `cd /flash/ft8`, bare `ls`, bare `cd`, then `pwd` showing `/`.
Confirm explicit `ls /` keeps its root listing format. No device flashed or
hardware tested here; no QMX/RF validation required.

### Known limitations / risks

Only omitted arguments change meaning. Bare ls at root intentionally uses the
ordinary directory suffix presentation, unlike explicit `ls /`. No new known
implementation limitation; ADV acceptance remains pending.

### Commit

One implementation commit titled `T073: use natural bare ls and cd defaults`,
parent `1118bc2f0660912a8701c9764ed14e735d66c5ca`, on
`codex/T073-natural-ls-cd-defaults`. Exact pushed SHA is returned in the handoff.
No merge or PR.

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and local test evidence.

## Architect test result

Record ADV bare-ls/bare-cd validation here.