# T073 — Natural bare ls and cd defaults

Status: READY

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

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and local test evidence.

## Architect test result

Record ADV bare-ls/bare-cd validation here.