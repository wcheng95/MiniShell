# T003 — Preserve station.txt on load failure

Status: READY

## Architect intent

Resolve T001 finding F02 before further feature work.

MiniFT8 must distinguish:

1. `station.txt` is genuinely absent;
2. `station.txt` exists and was loaded successfully;
3. loading an existing/configured path failed.

Only case 1 authorizes creating the default station file.

A read/size/open/close/storage failure is **not** permission to replace an existing configuration.

## Objective

Replace the current boolean text-load result with an application-private typed result that preserves the missing-vs-error distinction, then make `app_controller_init()` follow this policy:

```text
FOUND
    -> parse existing text
    -> continue on valid config
    -> fail initialization on parse error
    -> never rewrite merely because load/parse failed

NOT_FOUND
    -> use defaults
    -> create station.txt through existing atomic-save path

ERROR
    -> fail initialization
    -> preserve existing storage
    -> do not create/truncate/rename station.txt or station.txt.tmp as a recovery action
```

This task does not redesign the station format or configuration ownership.

## T001 source finding

T001 F02:

> A failed station read is treated as permission to replace it.

Current behavior:

```text
storage_service_read_text()
    returns false for:
        MINI_ERR_NOT_FOUND
        other open failures
        read failures
        oversized input
        close failures

app_controller_init()
    interprets every false as "not loaded"
    -> serializes defaults
    -> atomically replaces station.txt
```

T001 demonstrated that an existing oversized `station.txt` was replaced by defaults.

## Source of truth

Read before editing:

```text
AGENTS.md
docs/project/codex/T001-architecture-boundary-audit.md
docs/MiniFT8/README.md
docs/MiniFT8/development.md

apps/ft8/src/storage_service/storage_service.h
apps/ft8/src/storage_service/storage_service.c
apps/ft8/src/config_service/config_service.h
apps/ft8/src/config_service/config_service.c
apps/ft8/src/app_controller/app_controller.c

include/minishell/api.h
docs/api/filesystem-api.md
CMakeLists.txt
```

Also inspect existing Linux and FT8 test patterns under `tests/`.

## Architectural constraints

### Ownership split

Preserve:

```text
MiniShell Filesystem
    owns generic path/file operations and mini_result_t backend/service errors
        |
        v
FT8 storage_service
    owns application storage mechanics and translates text-load outcome
        |
        v
app_controller
    owns application policy:
        missing -> create defaults
        found -> parse/use
        error -> abort without destructive recovery
        |
        v
config_service
    owns station configuration syntax/defaults/serialization
```

Do not move default-creation policy into `storage_service`.

Do not make `config_service` perform filesystem I/O.

### Result contract

Use a typed **application-private** result for text load with at least these semantic states:

```text
FOUND
NOT_FOUND
ERROR
```

Names are implementation-private and may differ.

Do not add a public MiniShell API or public `mini_result_t` value for this FT8-specific policy.

The storage layer should preserve enough information to distinguish absence from every other failure. It does not need to expose all backend errors individually unless that is already useful within this private module.

### Missing means positively missing

`NOT_FOUND` may be returned only when the Filesystem operation positively reports `MINI_ERR_NOT_FOUND` for the requested station file.

Do not infer missing from:

- generic open failure;
- access failure;
- path/type error;
- read error;
- oversized text;
- close failure;
- malformed contents.

### Existing file preservation

For `ERROR` and parse failure:

- do not write defaults;
- do not truncate the existing station file;
- do not rename a temporary default over it;
- do not silently continue with defaults.

Initialization may fail and return control to MiniShell.

### File format

Do not change:

```text
/flash/ft8/station.txt
```

Do not change station keys, normalization rules, defaults, serialization order, or atomic-save format.

### F01 separation

Do **not** address T001 F01 (runtime GPS grid vs persisted grid) in T003.

T003 is solely the load-result/destructive-recovery defect.

## Expected storage behavior

For `storage_service_read_text()` or its renamed/retyped equivalent:

### Successful file

```text
open -> MINI_OK
read all text within buffer
EOF
close -> MINI_OK
=> FOUND
=> output is NUL terminated
```

### Missing file

```text
open -> MINI_ERR_NOT_FOUND
=> NOT_FOUND
```

No read/close is expected because no handle was acquired.

### Other open failure

```text
open -> anything other than MINI_OK / MINI_ERR_NOT_FOUND
=> ERROR
```

### Read failure

If open succeeds but any read fails:

```text
close acquired handle
=> ERROR
```

### Oversized text

If text does not fit in the caller buffer including terminating NUL:

```text
close acquired handle
=> ERROR
```

It must not be treated as missing.

### Close failure

If all reads succeeded but close fails:

```text
=> ERROR
```

### Parse failure

Parsing remains controller/config policy:

```text
storage result FOUND
config_service_parse() fails
=> app_controller_init() fails
=> existing station file remains untouched
```

## Implementation scope

Expected production files:

```text
apps/ft8/src/storage_service/storage_service.h
apps/ft8/src/storage_service/storage_service.c
apps/ft8/src/app_controller/app_controller.c
```

Change only what is required to express and consume the typed load result.

Expected test additions:

```text
tests/ft8_storage_service_test.c       # focused private application-storage contract
tests/linux_ft8_config_load.py         # real controller/application policy integration
CMakeLists.txt                         # register focused tests
```

Equivalent narrowly scoped test filenames are acceptable.

Update this T003 task file with implementation notes before handoff.

Canonical MiniFT8 docs should be changed only if a current statement becomes inaccurate. Do not perform general F10 documentation cleanup here.

## Required automated regression — storage layer

Add a focused host unit test around `storage_service` using a fake `mini_fs_api_t`.

It must prove at least:

1. valid small text -> `FOUND`, exact text, NUL termination;
2. open returns `MINI_ERR_NOT_FOUND` -> `NOT_FOUND`;
3. open returns another error -> `ERROR`;
4. read returns error after open -> `ERROR` and acquired handle is closed exactly once;
5. file is larger than output buffer -> `ERROR`, not `NOT_FOUND`, and handle closes;
6. close failure after otherwise successful read -> `ERROR`.

Tests must not depend on Linux POSIX behavior for these cases.

Do not test only the private enum; verify resource cleanup/counts where relevant.

## Required automated regression — controller/application policy

Add a separate Linux integration test registered with root CTest.

Do **not** fold this into the currently known-failing `tests/linux_ft8.py`.

Use an isolated temporary `MINISHELL_ROOT` and built FT8 module.

Prove these real application behaviors:

### Case A — missing station.txt

Start with the FT8 data directory available but no station file.

Run/launch `ft8` sufficiently to execute controller initialization, then exit/return as appropriate.

Verify:

- default `station.txt` is created;
- resulting file parses/contains the established default configuration;
- no stray `.tmp` remains.

This preserves current first-run behavior.

### Case B — oversized existing station.txt

Create an existing station file larger than the storage text buffer.

Record its exact bytes before launch.

Launch `ft8`.

Verify:

- initialization does not replace it with defaults;
- exact original bytes remain unchanged;
- no default `station.txt.tmp` replacement is left;
- MiniShell remains usable / the application failure returns cleanly according to existing lifecycle behavior.

### Case C — malformed but readable existing station.txt

Create a small existing file that loads successfully but fails `config_service_parse()`.

Launch `ft8`.

Verify:

- the original file remains byte-for-byte unchanged;
- no default recovery rewrite occurs.

This behavior already follows the intended policy; retain it as a regression.

If the real Linux backend offers a deterministic, non-privileged way to exercise another open/read failure without fragile permission assumptions, Codex may add it. It is not required because the pure storage test covers injected failures.

## Error-path requirement

When a file handle has been acquired, the storage layer must still attempt exactly one close even after a read/oversize failure.

Do not leave cleanup ownership ambiguous.

A close failure after another failure still yields `ERROR`; no attempt is required to encode multiple simultaneous causes.

## Non-goals

Do **not**:

- implement F01 persistent/runtime grid separation;
- change the station file path or format;
- add recovery backup/restore/versioning;
- add user prompts for corrupt configuration;
- redesign ConfigService;
- change MiniShell Filesystem API;
- change Linux filesystem backend semantics;
- touch ADIF/Cabrillo logging;
- fix F04 copy aliases;
- fix Audio findings;
- fix the known `linux_audio` or `linux_ft8` baseline tests;
- refactor unrelated FT8 controller code.

If a clean implementation appears to require any of these, stop and report the architectural conflict.

## Acceptance criteria

- [ ] Text load has an application-private typed result distinguishing FOUND / NOT_FOUND / ERROR.
- [ ] Only an explicit Filesystem `MINI_ERR_NOT_FOUND` maps to NOT_FOUND.
- [ ] Oversized text maps to ERROR.
- [ ] Read, non-NOT_FOUND open, and close failures map to ERROR.
- [ ] Acquired handles are closed on every load path.
- [ ] `app_controller_init()` creates defaults only for NOT_FOUND.
- [ ] `app_controller_init()` fails without rewriting on ERROR.
- [ ] Parse failure preserves the existing station file.
- [ ] First-run/missing-file default creation still works.
- [ ] Station file path/format/defaults remain unchanged.
- [ ] Public MiniShell API is unchanged.
- [ ] F01 and other T001 findings remain out of scope.
- [ ] Focused storage fault-injection tests pass.
- [ ] Separate Linux controller-policy integration passes.
- [ ] Normal Linux build succeeds.
- [ ] Existing unrelated baseline failures are not modified.
- [ ] Architecture checks pass.
- [ ] No unrelated cleanup is included.

## Automated tests

Run any new focused target directly first.

Then run:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux -R 'ft8_storage|ft8_config_load' --output-on-failure
ctest --test-dir build-linux --output-on-failure
```

If your actual focused CTest names differ, record them exactly.

The accepted pre-T003 baseline has two unrelated known failures:

```text
linux_audio   stale expected-output substring
linux_ft8     stale hard-coded queue-order expectation
```

Do not modify those tests or product behavior. Record whether they remain the only full-suite failures.

Also run:

```bash
python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer
python3 tests/ft8_platform_boundary.py .
git diff --check
```

## Manual / hardware validation

None required.

This task concerns deterministic application storage policy and can be proven by injected host tests plus an isolated Linux integration.

## Codex branch / PR

Use:

```text
branch: codex/T003-station-load-result
PR:     T003: preserve station config on load failure
```

Only T003-related files belong in the PR.

## Codex implementation notes

Codex fills this section and changes `Status` from `READY` to `REVIEW` before handoff.

### Implementation summary

### Typed load-result design

### Files changed

### Invariants preserved

### Tests added

### Tests run and results

### Manual/hardware validation still required

### Known limitations / risks

### Commit / PR

## Supervisor review

Supervisor reviews the actual diff, ownership split, all error/resource paths, integration behavior, and tests.

## Architect test result

No hardware test is expected. Architect records acceptance/rejection after supervisor review.
