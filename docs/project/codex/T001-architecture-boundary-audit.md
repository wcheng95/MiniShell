# T001 — Architecture boundary and ownership audit

Status: READY

## Architect intent

Before any further feature implementation, perform a repository-wide architecture review to verify that MiniShell still follows a top-down modular design with clean ownership and dependency boundaries.

The architect's priority is not merely that the code works. The design must remain understandable, replaceable by layer, and free of hidden cross-module ownership or platform coupling.

This task is a **read-only engineering audit**. Do not fix findings in T001.

## Objective

Review the current MiniShell code and canonical documentation and produce an evidence-backed architecture audit that answers:

> Does the current repository preserve the intended top-down dependency direction, single ownership of state/policy/resources, and clean module/platform boundaries?

Identify every material violation, ambiguity, or documentation/code mismatch that should be resolved before new feature work proceeds.

## Current context

Recent work materially extended MiniFT8 on Linux:

- live QMX USB-UAC RX through Linux ALSA
- explicit ALSA capture start
- independent Linux capture worker/ring buffering so synchronous FT8 decoding does not starve capture
- V2-compatible FT8 live timing: decode after 79 blocks / 12.64 s and continue through the slot
- AutoSeq AS-0..AS-8 complete
- V2-style daily ADIF logging
- V2-style Field Day Cabrillo logging
- repository documentation consolidation

These changes work in real pc-1/QMX testing, but functionality is not sufficient evidence of architectural correctness. T001 exists to stop and audit the structure before proceeding.

The repository also contains Keyer and resident MiniShell services/platform backends; the audit is repository-wide, not MiniFT8-only.

## Source of truth

Read these first, in this order:

```text
AGENTS.md
README.md
docs/README.md
include/minishell/api.h

docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/resident-vs-app.md
docs/architecture/configuration.md

docs/project/architecture-cleanup.md
docs/project/progress.md

docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/architecture.md
docs/MiniFT8/ui.md

docs/keyer/README.md
```

Detailed `rx-*`, `as-*`, Keyer stage documents, and historical plans are supporting evidence only. Current-state documents take precedence when status prose conflicts.

For MiniFT8 V2 behavior that V3 intentionally preserves, the pinned behavioral reference is:

```text
repository: wcheng95/Mini-FT8
commit:     491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

Use V2 only to understand preserved behavior. Do not treat V2's platform coupling or module structure as a V3 architecture model.

## Architectural model to verify

### Repository-level dependency direction

The intended top-down direction is:

```text
application / domain policy
        |
        v
MiniShell public API
        |
        v
portable MiniShell services/core
        |
        v
private backend/port contract
        |
        v
platform implementation
Linux / ADV / mocks
```

Dependencies must not point upward around this graph.

In particular:

- application code must not reach directly into POSIX, ALSA, pthread, ESP-IDF, board APIs, USB stacks, FATFS, GPIO drivers, or backend-private interfaces;
- portable MiniShell services/core must not contain application-domain policy;
- platform/backend code must implement platform mechanics, not MiniFT8/Keyer product policy;
- application-visible behavior must go through the public MiniShell API unless a documented architecture explicitly owns it elsewhere.

### Application coordination rule

For structured applications such as MiniFT8 and Keyer:

```text
main
    lifecycle/bootstrap only
        |
        v
app_controller
    sole application coordinator
        |
        +--> sibling/domain modules
```

Sibling/domain modules must not become hidden peer coordinators or side-talk directly in ways that bypass `app_controller` unless the current canonical architecture explicitly documents that dependency.

### Ownership rule

Every mutable state/policy/resource must have one clear owner.

Audit for:

- duplicated mutable state or cached policy with unclear authority;
- the same lifecycle/resource being opened/closed or advanced by multiple owners;
- UI/presentation code owning domain policy;
- transport/provider code owning application timing or protocol policy;
- domain modules reading clocks/files/devices directly when a coordinator should supply facts/events;
- application modules retaining pointers/references across owner lifetimes;
- helper modules that silently orchestrate sibling modules;
- public/private API boundaries being bypassed for convenience.

### Pure-domain rule

Modules documented as pure/domain logic must remain independent of MiniShell and platform facilities.

Examples include, where applicable:

```text
MiniFT8 Ft8Engine submodules
RxFrontend
RxSlotFramer
RxResultBuilder
auto_seq
TxLifecycle
Keyer engine/domain modules
```

Check both includes and behavior, not only filenames.

### Platform/resource rule

Platform-specific mechanisms belong below the MiniShell public service boundary.

Pay particular attention to recent Linux live-audio work:

```text
platform/linux/linux_audio_wav.c
platform/linux/linux_audio_buffered.h
platform/linux/linux_services.c
```

Verify that ALSA/pthread/ring-buffer mechanics remain generic MiniShell Audio-provider concerns and do not leak QMX/FT8 policy upward or downward.

Also verify resource ownership and cleanup on normal exit and error paths.

### MiniFT8 logging rule

Review the recent ADIF/Cabrillo implementation carefully.

The intended policy split is:

```text
auto_seq
    owns QSO state and typed log eligibility only

app_controller
    owns application coordination / TX-start event ordering

MiniShell Filesystem + Time/Location
    own platform-visible storage/time mechanics
```

Determine whether the current placement and implementation preserve clean responsibility boundaries or whether logging serialization/storage has become mixed into a coordinator in a way that should be separated before future growth.

Do not fix it in this task; report evidence and recommended ownership if needed.

## Required audit dimensions

Review at least these dimensions.

### A. Layering / forbidden upward dependencies

Find direct includes, calls, globals, callbacks, or shared state that bypass the intended layer graph.

### B. Application dependency graph / side-talk

For MiniFT8 and Keyer, derive the actual production dependency graph from code and compare it with the documented graph.

Identify sibling-to-sibling coordination that should flow through `app_controller`.

### C. State and policy ownership

For major runtime state, identify the owner and check for duplicates or shadow owners.

At minimum inspect:

```text
MiniShell app lifecycle
filesystem handles/quota
Audio stream lifecycle
Digital I/O lifecycle
MiniFT8 RX timing state
Ft8Engine DSP/hash state
RxBatch/selection state
AutoSeq QSO state
TX lifecycle/parity state
logging eligibility + persistence state
station/config state
Keyer engine + KeyIn/KeyOut state
```

### D. Platform contamination

Search application and portable-core code for platform-specific headers/APIs and indirect platform assumptions.

Examples to search for include, but are not limited to:

```text
alsa/
snd_
pthread
unistd
fcntl
sys/
linux/
esp_
freertos
driver/
M5
FATFS
GPIO
USB/UAC
```

Context matters: these are allowed in platform/backend implementation and forbidden where the architecture says code is portable.

### E. Public/private API discipline

Check that applications use `include/minishell/api.h` rather than backend-private service/port structures.

Check for accidental export expansion or domain-specific additions to generic MiniShell services.

### F. Lifecycle / cleanup ownership

Audit open/start/read/stop/close and allocation/free paths for ownership symmetry and error-path cleanup.

This is an architecture audit, not a leak hunt only: flag cases where lifecycle responsibility is spread across modules even if current tests happen to pass.

### G. Documentation vs implementation

Find current canonical documentation that states an ownership/dependency invariant the code no longer follows, or code architecture that is not represented correctly in current docs.

Do not report stale status text in explicitly historical stage docs unless it can mislead current architecture decisions.

### H. Mechanical architecture tests

Review the existing architecture tests themselves. Determine whether they actually enforce the current intended boundaries or have blind spots.

At minimum inspect:

```text
tests/app_dependency_boundary.py
tests/ft8_platform_boundary.py
```

Also identify any architecture invariant that is important enough to automate but currently exists only in prose.

## Findings classification

Every finding must use one of these severities:

```text
BLOCKER  clear ownership/layering violation that should be fixed before feature work
HIGH     material architecture erosion or duplicate ownership likely to cause future coupling
MEDIUM   boundary ambiguity, misplaced responsibility, or enforcement gap worth correcting
LOW      cleanup/documentation clarity issue with little immediate structural risk
```

Do not inflate severity. A style preference is not an architecture violation.

For every finding include:

```text
ID
Severity
Category
Files + line numbers / symbols
Observed dependency or ownership
Expected invariant
Why it matters
Recommended ownership/direction
Suggested follow-up task boundary
```

If something looks suspicious but is intentionally valid, put it under **Reviewed and accepted structures**, not Findings.

## Required deliverable

Update this same file on the Codex branch by replacing/filling the sections below.

The report must contain:

1. **Executive conclusion**
   - `CLEAN`, `CLEAN WITH FOLLOW-UPS`, or `VIOLATIONS FOUND`.
   - This is descriptive, not permission to merge future features; the architect/supervisor decide that.

2. **Actual dependency maps**
   - repository-level layers;
   - MiniFT8 production module graph;
   - Keyer production module graph.

3. **Ownership table**
   - major state/resource -> current owner -> expected owner -> result.

4. **Findings**
   - evidence-backed and ordered by severity.

5. **Reviewed and accepted structures**
   - notable cases checked and found consistent, especially recent ALSA buffering and logging boundaries.

6. **Architecture-test coverage gaps**
   - what should be mechanically enforced later.

7. **Recommended cleanup sequence**
   - smallest top-down sequence of separate follow-up tasks;
   - no code fixes in T001.

8. **Codex audit notes**
   - commands/searches/tests used and any areas that could not be fully verified.

## Implementation scope

Allowed changes in T001:

```text
docs/project/codex/T001-architecture-boundary-audit.md
```

No production code changes.
No test changes.
No canonical architecture changes.
No formatting cleanup elsewhere.

If Codex discovers an obvious bug while auditing, report it as a finding; do not fix it.

## Non-goals

Do **not**:

- implement physical MiniFT8 TX;
- refactor logging;
- change the Linux Audio implementation;
- update public MiniShell APIs;
- rename files/modules;
- fix stale `linux_ft8.py` queue-order expectations;
- redesign AutoSeq, Ft8Engine, Keyer, or MiniShell services;
- rewrite documentation outside this task report;
- make opportunistic cleanup commits.

## Acceptance criteria

- [ ] Entire production repository layering reviewed, not only MiniFT8.
- [ ] MiniFT8 actual dependency graph documented and compared with canonical ownership.
- [ ] Keyer actual dependency graph documented and compared with canonical ownership.
- [ ] Major mutable-state/resource ownership table completed.
- [ ] Recent Linux ALSA/capture-worker design explicitly reviewed.
- [ ] Recent ADIF/Cabrillo placement explicitly reviewed.
- [ ] Existing architecture-boundary tests reviewed for blind spots.
- [ ] Every material finding has file/symbol/line evidence and an expected invariant.
- [ ] No production code, tests, or unrelated docs changed.
- [ ] Full Linux build/test baseline run and results recorded, with pre-existing failures distinguished from audit findings.
- [ ] Recommended follow-up work is split into bounded tasks rather than one broad refactor.

## Automated baseline

Run from a clean branch based on current `main`:

```bash
git status --short
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
```

Also run the architecture checks directly so their output is visible in the report:

```bash
python3 tests/app_dependency_boundary.py .
python3 tests/ft8_platform_boundary.py .
```

If a direct invocation requires different arguments, inspect CMake/tests and use the repository-defined invocation; record the exact command used.

Do not repair failures in this task.

## Manual / hardware validation

None. This is a static/code/document architecture audit plus host baseline tests.

The architect will decide which reported findings become follow-up tasks.

## Codex implementation notes / audit report

Codex fills this entire section before handoff and changes `Status` from `READY` to `REVIEW`.

### Executive conclusion

### Actual dependency maps

#### Repository-level

#### MiniFT8

#### Keyer

### Ownership table

### Findings

### Reviewed and accepted structures

### Architecture-test coverage gaps

### Recommended cleanup sequence

### Commands / tests / searches performed

### Areas not fully verified

### Commit / PR

## Supervisor review

Supervisor fills this only after reading the actual Codex report and independently checking material findings against the repository.

## Architect decision

Architect records which findings are accepted, rejected, or deferred and authorizes follow-up tasks.
