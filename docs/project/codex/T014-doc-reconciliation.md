# T014 — Reconcile canonical architecture and status documentation

Status: READY

## Objective

Close T001 F10 and F13 by making the canonical/current-state documentation match the implementation already accepted on `main`.

This is a **documentation-only reconciliation** task.

Do not change production source, public API, tests, runtime behavior, persisted formats, UI behavior, or hardware configuration.

## Why this task exists

The code has moved ahead of several canonical documents. Stale text currently describes:

- ADV external applications under `/flash/<app>.elf` and `/sd/<app>.elf` instead of the implemented `/flash/apps/<app>.elf` and `/sd/apps/<app>.elf`;
- Digital I/O as future even though it is a public v3 service and Keyer K2/K4 use it;
- Keyer as "next/future" despite a working runtime ELF, GPIO KeyIn/KeyOut, and implemented sidetone;
- MiniFT8 modules such as `qso_scheduler` and "future ft8_engine/RX edge" that no longer represent the production architecture;
- MiniFT8 live RX as paused/not integrated, although continuous QMX/ALSA RX is working;
- logging storage/serialization as controller-owned after T006 extracted `log_service`;
- Memory diagnostics as polled only while V/Memory is visible, although the controller builds the memory snapshot on every complete `UiModel` build;
- current status summaries that still say Keyer K5 sidetone is next.

F10/F13 are documentation defects, not authorization for code changes.

## Source-of-truth rule

Before editing prose, verify claims against current `main` source and already accepted task evidence.

Highest priority remains:

```text
include/minishell/api.h
current production source
accepted T002-T013 task evidence
canonical current-state docs
historical stage docs
```

Do not "fix" code to match stale prose.

## Required canonical corrections

### 1. ADV application packaging / resolution

Update all canonical architecture text that still says:

```text
/flash/<app>.elf
/sd/<app>.elf
```

to the implemented form:

```text
/flash/apps/<app>.elf
/sd/apps/<app>.elf
```

Current ADV resolution:

```text
1. compiled-in application
2. /flash/apps/<app>.elf
3. /sd/apps/<app>.elf
```

Use concrete Keyer examples:

```text
/flash/apps/keyer.elf
/sd/apps/keyer.elf
```

At minimum reconcile:

```text
docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/resident-vs-app.md
```

Do not globally rewrite historical task/audit records merely because they mention the old path as past evidence.

### 2. Current public services

Canonical architecture must list **Digital I/O** as an implemented public MiniShell v3 service, not a future service.

Current public service set:

```text
App
System
Console
Memory
Filesystem
Time/Location
Display
Input
Audio
Digital I/O
```

Do not invent a public Control service; Control remains future.

### 3. ADV runtime milestone/status

Replace stale "ADV next runtime external ELF" language with the implemented state:

- ADV supports compiled-in applications plus runtime external ELF loading;
- runtime ELF loading has been hardware-validated;
- `keyer.elf` is an actual external application target, not merely the next loader proof;
- same ELF can run from `/sd/apps` and `/flash/apps`;
- compiled-in > flash > SD resolution remains the runtime rule.

Keep historical descriptions only when explicitly labeled historical.

### 4. MiniFT8 architecture module map

Reconcile `docs/MiniFT8/architecture.md` to the current production modules.

Current key ownership map must describe at least:

```text
ft8_main
    lifecycle/top-level loop only

ft8_ui_adapter
    MiniShell Display/Input <-> UiFrame/UiInput

app_controller
    sole cross-module coordinator

ui_shell
    screen/submenu/navigation/rendering policy

config_service
    persistent station/config values

auto_seq
    pure QSO sequencing / queue / eligibility / typed log event

tx_lifecycle
    pure TX-slot/parity/lifecycle eligibility

rx_audio_adapter
    MiniShell Audio RX edge

rx_frontend
    canonical 12 kHz stereo -> 6 kHz mono frontend

rx_slot_framer
    FT8 slot/sample progression and block framing

ft8_engine
    FT8 DSP/protocol decode/hash state

rx_result_builder
    engine result -> factual RxBatch entries

storage_service
    FT8 config/text persistence helpers where still applicable

log_service
    ADIF/Cabrillo serialization, date/time/frequency/path policy,
    and copy-on-write filesystem mutation through injected MiniShell APIs

presentation_profile
    presentation/layout profile facts
```

Remove stale production ownership for:

```text
qso_scheduler
future MiniFT8 RX-audio edge
future ft8_engine
paused RX implementation
```

Do not add modules that do not exist.

### 5. MiniFT8 current implementation boundary

Rewrite the stale "not yet implemented/integrated" section so it reflects the accepted production baseline:

Implemented:

```text
MiniShell Audio RX API/service
Linux WAV RX provider
Linux live ALSA/QMX capture worker + ring
rx_audio_adapter
RxFrontend
RxSlotFramer
Ft8Engine
RxResultBuilder / RxBatch
continuous multi-slot live RX
AutoSeq AS-0..AS-8
simulated TX lifecycle
ADIF + Field Day Cabrillo logging via log_service
```

Still future/not production:

```text
physical QMX TX realization
generic MiniShell Control/CAT service
physical CAT/control integration
```

Do not claim physical MiniFT8 TX exists.

### 6. Logging ownership

Canonical/current-state docs must reflect T006/T007:

```text
AutoSeq
    owns pure log eligibility/event + per-format ACK state

app_controller
    owns ordering/coordination at TX start

log_service
    owns ADIF/Cabrillo serialization, date/frequency/path policy,
    and atomic copy-on-write file mutation through MiniShell APIs
```

Do not say file/time I/O or serialization is still implemented directly inside `app_controller_tx.c`.

At minimum reconcile any stale statement in:

```text
docs/MiniFT8/architecture.md
docs/project/progress.md
```

### 7. Keyer sidetone / current status

Reconcile current Keyer docs/status summaries.

Implementation facts:

- `sidetone` module exists;
- controller coordinates it as a sibling of KeyIn/engine/KeyOut;
- it uses MiniShell Audio TX only;
- ADV speaker provider realizes the `speaker` endpoint;
- T009 measured current 48-frame/48 kHz behavior;
- T010 fixed provider timeout/nonblocking semantics and hardware-validated it;
- Keyer timing remains owned by engine/controller, not Audio provider.

Update module diagrams where appropriate to include sidetone:

```text
                  +--> keyin
                  +--> keyer_engine
app_controller ---+--> keyout
                  `--> sidetone
```

Do not overstate validation:

- software implementation is complete;
- ADV Audio TX/sidetone transport behavior has hardware evidence;
- if full "field Keyer K5" validation requires audible/operator end-to-end behavior not already recorded, phrase status as "implemented / transport hardware-validated" rather than inventing an unrecorded field acceptance.

Reconcile stale "K5 next" text in at least:

```text
README.md
docs/project/progress.md
docs/keyer/README.md
```

Choose the most accurate status wording from existing evidence rather than automatically marking later K6/K7 complete.

### 8. Memory diagnostic polling — F13

Correct `docs/MiniFT8/ui.md`.

Current behavior:

- `app_controller_build_model()` (or current equivalent model-build path) snapshots Memory facts when building the complete UiModel;
- that model build is not conditional on V/Memory being the visible screen;
- V/Memory displays the snapshot, but screen visibility does not own whether the controller obtains those facts;
- keep the controller/UI ownership boundary: controller supplies facts, UI decides presentation/redraw/navigation.

Replace the stale claim:

```text
The memory snapshot is queried only while V -> 1 Memory is visible.
```

with wording matching actual controller snapshot behavior.

Do not move polling policy into `ft8_main` or `ui_shell`; this is documentation only.

## Current-state summaries

Review these for stale statements caused by the same already-accepted changes:

```text
README.md
docs/project/progress.md
docs/keyer/README.md
docs/MiniFT8/architecture.md
docs/MiniFT8/ui.md
docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/resident-vs-app.md
```

Also inspect:

```text
docs/MiniFT8/README.md
docs/MiniFT8/development.md
```

Edit them only if they contain a directly conflicting current-state statement.

Avoid broad stylistic rewriting.

## Historical records policy

Do **not** rewrite:

- old completed T00x task packets;
- T001 audit evidence;
- historical RX/AS stage documents;

unless a file explicitly presents itself as current canonical state.

Historical text may remain stale by design if its date/stage makes that clear.

## Verification

### Static consistency search

After edits, search canonical/current-state docs for stale patterns such as:

```bash
rg -n '/flash/<app>\.elf|/sd/<app>\.elf|/flash/keyer\.elf|/sd/keyer\.elf' \
  README.md docs/architecture docs/MiniFT8 docs/keyer docs/project/progress.md

rg -n 'Digital I/O.*future|future.*Digital I/O|K5.*NEXT|K5 next|qso_scheduler|RX implementation is temporarily paused|future MiniFT8 RX-audio edge|future `ft8_engine`' \
  README.md docs/architecture docs/MiniFT8 docs/keyer docs/project/progress.md

rg -n 'memory snapshot.*only|only while.*Memory|continuously poll memory' \
  docs/MiniFT8/ui.md

rg -n 'app_controller.*logging|file/time I/O.*app_controller|logging.*app_controller' \
  docs/MiniFT8 docs/project/progress.md
```

Inspect matches in context. Do not blindly require zero matches when a phrase is explicitly historical/future in a different sense.

### Production-source cross-check

Use source only to confirm documentation claims, especially:

```text
include/minishell/api.h
platform/adv/README.md
apps/ft8/src/app_controller/
apps/ft8/src/log_service/
apps/ft8/src/rx_*/
apps/ft8/src/ft8_engine/
apps/keyer/src/app_controller/
apps/keyer/src/sidetone/
platform/adv/adv_audio_speaker.cpp
```

No source edits.

### Local tests

Because this is documentation-only, normal regression evidence is sufficient:

```bash
git status --short
git diff --check

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux -R 'architecture|ft8_reference_pin' --output-on-failure

cmake -S tests/unit -B /tmp/T014-build-unit
cmake --build /tmp/T014-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T014-build-unit --output-on-failure

ctest --test-dir build-linux --output-on-failure
```

The accepted `linux_audio` and `linux_ft8` baseline failures may remain.

No hardware validation and no GitHub Actions wait are required.

## Scope

Expected changes are documentation only, primarily:

```text
README.md
docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/resident-vs-app.md
docs/MiniFT8/architecture.md
docs/MiniFT8/ui.md
docs/keyer/README.md
docs/project/progress.md
docs/project/codex/T014-doc-reconciliation.md
```

Possibly `docs/MiniFT8/README.md` or `development.md` if a direct conflict is found.

No production/test/CMake/workflow changes.

## Non-goals

Do not:

- change runtime behavior;
- change public API;
- change module ownership;
- redesign UI;
- change Keyer timing;
- change application resolution;
- change logging behavior;
- fix the two known stale Linux tests;
- mark K6/K7 complete without evidence;
- claim physical MiniFT8 TX;
- rewrite historical task/audit documents;
- perform prose cleanup unrelated to F10/F13/current-status consistency.

## Acceptance criteria

- [ ] canonical ADV ELF paths use `/flash/apps` and `/sd/apps`;
- [ ] Digital I/O is documented as current public service;
- [ ] runtime external ELF is documented as implemented/validated, not future;
- [ ] MiniFT8 module map matches current source;
- [ ] stale `qso_scheduler`/future RX/paused RX production text is removed or clearly historical;
- [ ] MiniFT8 live RX/current boundary matches production;
- [ ] logging ownership matches AutoSeq/controller/log_service split;
- [ ] Keyer diagrams/status include sidetone accurately;
- [ ] current summaries no longer say K5 is simply "next";
- [ ] K6/K7 are not falsely marked complete;
- [ ] Memory snapshot documentation matches current controller model-build behavior;
- [ ] implemented vs hardware-validated status is distinguished where relevant;
- [ ] no production/test/build/workflow source changes;
- [ ] static consistency searches reviewed;
- [ ] architecture/reference focused tests pass;
- [ ] unit suite passes;
- [ ] full Linux suite result recorded;
- [ ] no unrelated documentation rewrite.

## Branch workflow

Use:

```text
codex/T014-doc-reconciliation
```

Before handoff:

1. set Status to REVIEW;
2. list every canonical/current-state file changed and why;
3. record stale-pattern searches before/after;
4. record source cross-checks;
5. run local regression commands;
6. commit and push;
7. return commit SHA;
8. do not open a PR;
9. do not wait for GitHub Actions.

Supervisor reviews `main..<SHA>`. If clean, fast-forward/merge to `main`, then delete local and remote T014 branches.

## Codex implementation notes

### Implementation summary

### Canonical architecture corrections

### MiniFT8 ownership/status corrections

### Keyer status corrections

### Memory polling correction

### Files changed

### Static consistency searches

### Source cross-checks

### Local tests run and results

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews the actual documentation diff against current production source and accepted task evidence.

## Architect test result

No hardware validation is required for this documentation-only task.
