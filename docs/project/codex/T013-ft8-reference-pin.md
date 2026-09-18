# T013 — Align MiniFT8 V2 reference provenance

Status: READY

## Objective

Resolve T001 F11: the FT8 reference workflow must use the same approved MiniFT8-V2 reference revision as the canonical MiniShell documentation, and the provenance must be mechanically checked so it cannot silently drift again.

Canonical MiniFT8-V2 behavioral reference:

```text
repository: wcheng95/Mini-FT8
commit:     491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

Current workflow still checks out:

```text
5bd3ef98f72388a850bebad04bd7300b90edb63c
```

## Pre-task evidence

Supervisor comparison of the two Mini-FT8 revisions shows:

```text
old workflow pin: 5bd3ef98f72388a850bebad04bd7300b90edb63c
canonical pin:    491e757ae6b1e4cfd2b9a6ba10f48b35643849e0

relationship: canonical is 11 commits ahead of old pin
```

Files changed between those revisions include:

```text
.github/workflows/rx1a-reference.yml
components/ft8_lib/ft8/message.c
tests/tx_e2e/CMakeLists.txt
tests/tx_e2e/rx1a_reference_dump.cpp
tests/tx_e2e/test_rx1a_boundaries.cpp
tests/tx_e2e/test_telemetry_decode.cpp
```

The exact golden WAV used by MiniShell's reference workflow is unchanged across both revisions:

```text
tests/tx_e2e/golden/ft8_cq_w1xyz_fn42.wav

old blob SHA:      04b67efb6d1d2092dfc4d57fd2477e9e381eb46d
canonical blob SHA:04b67efb6d1d2092dfc4d57fd2477e9e381eb46d
size:              163724 bytes
```

T013 must independently reproduce/record this evidence locally or through git metadata before changing the workflow.

## Required change

Update:

```text
.github/workflows/ft8-reference.yml
```

so the checkout uses the canonical pin:

```text
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

Do not change any FT8 algorithm, expected decode string, WAV transformation, or golden output in this task.

## Reference provenance policy

The workflow should make the distinction explicit:

```text
behavioral source revision
    = canonical MiniFT8-V2 commit 491e757...

golden WAV artifact
    = tests/tx_e2e/golden/ft8_cq_w1xyz_fn42.wav
    = Git blob 04b67efb6d1d2092dfc4d57fd2477e9e381eb46d
```

The WAV happens to be identical at the old and canonical revisions; that fact explains why previous green reference jobs may still have produced the same WAV-based results, but it does **not** justify keeping the old behavioral source pin.

Add concise workflow comments or a small provenance note in the task/docs where useful. Do not create a broad historical document.

## Drift regression

Add a lightweight repository test that fails if the reference workflow pin diverges from the canonical MiniFT8-V2 commit documented by MiniShell.

Preferred:

```text
tests/ft8_reference_pin.py
```

The test should inspect repository text and verify at least:

1. `.github/workflows/ft8-reference.yml` contains the canonical commit;
2. `AGENTS.md` contains the same canonical commit;
3. `docs/MiniFT8/README.md` contains the same canonical commit;
4. `docs/MiniFT8/development.md` contains the same canonical commit.

Avoid a fragile requirement that these files contain the SHA only once if normal prose needs repetition. The invariant is that there is no conflicting full 40-hex MiniFT8-V2 pin in the relevant reference declarations.

The checker may define the canonical SHA once internally as the expected value. Do not introduce runtime/generated configuration merely to eliminate four readable documentation references.

Register the check in root CTest.

## Workflow validation

Because GitHub Actions is not the gate for this cleanup loop, validate locally as far as practical.

Required:

- parse the YAML structurally if PyYAML is already available; otherwise a focused text validation is sufficient;
- verify the checkout `repository` remains `wcheng95/Mini-FT8`;
- verify the checkout `ref` is canonical;
- verify the golden WAV path remains unchanged;
- verify no reference test command/expected string changed unintentionally.

Do not add a new dependency just to parse YAML.

## Optional local reference run

If the Mini-FT8 canonical checkout is already available locally, or can be fetched normally on the developer machine, run the existing consolidated reference suite against the canonical WAV.

At minimum, when practical:

```bash
cmake -S . -B /tmp/T013-ft8-reference \
  -DFT8_RX1C_REFERENCE_WAV=/path/to/canonical/tests/tx_e2e/golden/ft8_cq_w1xyz_fn42.wav \
  -DFT8_RX1D_REFERENCE_WAV=/path/to/canonical/tests/tx_e2e/golden/ft8_cq_w1xyz_fn42.wav \
  -DFT8_RX1G_REFERENCE_WAV=/path/to/canonical/tests/tx_e2e/golden/ft8_cq_w1xyz_fn42.wav

cmake --build /tmp/T013-ft8-reference -j"$(nproc)"
ctest --test-dir /tmp/T013-ft8-reference \
  -R '^(ft8_monitor_rx1c|ft8_decoder_rx1d|ft8_engine_rx1g)' \
  --output-on-failure
```

If the reference repo is not locally available and fetching it would be the only blocker, record that limitation; do not fabricate results.

## Scope

Expected changes:

```text
.github/workflows/ft8-reference.yml
tests/ft8_reference_pin.py
CMakeLists.txt
docs/project/codex/T013-ft8-reference-pin.md
```

Possibly a tiny canonical-doc wording clarification only if needed for provenance.

Do not change:

```text
apps/ft8/
core/
platform/
include/minishell/api.h
reference decode expectations
golden WAV content
DSP/protocol behavior
```

## Local test gate

Run:

```bash
git status --short

python3 tests/ft8_reference_pin.py .

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux -R 'ft8_reference_pin|architecture' --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_platform_boundary.py . ft8
python3 tests/ft8_platform_boundary.py .

cmake -S tests/unit -B /tmp/T013-build-unit
cmake --build /tmp/T013-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T013-build-unit --output-on-failure

git diff --check

ctest --test-dir build-linux --output-on-failure
```

The accepted `linux_audio` and `linux_ft8` baseline failures may remain and must not be altered.

No hardware validation and no GitHub Actions wait are required.

## Acceptance criteria

- [ ] old workflow pin is replaced by canonical `491e757...`;
- [ ] workflow still checks out `wcheng95/Mini-FT8`;
- [ ] golden WAV path/content is not changed;
- [ ] old vs canonical revision relationship is recorded;
- [ ] identical golden WAV blob provenance is recorded;
- [ ] no DSP/protocol/reference expected output is rebaselined;
- [ ] drift regression checks workflow + canonical docs;
- [ ] drift regression is registered in root CTest;
- [ ] FT8 architecture checks pass;
- [ ] unit suite passes;
- [ ] full Linux suite result recorded;
- [ ] optional canonical reference tests are run if locally practical, otherwise limitation recorded;
- [ ] no unrelated cleanup.

## Branch workflow

Use:

```text
codex/T013-ft8-reference-pin
```

Before handoff:

1. set Status to REVIEW;
2. record old/canonical commit comparison and WAV blob evidence;
3. record exact workflow change;
4. run local tests;
5. commit and push;
6. return commit SHA;
7. do not open a PR;
8. do not wait for GitHub Actions.

Supervisor reviews `main..<SHA>`. If clean, fast-forward/merge to `main`, then delete local and remote T013 branches.

## Codex implementation notes

### Implementation summary

### Revision comparison

### Golden WAV provenance

### Pin-drift regression

### Workflow change

### Files changed

### Local tests run and results

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and provenance evidence.

## Architect test result

No hardware validation is required for this workflow/provenance task.
