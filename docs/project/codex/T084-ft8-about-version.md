# T084 — MiniFT8 About version label

Status: TESTING

## Intent

Expose the accepted MiniFT8-V3 baseline version on the existing read-only
`V -> 6 About` screen.

Current accepted runtime version:

```text
MiniFT8-V3.083
```

The `.083` suffix identifies the accepted T083 CAT-fault-containment baseline.

This task does **not** rename the architectural generation from "MiniFT8-V3" to
"MiniFT8-V3.083" throughout historical/design documentation. "V3" remains the
generation name; `MiniFT8-V3.083` is the current runtime/product version label.

Version bumps are tied to the MiniFT8 binary identity.

After T084, every future task that changes the production MiniFT8 executable must
update `V -> 6 About` so the displayed suffix matches that task number.

Examples:

```text
T085 changes MiniFT8 production code -> MiniFT8-V3.085
T091 changes MiniFT8 production code -> MiniFT8-V3.091
```

Documentation-only, test-only, or unrelated MiniShell tasks do not bump the
MiniFT8 version.

This lets the architect identify which MiniFT8 binary is actually running on ADV
without needing a Git SHA or build metadata.

## Current state

`V -> 6 About` already exists.

Current About content begins with:

```text
MiniFT8-V3
MiniShell application
Runtime app: ft8
...
```

No new menu entry, submenu, navigation path, controller state, or platform API is
needed.

## Required change

Change the first About line to exactly:

```text
MiniFT8-V3.083
```

Preserve the rest of `V -> 6 About` behavior unless a test requires only the
minimal expected-string update.

Expected screen content:

```text
MiniFT8-V3.083
MiniShell application
Runtime app: ft8
FT8 protocol only
V is strictly read-only
```

Existing footer/back/quit behavior remains unchanged.

## Version ownership / four principles

### Modularity

The version label is MiniFT8 application metadata. Do not add MiniShell-platform
or ADV-specific version logic.

### Ownership

For T084 there is only one runtime consumer: `V -> 6 About`.

Keep one production source for the displayed string. Do not duplicate version
state across controller, configuration, platform, persisted files, or MiniShell
services.

A file-local constant or direct literal in the About renderer is acceptable and
preferred while there is only one consumer. Do **not** add a new public version
API or configuration key.

### Decoupling

The version label must not affect:

- RX/decode;
- CAT;
- Audio/UAC;
- AutoSeq;
- logging;
- configuration;
- application lifecycle.

### KISS

This should be a tiny UI/document/test change.

Do not add:

- a version parser;
- semantic-version machinery;
- build-number generation;
- Git SHA embedding;
- compile-time CMake version plumbing;
- a new controller/model field;
- a persisted version setting.

If another runtime consumer needs the version later, centralize then.

## Documentation

Update `docs/MiniFT8/ui.md` so `V -> 6 About` explicitly documents the current
first line as:

```text
MiniFT8-V3.083
```

Optionally add one concise note to the current MiniFT8 README/development baseline
that the displayed runtime version is `MiniFT8-V3.083`. Do not mass-replace
historical `MiniFT8-V3` wording.

## Tests

Update/add the narrow UI-shell assertion that entering:

```text
V -> 6
```

renders `MiniFT8-V3.083` on the first content row.

Preserve existing V-root entries:

```text
1 Memory >
2 GPS >
3 QSO / Log >
4 Performance >
5 System Info >
6 About >
```

Run the normal focused UI test plus:

```text
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T084-unit
cmake --build /tmp/T084-unit -j8
ctest --test-dir /tmp/T084-unit --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_platform_boundary.py . ft8
python3 tests/ft8_platform_boundary.py .
python3 tests/architecture_rules.py .

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

## Hardware acceptance

Minimal ADV confirmation:

```text
ft8
V
6
```

Verify first About row shows:

```text
MiniFT8-V3.083
```

and Back/Q behavior is unchanged.

## Non-goals

- no global repository rename from V3 to V3.083;
- no version bump for doc-only/test-only/unrelated tasks;
- production MiniFT8 binary changes must keep About coherent with the task number;
- no new version API;
- no platform-specific behavior;
- no changes outside About/version documentation/tests unless strictly required.

## Codex branch / handoff

Work on:

```text
codex/T084-ft8-about-version
```

Start from current `main`.

Read:

```text
AGENTS.md
docs/README.md
docs/MiniFT8/ui.md
docs/MiniFT8/README.md
docs/project/codex/T084-ft8-about-version.md
```

Keep one small reviewable commit and record normal handoff evidence in this task
packet.


## Future MiniFT8 task rule

For every future task packet that changes production code linked into the
MiniFT8 binary, Codex must include this acceptance item:

```text
V -> 6 About version updated to MiniFT8-V3.<task number>
```

The task's tests must assert the matching About string.

Purpose: the About screen is the architect's on-device binary identifier for ADV.

Do not substitute Git SHA/build timestamps or add generated version machinery
unless separately requested.

## Codex implementation handoff

### Implementation summary

Changed the existing About renderer's first content row to `MiniFT8-V3.083`.
Added a focused assertion for `V -> 6` in both desktop and ADV presentation
profiles and documented the current label in the canonical UI contract.
No deviations from the task.

### Files changed

- `apps/ft8/src/ui_shell/ui_shell.c`: one displayed literal.
- `tests/ft8_ui_smoke.c`: exact padded first-row assertion after About navigation.
- `docs/MiniFT8/ui.md`: About content and explicit version ownership.
- `docs/project/codex/T084-ft8-about-version.md`: status and handoff evidence.

### Behavior/invariants preserved

The About renderer remains the single production source of the version label.
V-root entries, remaining About rows, footer, Back/quit handling, and read-only
behavior are unchanged. No changes to controller state, public APIs, platform
ownership, persisted configuration, RX, CAT, Audio, AutoSeq, or logging.

### Tests run and results

All commands below passed on 2026-09-26:

```text
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux -R '^ft8_ui_smoke$' --output-on-failure
  1/1 passed
ctest --test-dir build-linux --output-on-failure
  128/128 passed

cmake -S tests/unit -B /tmp/T084-unit
cmake --build /tmp/T084-unit -j8
ctest --test-dir /tmp/T084-unit --output-on-failure
  29/29 passed

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_platform_boundary.py . ft8
python3 tests/ft8_platform_boundary.py .
python3 tests/architecture_rules.py .

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
  PASS: minishell_adv.bin generated, 78% app partition free

git diff --check
```

ADV configuration used installed dependencies after a component-registry
connection notice. Compilation reported ESP-IDF header `#include_next`
pedantic warnings; the build completed successfully.

### Hardware/manual validation still required

On ADV, launch `ft8`, enter `V -> 6`, confirm `MiniFT8-V3.083` on the first
content row, and confirm unchanged Back/Q behavior. No hardware was flashed
or exercised for this handoff.

### Known limitations or risks

No known implementation limitations; physical display/navigation acceptance
remains pending. Existing unrelated untracked files were left untouched.

### Commit reference

The single implementation commit containing this handoff on
`codex/T084-ft8-about-version`, based on
`3a79c6dc9d1d44c0f1e9797ca00c78a1d1c5d1fa`. The resulting SHA is supplied in
the handoff response.


## Supervisor review

Reviewed rebased implementation `8cbcfab8bc81dd3451ede3aa59f42edfeff03e00`
against current `main`.

Result: **PASS for software review; advanced to TESTING.**

The production diff remains intentionally narrow:

- `V -> 6 About` first row is exactly `MiniFT8-V3.083`;
- existing About rows/navigation/read-only behavior are unchanged;
- focused UI coverage checks the exact version on both desktop and ADV
  presentation profiles;
- canonical UI documentation carries the future task-number version rule;
- no controller/model field, public API, persisted setting, build-version
  machinery, platform dependency, or unrelated MiniFT8 behavior was added.

Accepted software evidence from the implementation handoff:

```text
Linux CTest:          128/128 PASS
portable units:       29/29 PASS
focused ft8_ui_smoke: PASS
boundary checks:      PASS
ADV build:            PASS
git diff --check:     PASS
```

The implementation branch was rebased onto current main before review.

Remaining gate: ADV manual confirmation of `ft8 -> V -> 6`, exact displayed
version `MiniFT8-V3.083`, and unchanged Back/Q behavior.
