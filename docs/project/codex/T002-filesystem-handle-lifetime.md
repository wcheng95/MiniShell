# T002 — Filesystem stale-handle lifetime

Status: READY

## Architect intent

Resolve T001 finding F03 before further feature work.

The Filesystem service is the **single owner** of application-visible file/directory handle identity and lifetime. Once a handle is closed or reclaimed, that stale token must not regain authority over a later resource merely because an internal slot is reused.

This is a core MiniShell ownership/lifetime fix. Applications and platform backends must not work around it.

## Objective

Fix the portable Filesystem handle registry so that:

> A stale `mini_file_t` or `mini_dir_t` from a completed resource lifetime is rejected after that slot is reused for another resource.

The fix must apply consistently to both file and directory handles.

## T001 source finding

T001 F03:

> Closed filesystem tokens regain ownership of later resources.

Current implementation encodes:

```text
handle = app_generation + slot index
```

The generation advances at app begin, but **not when a slot is closed/reopened inside the same app**. Therefore immediate slot reuse can recreate the exact same token.

T001 demonstrated this with the unchanged handle module:

```text
reset
-> activate slot 0
-> retain token A
-> release slot 0
-> activate slot 0 with another backend resource
-> token B == token A
-> stale lookup(A) succeeds
```

This violates the documented Filesystem contract.

## Source of truth

Read before editing:

```text
AGENTS.md
docs/project/codex/T001-architecture-boundary-audit.md
include/minishell/api.h
docs/api/filesystem-api.md

core/minishell_services/filesystem_internal.h
core/minishell_services/filesystem_handles.c
core/minishell_services/filesystem_service.c

tests/unit/test_filesystem.c
tests/unit/test_support.c
tests/unit/CMakeLists.txt
```

Relevant public contract in `docs/api/filesystem-api.md`:

```text
Handles are opaque and owned by the current foreground app.
The portable service uses generation-aware slots so stale handles are rejected.
Remaining open handles are reclaimed at app teardown.
```

## Architectural constraints

### Ownership

The ownership chain remains:

```text
application
    owns only opaque handle tokens
        |
        v
Filesystem service
    owns token identity, slot state, validation and lifecycle
        |
        v
backend
    owns native file/directory objects
```

Only the Filesystem service may decide whether a public handle is valid.

### Public API

Do **not** change:

```c
typedef uint32_t mini_file_t;
typedef uint32_t mini_dir_t;
#define MINI_FILE_INVALID 0u
#define MINI_DIR_INVALID  0u
```

No new public API is authorized.

Handle bit layout is private implementation and may change.

### Platform boundary

Do not change Linux/ADV backend handle behavior to compensate.

No application changes are permitted as part of this fix.

### Resource lifecycle

A successful close/release must permanently invalidate the released public token for the practical lifetime of the registry.

App-end cleanup must also invalidate all reclaimed tokens before later acquisitions.

File and directory registries must obey the same invariant.

### Token-space quality

Do not merely move the immediate bug elsewhere with an unnecessarily tiny reuse counter.

Because public handles are opaque 32-bit values and slot counts are small:

```text
files: 32 slots
dirs:  16 slots
```

use the available token space sensibly. A design that intentionally aliases stale tokens after only a small number of ordinary acquisitions (for example, because most token bits remain unused) is not acceptable without stopping and reporting the constraint.

Codex may choose the private encoding/allocation scheme. Document the chosen wrap behavior in implementation notes.

## Expected invariant

For any resource lifetime:

```text
open -> H1
close H1
open later resource -> H2

H2 != H1
all operations using H1 -> MINI_ERR_BAD_HANDLE
operations using H2 -> operate only on H2
```

This must hold when:

- the same internal slot is reused;
- a different native backend object is assigned;
- file handles are reused repeatedly;
- directory handles are reused repeatedly;
- app teardown closes resources and a later app begins.

Closing the same handle twice must continue to return `MINI_ERR_BAD_HANDLE` on the second close.

## Implementation scope

Expected production scope:

```text
core/minishell_services/filesystem_handles.c
core/minishell_services/filesystem_internal.h   # only if private state/signatures need adjustment
```

Expected test scope:

```text
tests/unit/test_filesystem.c
```

`filesystem_service.c` may be changed only if genuinely required by the clean private-handle design. Explain why in the implementation notes if touched.

Update `docs/api/filesystem-api.md` only if the existing public statement needs a small clarification. Do not expose private bit layout.

Update this T002 task file with implementation notes before handoff.

## Required regression tests

Add service-level tests through the public `mini_fs_api_t`, not only direct tests of private helper functions.

### File stale-token reuse

Within one `minishell_services_app_begin()` lifetime:

1. open a file and retain `H1`;
2. close `H1`;
3. open another file such that the registry can reuse the released slot;
4. verify the new handle differs from `H1`;
5. verify read/write/seek/sync/close through stale `H1` return `MINI_ERR_BAD_HANDLE` as applicable;
6. verify the new handle still works normally.

Use access modes appropriate to exercise the operations; do not distort service semantics merely to hit every API call in one handle.

### Directory stale-token reuse

Within one app lifetime:

1. `dir_open` and retain `D1`;
2. `dir_close(D1)`;
3. open another directory with slot reuse;
4. verify the new token differs from `D1`;
5. `dir_read(D1)` and `dir_close(D1)` return `MINI_ERR_BAD_HANDLE`;
6. the new directory handle remains valid.

### Repeated reuse

Exercise enough close/reopen cycles to prove identity changes across repeated reuse, not only one special transition.

Do not create an enormous wrap-duration test. Test the allocation rule deterministically.

### Existing cross-app lifecycle

Preserve the existing test proving a token retained across:

```text
app_end -> app_begin
```

is rejected.

## Non-goals

Do **not**:

- address F04 copy-to-self path aliases;
- change path normalization;
- change quota accounting;
- change writable-path exclusion;
- change public Filesystem API types or signatures;
- change backend-native handle types;
- change MiniFT8/Keyer/application code;
- fix T001 F01/F02/F05 or later findings;
- refactor unrelated service code;
- repair the existing `linux_audio` or `linux_ft8` baseline failures.

If the clean fix appears to require any item above, stop and report the architectural conflict rather than expanding scope.

## Acceptance criteria

- [ ] Closed file handles cannot become valid for an immediately/repeatedly reopened file slot.
- [ ] Closed directory handles cannot become valid for an immediately/repeatedly reopened directory slot.
- [ ] New resources receive distinct valid tokens under ordinary repeated reuse.
- [ ] Stale public operations return `MINI_ERR_BAD_HANDLE`.
- [ ] Current valid handles still operate normally.
- [ ] App-end/app-begin stale-handle rejection remains intact.
- [ ] Public API is unchanged.
- [ ] Platform backends are unchanged.
- [ ] No application workaround is introduced.
- [ ] Unit regression coverage is added at the public Filesystem service boundary.
- [ ] Existing Filesystem API documentation remains truthful.
- [ ] Full required tests are run and results recorded.
- [ ] No unrelated cleanup is included.

## Automated tests

Run the focused unit suite first:

```bash
cmake -S tests/unit -B /tmp/T002-build-unit
cmake --build /tmp/T002-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T002-build-unit --output-on-failure
```

At minimum verify explicitly:

```bash
ctest --test-dir /tmp/T002-build-unit -R api_filesystem_unit --output-on-failure
```

Then run the normal Linux baseline:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
```

The T001 baseline had two unrelated known failures:

```text
linux_audio   stale expected-output substring
linux_ft8     stale hard-coded queue-order expectation
```

Do not modify those tests or product behavior in T002. Record whether they remain the only failures.

Also run:

```bash
python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer
python3 tests/ft8_platform_boundary.py .
git diff --check
```

## Manual / hardware validation

None required.

This is portable Filesystem service ownership/lifetime behavior and should be fully proven by host unit/integration tests.

## Codex branch / PR

Use:

```text
branch: codex/T002-filesystem-handle-lifetime
PR:     T002: fix filesystem stale-handle lifetime
```

Only T002-related files belong in the PR.

## Codex implementation notes

Codex fills this section and changes `Status` from `READY` to `REVIEW` before handoff.

### Implementation summary

### Handle/token design and wrap behavior

### Files changed

### Invariants preserved

### Tests added

### Tests run and results

### Manual/hardware validation still required

### Known limitations / risks

### Commit / PR

## Supervisor review

Supervisor reviews the actual diff, token-lifetime design, regression tests, and test results.

## Architect test result

No hardware test is expected. Architect records acceptance/rejection after supervisor review.
