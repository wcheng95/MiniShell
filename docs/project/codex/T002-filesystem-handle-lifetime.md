# T002 — Filesystem stale-handle lifetime

Status: COMPLETE

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

- [x] Closed file handles cannot become valid for an immediately/repeatedly reopened file slot.
- [x] Closed directory handles cannot become valid for an immediately/repeatedly reopened directory slot.
- [x] New resources receive distinct valid tokens under ordinary repeated reuse.
- [x] Stale public operations return `MINI_ERR_BAD_HANDLE`.
- [x] Current valid handles still operate normally.
- [x] App-end/app-begin stale-handle rejection remains intact.
- [x] Public API is unchanged.
- [x] Platform backends are unchanged.
- [x] No application workaround is introduced.
- [x] Unit regression coverage is added at the public Filesystem service boundary.
- [x] Existing Filesystem API documentation remains truthful.
- [x] Full required tests are run and results recorded.
- [x] No unrelated cleanup is included.

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

Fixed T001 F03 in the portable Filesystem registry. Each successful file/directory
acquisition now gets a fresh opaque token independent of its slot and native handle.
Release and teardown remove the live token; later acquisitions cannot recreate it.
Added public-service regression tests and a small handle-contract clarification.

Branch started from fetched `origin/main` at `508b89d` (T002 active-task packet).
No unrelated T001 finding was addressed.

### Handle/token design and wrap behavior

- A shared `uint32_t` acquisition sequence supplies file and directory tokens from
  1 through `UINT32_MAX`. All 32 bits participate in identity; no bits are reserved
  for slot indexing. Files and directories share 4,294,967,295 acquisitions per
  running registry lifetime, without collisions between their live identities.
- Each private slot stores its complete public token. Lookup checks live backend
  state and token equality in a bounded scan of at most 32 file or 16 directory
  slots. No heap allocation, backend identity dependency, or new public API.
- Close/release clears the slot, as before. App teardown and service reconfiguration
  clear live entries but **never reset the sequence**. Removing the old per-app
  generation increment is necessary because identity now advances per acquisition.
- After issuing `UINT32_MAX`, defined unsigned increment reaches zero, which is a
  permanent exhausted sentinel. Neither allocator nor free-slot discovery advances
  beyond it. Subsequent public opens return existing `MINI_ERR_TOO_MANY_OPEN` before
  invoking backend open, so exhaustion cannot truncate/create a file or alias a stale
  token. Existing valid handles still work and can close. This deliberately trades
  availability at complete token-space exhaustion for lifetime correctness.
- A process/device restart creates a new registry lifetime. Opaque handles are not
  persistent identifiers and cannot be carried across such a restart.

### Files changed

- `core/minishell_services/filesystem_handles.c`: acquisition sequence, live-token
  lookup, occupied-slot protection, and fail-closed exhaustion handling.
- `core/minishell_services/filesystem_internal.h`: full public-token fields in
  private slots; remove obsolete per-app generation helper declaration.
- `core/minishell_services/filesystem_service.c`: remove only the obsolete generation
  increment in app begin; the existing app-end cleanup call remains. This one-line
  integration change avoids keeping a meaningless private lifecycle helper.
- `tests/unit/test_filesystem.c`: public Filesystem lifetime regressions and local
  directory callbacks (the shared fake port has no directory callbacks).
- `docs/api/filesystem-api.md`: replace the old implementation-specific
  “generation-aware slots” wording with fresh acquisition identity and invalidation
  on slot reuse. No private encoding is exposed.
- This task packet: acceptance checklist, REVIEW status, and handoff evidence.

### Invariants preserved

Public `mini_file_t` / `mini_dir_t` remain opaque `uint32_t` values with zero invalid.
Public signatures, native backend types, 32-file/16-directory capacity, normal
close/teardown behavior, path normalization, quota accounting, writable-path exclusion,
and application code are unchanged. Filesystem alone validates public identity;
Linux/ADV backends still own native resources. Existing cross-app stale-file coverage
is retained. No application workaround or backend change was introduced.

### Tests added

All regression operations enter through `mini_api_get()->fs` (`mini_fs_api_t`), not
private handle helpers:

- 64 sequential file acquisitions alternating two files, with close/reopen reusing
  the available slot. Every new token must differ from every retained earlier token.
- Every retired file token is rejected by read/write/seek/sync/close while the next
  resource is live. Stale sync/close must not reach the backend. The current resource
  retains the expected data/position and supports normal read/write/seek/sync/close.
- 64 directory acquisitions alternating two distinct fake native directories. Every
  older token is rejected by dir_read/dir_close without consuming the live directory's
  entry or closing its backend. Current handles return the correct entry and EOF.
- Double close/stale operations immediately after release, for both handle kinds.
- Unclosed file/directory handles reclaimed at app end, followed by app begin and
  fresh acquisitions. Old tokens stay invalid and the new resources remain usable.
- The same reclamation/reacquisition checks across service reconfiguration, including
  exact backend close counts. This protects the sequence from accidental reset.

The new focused regression was first run against the original production registry:
it **failed** at `files[i] != files[j]` on the first repeated acquisition. After the
fix, the same test passes. No existing assertion was weakened or removed.

### Tests run and results

```sh
cmake -S tests/unit -B /tmp/T002-build-unit
cmake --build /tmp/T002-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T002-build-unit --output-on-failure
ctest --test-dir /tmp/T002-build-unit -R api_filesystem_unit --output-on-failure
```

Configure/build **PASS** with the unit target's `-Wall -Wextra -Werror` settings;
full unit suite **14/14 PASS**; explicit Filesystem test **1/1 PASS**. The earlier
pre-fix focused run failed as described above, establishing the regression.

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
```

Linux configure/build **PASS**. CTest **18/20 PASS** (exit 8); the only failures are
the same two identified by T001 and this task:

- `linux_audio`: expected contiguous `frames=180140 hash=f05f17c990b748e1`; actual
  probe output inserts diagnostic fields between those same frame/hash values.
- `linux_ft8`: timeout at the stale queue-rotation expectation for
  `N5CH     RPLY 0/3` (`tests/linux_ft8.py:207`); actual page ends with KQ4PUG.

Linux Filesystem/directory/resource and portable-app integrations pass. Neither
known failing test nor the relevant application behavior was modified. Full baseline
output was captured locally in `/tmp/T002-linux-ctest.log`.

```sh
python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer
python3 tests/ft8_platform_boundary.py .
git diff --check
```

All **PASS**. Direct checker output:

```text
app_dependency_boundary: PASS (ft8)
app_dependency_boundary: PASS (keyer)
ft8_platform_boundary: PASS (52 source/header files; platform-clean; dependency-clean; AutoSeq heap-free)
```

### Manual/hardware validation still required

None required by T002. No hardware validation performed; the change is portable
service logic and the required host regressions/integrations were run.

### Known limitations / risks

- Token exhaustion intentionally stops future acquisitions until runtime restart;
  there is no stale-token wraparound. Tests exercise repeated allocation and lifecycle
  transitions deterministically, not billions of acquisitions. The terminal counter
  transition and pre-backend exhaustion guards were reviewed statically.
- Token lookup is now a bounded linear scan rather than index decoding (maximum 32
  or 16 slots). No performance claim beyond that fixed bound is made.
- Existing foreground/synchronous registry assumptions are unchanged; this task does
  not add concurrency or reentrancy support.
- The two unrelated baseline failures remain. All other T001 findings are out of
  scope. No deviation from T002's authorized implementation boundary.

### Commit / PR

Branch: `codex/T002-filesystem-handle-lifetime`.
PR title: `T002: fix filesystem stale-handle lifetime`.
The implementation commit and PR URL are recorded in the PR body/final handoff to
avoid embedding a self-referential commit hash here.

## Supervisor review

PASS. The merged implementation preserves Filesystem ownership, uses a fresh shared nonzero 32-bit token per acquisition, fails closed on token-space exhaustion, keeps lookup bounded to the fixed slot counts, and adds public-API regressions for file/directory reuse, stale operations, teardown, and reconfiguration. PR #34 was reviewed against commit `6840262` and merged as `1ff4f0be7640cffcc09e13bbb86a3b20da06a493`.

## Architect test result

ACCEPTED. No hardware validation required. Architect authorized merge of PR #34.
