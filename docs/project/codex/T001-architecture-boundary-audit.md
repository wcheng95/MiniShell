# T001 — Architecture boundary and ownership audit

Status: REVIEW

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

- [x] Entire production repository layering reviewed, not only MiniFT8.
- [x] MiniFT8 actual dependency graph documented and compared with canonical ownership.
- [x] Keyer actual dependency graph documented and compared with canonical ownership.
- [x] Major mutable-state/resource ownership table completed.
- [x] Recent Linux ALSA/capture-worker design explicitly reviewed.
- [x] Recent ADIF/Cabrillo placement explicitly reviewed.
- [x] Existing architecture-boundary tests reviewed for blind spots.
- [x] Every material finding has file/symbol/line evidence and an expected invariant.
- [x] No production code, tests, or unrelated docs changed.
- [x] Full Linux build/test baseline run and results recorded, with pre-existing failures distinguished from audit findings.
- [x] Recommended follow-up work is split into bounded tasks rather than one broad refactor.

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

**VIOLATIONS FOUND.** Audited baseline: `32a84980c4a98b8f6ed3f6450dbd057eea385d80`
(`origin/main`, fetched before creating this branch). This conclusion describes the
review findings; it does not authorize architecture changes or future features.

The main dependency direction is intact: production applications use MiniShell,
pure domain modules do not call platform services, and both structured applications
retain controller-owned coordination. The Linux ALSA worker is a generic provider
mechanism, not an FT8 scheduler. No BLOCKER-level forbidden production application
platform call was found.

There are nevertheless material ownership/lifetime defects: transient GPS state can
be persisted as station configuration, failed configuration reads can replace an
existing file, filesystem tokens can alias later resources, and live capture can
conceal discontinuities from the application timing owner. The utility review also
confirmed destructive copying through equivalent logical paths. Logging remains
correctly above MiniShell and outside AutoSeq, but file policy and persistence
failure handling need bounded follow-up work. Public/private export enforcement and
canonical documentation have gaps.

Findings below comprise **5 HIGH, 7 MEDIUM, and 1 LOW** items. HIGH findings include
concrete resource/data defects found during the architecture review, not claims
that every item is a forbidden dependency. Static conclusions and executed probes
are distinguished below. No production code, tests, public API, configuration
format, or canonical documentation was changed.

### Actual dependency maps

Arrows below mean production calls/dependencies unless explicitly labeled as data
flow. Source locations refer to the audited baseline, not a proposed redesign.

#### Repository-level

```text
platform entry (Linux main / ADV app_main)
  -> core/minishell_runtime
     -> shell -> app_manager -> private platform loader/registry
     -> minishell_services configure / app_begin / app_end

portable apps (ft8, keyer, hello, cat, cp, date, df, free,
               ls, mkdir, mv, nano, rm, rmdir)
  -> mini_api_get / include/minishell/api.h
  -> portable services: System, Console, Memory, Filesystem,
                        Time/Location, Display, Input, Audio, Digital I/O
  -> minishell_services_port_t callbacks / platform_backend.h
  -> Linux native loader, terminal, filesystem, clock, Audio, simulated Digital I/O
     or ADV loader/registry, FATFS/media, display/keyboard, RTC/GPS, GPIO, speaker
```

App lifecycle is a runtime contract, not an additional `mini_api_t.app` function
table. The actual public table in `include/minishell/api.h` is the authority.
`core/app_manager.c:14-18` brackets each platform run with portable resource cleanup.
`core/minishell_runtime.c` starts resident producers after service configuration and
stops them before replacing the port. Service helpers remain private implementation
of their service, not additional application interfaces.

Private producer ingress is intentional: keyboard/input providers submit normalized
events through `minishell_services_input_submit`; ADV GPS publishes UTC/location
facts through the private Time/Location ingress. These are explicit service-owned
contracts (`core/minishell_services/minishell_services.h:152-157`), not providers
calling application policy. The ADV compiled-in registry and application source
wrappers are packaging/composition exceptions, not lower-layer domain coordination.
ADV `usbmsc` is a documented platform utility with exclusive raw-media handoff
(`platform/adv/README.md`, “usbmsc utility”), not a portable app bypass.

Repository-wide review included production source/include searches across `apps/`,
`core/`, and both `platform/` trees, build composition, the shell/loaders, all service
owners, utility application API use, and the ADV I2C/GPS/USB/filesystem/GPIO/audio
edges. Detailed behavioral inspection focused on stateful/error paths, rather than
claiming a proof of every driver instruction.

#### MiniFT8

```text
ft8_main -> app_controller create / step_location / step_rx / step_tx /
            build_model / apply_action / destroy
         -> ui_shell (navigation, rendering, AppAction)
         -> ft8_ui_adapter -> public Display/Input
         -> presentation_profile (application resource/presentation choice)

app_controller -> config_service (parse/serialize owned configuration)
               -> storage_service -> public Filesystem (configuration persistence)
               -> auto_seq -> auto_seq_tx_intent (within the same module)
               -> tx_lifecycle (slot/parity admission)
               -> rx_audio_adapter -> public Audio
               -> rx_frontend
               -> rx_slot_framer
               -> ft8_engine
               -> rx_result_builder -> ft8_engine message-codec types/constants
               -> public Memory / Time-Location / Filesystem

ui_shell -> shared ft8/app_types + presentation_profile
ft8_engine -> monitor -> vendor kissfft
           -> decoder -> monitor / LDPC / CRC
           -> message codec / hash store
```

The RX data flow is Audio -> frontend -> framer events -> engine -> result builder
-> retained RxBatch -> AutoSeq/UI. It does **not** represent hidden sibling calls:
`rx_emit_event` is implemented inside `app_controller.c:300-335`, and the controller
supplies that callback to the pure framer. Result-builder dependence on engine
protocol output is deliberate, already allowed by both checkers. UI data/actions
cross the controller facade; UI does not call AutoSeq, storage, or the decoder.
`ft8_main`'s explicit-fixture TX suppression and edge wiring remain the existing
bootstrap/loop contract, with no independent QSO state machine.

Compared with canonical documents, README/development describe the current RX and
AutoSeq graph accurately; `architecture.md` still names removed `qso_scheduler` and
future RX modules (F10). The direct controller Filesystem edge for logs is real and
is explicitly described in README/development, but conflicts with the architecture
file-policy allocation (F06). There is no production QMX/ALSA/pthread API edge in
MiniFT8. `apps/ft8/tools/ft8_decode.c` is a separate host diagnostic executable,
not the runtime `ft8` module; its host file/allocator access is not attributed to
the production application's dependency graph.

#### Keyer

```text
keyer_main -> app_controller init / run / shutdown
app_controller -> config_service -> public Filesystem
               -> keyin -> public Digital I/O
               -> keyer_engine -> keyer_decoder (same engine module)
               -> keyout -> public Digital I/O
               -> sidetone -> public Audio TX
               -> public Time/Location, Input, Console
config_service / keyin / keyout -> shared keyer_types -> engine enum types
```

The shared-to-engine dependency is a documented type reuse in the checker, not
engine calls from configuration or KeyIn/KeyOut. `keyer_engine_step` receives
`now_us` and sampled facts from the controller; it reads no clock/device. KeyOut
and sidetone receive logical key state from the controller and never call each
other. All concrete state lives in controller-owned static objects, with each
edge managing its own public handles. `keyer_main.c` is lifecycle-only.

The actual graph includes K5 sidetone, its configuration, tests, and ADV speaker
provider, although current canonical status says K5 is next (F10). Its synchronous
Audio-driven loop pacing also creates a transport-dependent latency concern (F12).
No production Keyer platform-private include/call was found.

### Ownership table

| State/resource | Current owner | Expected owner | Result |
| --- | --- | --- | --- |
| Foreground app begin/end | `core/app_manager.c`; loader owns native image/task | Runtime brackets one app; backend packages it | Consistent; native unload precedes residual service cleanup, with no retained app callback found |
| Public API/port tables | `services.c` | Portable services, private platform port | Consistent source use; excessive Linux symbol exports F08 |
| Application memory/quota | `memory_service.c` slots/limits; native allocator underneath | Memory service | Consistent; app workspace obtained via API and released before controller destruction |
| Filesystem file/directory tokens | `filesystem_handles.c` | Filesystem service | F03: generation changes per app, not per reopened slot |
| Logical paths, writable exclusion, quota | `filesystem_paths.c`, `filesystem_service.c`, `filesystem_quota.c` | Filesystem service | Correct placement; consumer equality gap F04 |
| Input queue / terminal parser | Input service / Linux byte parser or ADV keyboard | Service owns logical queue; backend owns input mechanics | Consistent private ingress, lifecycle flush, lock hooks |
| UTC/default/live location | Time/Location service; ADV GPS submits facts | Service owns effective time/location | Consistent lock-protected state; producers stopped before service replacement |
| Audio public RX/TX handles | `audio_service.c` | Audio service | Consistent independent state and app-exit stop/abort/close fallback |
| ALSA PCM / capture worker / ring | `linux_audio_wav.c` / `linux_audio_buffered.h` | Private Audio provider | Generic placement and normal cleanup accepted; discontinuity gap F05 |
| Digital I/O public/native handles | `digital_io_service.c` / Linux or ADV provider | Service lifecycle; backend electrical realization | Consistent duplicate-line rejection and teardown; no dit/dah policy below API |
| MiniFT8 RX time phase | Controller supplies initial UTC/explicit timing; framer advances samples | Controller supplies facts; pure framer owns progression | Consistent normal stream; F05 on concealed loss/start backlog |
| DSP workspace, FFT history, hashes | `Ft8Engine` submodules within controller-supplied workspace | Engine owns DSP/hash state; controller owns allocation lifetime | Consistent baseline; no clock/FS/Audio calls |
| RxBatch / selected message | `AppRxState` arrays and generation/index | Controller retains factual batch; UI gets copied presentation | Consistent; batch replacement invalidates selection, no retained UI pointer |
| QSO queue/retry/priority/log eligibility | `AutoSeq` fixed arrays and typed events | Pure AutoSeq | Consistent; no heap/API/platform calls |
| TX slot/parity / last intent | `TxLifecycle` eligibility; controller snapshots/coordinates AutoSeq | Same split | Consistent; `last_intent`/`last_log_event` are snapshots, not a second queue |
| Log serialization/storage | Controller TX implementation | Controller orders events; explicit application file-policy owner | F06/F07; no platform leakage into AutoSeq |
| Station config / runtime GPS grid | `ConfigService`, `manual_grid`, `AutoSeq` applied snapshot | Persisted configuration distinct from runtime location override | F01; applied retry/Skip-TX1 copies otherwise explicitly synchronized |
| Configuration read failure policy | Boolean storage result interpreted by controller | Storage reports result; config owner decides absence vs error | F02 |
| UI navigation/model diagnostics | `UiShell`; controller constructs model | UI owns navigation; controller owns facts | Structurally consistent; polling documentation mismatch F13 |
| Keyer settings/engine state | Controller `s_config` / `s_engine` | Controller injects config/time; pure engine advances CW | Consistent |
| Keyer KeyIn/KeyOut/sidetone handles | Each edge object under controller lifecycle | Edge owns its handle; controller orders siblings | Consistent ordinary/error unwind; F12 pacing assumption |
| ADV raw media / USB MSC | `adv_filesystem_handoff` loans media to USB backend and remounts | Exclusive platform media owner | Consistent documented platform exception; hardware failure recovery not exercised |

### Findings

#### F01 — HIGH — C: Runtime GPS grid can overwrite persistent station identity

- **Evidence:** `apps/ft8/src/app_controller/app_controller_instance.c:90-93, 134-138,
  150-158`; `app_controller.c:199-203, 691-718`;
  `apps/ft8/src/config_service/config_service.c:131-149`.
- **Observed:** The controller preserves `manual_grid`, then writes the live grid
  into `app->config.grid`. Changing band/profile/Skip-TX1/retry calls
  `app_save_config`, which serializes that same configuration including the live
  grid. It never substitutes `manual_grid` before persistence.
- **Expected invariant:** The explicit source invariant says GPS is a session
  override that must never rewrite `station.txt`; persisted and effective station
  state need distinct authority.
- **Why it matters:** An unrelated settings action during a live fix can silently
  replace the operator's configured grid for the next launch. Restoring the manual
  grid in RAM when GPS disappears does not restore the saved file.
- **Recommended ownership/direction:** Keep persisted station identity under the
  config owner and derive/inject effective station facts through the controller.
- **Follow-up boundary:** A configuration/runtime-grid separation task with a live
  fix -> settings save -> restart regression. No file-format migration. This is a
  traced production path, not a hardware reproduction in T001.

#### F02 — HIGH — C/F: A failed station read is treated as permission to replace it

- **Evidence:** `apps/ft8/src/storage_service/storage_service.c:36-75`;
  `apps/ft8/src/app_controller/app_controller.c:375-384`.
- **Observed:** `storage_service_read_text` returns the same `false` for missing,
  unreadable, oversized, read-failed, and close-failed files. The controller takes
  every false result as “not loaded” and atomically saves defaults over the path.
- **Expected invariant:** The file owner must distinguish absence from failure;
  resource errors must not silently authorize replacing existing configuration.
- **Why it matters:** Confirmed with a disposable 2,202-byte `station.txt`: starting
  `ft8` replaced it with 122 bytes of defaults. Atomic rename makes replacement
  complete; it does not make the decision correct.
- **Recommended ownership/direction:** Storage supplies a typed result; controller/
  configuration policy creates defaults only on a positively missing file.
- **Follow-up boundary:** Preserve station files on load errors, with oversized,
  I/O-failed, and genuinely missing-file cases. Keep the existing format/path.

#### F03 — HIGH — C/F: Closed filesystem tokens regain ownership of later resources

- **Evidence:** `core/minishell_services/filesystem_handles.c:7-16, 25-28,
  89-135`; `filesystem_service.c:465-468`; `docs/api/filesystem-api.md:50-59`.
- **Observed:** Reopening the first free slot uses the unchanged app generation.
  Closing clears the slot but does not change token identity. The same issue
  exists for directory slots.
- **Expected invariant:** Closed/stale public handles must not refer to a newly
  opened resource; the service owns token identity across each resource lifetime.
- **Why it matters:** A retained stale token can read/write/close another file or
  directory in the same app invocation. A temporary harness built directly from
  the unchanged handle module returned `65537` before and after both kinds of
  reopen; stale lookup succeeded in each case.
- **Recommended ownership/direction:** Keep generation/token allocation entirely
  in the Filesystem handle owner, advancing identity per acquisition.
- **Follow-up boundary:** File/directory slot-reuse lifecycle fix and regression
  tests, including cross-app cleanup. No application-side handle workaround.

#### F04 — HIGH — C/E: Copy's text equality disagrees with Filesystem path identity

- **Evidence:** `apps/cp/main/cp_copy.c:9-33, 55-61`;
  `core/minishell_services/filesystem_paths.c:15-52`;
  `filesystem_service.c:30-38`.
- **Observed:** `cp` rejects only byte-identical path strings, then opens the source
  read-only and destination with TRUNC. The Filesystem service normalizes `.` and
  repeated separators; its writable exclusion does not reject this read/write pair.
- **Expected invariant:** A copy-to-self guard must agree with the service's logical
  path identity, whose semantics belong to Filesystem.
- **Why it matters:** Confirmed on a temporary file: `cp /flash/a.txt
  /flash/./a.txt` reduced the source from 19 bytes to zero.
- **Recommended ownership/direction:** Have the architect select a portable way to
  establish same-resource identity/prevent destructive alias copying. Keep native
  stat/inode checks and backend-private normalization out of application code.
- **Follow-up boundary:** A narrowly scoped safe-copy identity task with equivalent
  logical-path tests; any public contract change requires separate authorization.

#### F05 — HIGH — C/F: Live capture hides discontinuities from the sample-clock owner

- **Evidence:** `platform/linux/linux_audio_buffered.h:143-157` (full-ring wait);
  `platform/linux/linux_audio_wav.c:381-398` (recover and continue);
  `apps/ft8/src/app_controller/app_controller.c:511-534` (one-time phase);
  `docs/MiniFT8/README.md`, “FT8 slot timing”.
- **Observed:** A full 65,536-frame ring stalls its producer (~5.46 seconds at
  12 kHz). ALSA wait/read errors can be recovered without surfacing a stream gap;
  samples after recovery can be delivered as if contiguous. MiniFT8 sets UTC phase
  once and thereafter trusts sample count. Initial anchoring also uses consumption
  time minus the returned block length, without capture backlog/timestamp facts.
- **Expected invariant:** Provider owns transport facts/loss reporting; application
  owns slot interpretation and re-anchoring. Hidden loss must not silently redefine
  the application's sample clock.
- **Why it matters:** A sufficiently long consumer pause or device overrun can
  invalidate later-slot alignment without a reset event. This is a static failure
  path, not evidence that the tested pc-1 normal decode workload overruns.
- **Recommended ownership/direction:** Define generic discontinuity/error semantics
  below Audio; controller decides how to restart/discard a partial FT8 slot. Do not
  put 15-second slots or FT8 recovery policy into ALSA/ring code. The architect must
  decide whether existing error results suffice before considering API expansion.
- **Follow-up boundary:** Audio continuity/error propagation and fake-provider
  overflow/recovery tests, followed by a separate MiniFT8 restart integration test.

#### F06 — MEDIUM — B/C/G: Logging combines coordination, serialization, and file policy

- **Evidence:** `apps/ft8/src/app_controller/app_controller_tx.c:46-392, 466-490`;
  `apps/ft8/src/storage_service/storage_service.c:77-127`;
  `docs/MiniFT8/architecture.md:323-334, 440` versus README ownership summary and
  development “Logging contract”.
- **Observed:** The controller owns Gregorian conversion, band-frequency tables,
  ADIF/Cabrillo serialization, filenames, append/seek/sync/close, and event ordering.
  Storage owns configuration safe-save helpers but is bypassed for these log files.
- **Expected invariant:** The controller coordinates; a coherent application-owned
  file-policy boundary handles representation/storage. Current docs conflict:
  README/development explicitly assign logging to the controller, while architecture
  assigns FT8 file policy to storage. Therefore this is a responsibility ambiguity,
  not an assertion that recent logging violated every current document.
- **Why it matters:** Adding log formats or persistence recovery changes the same
  implementation that owns TX-start ordering; independently testing persistence is
  unnecessarily tied to controller state.
- **Recommended ownership/direction:** Supervisor/architect should confirm a small
  log serializer/persistence owner, reached only through the controller. Supply it
  an immutable event plus station/time facts; leave eligibility and ACK state in
  AutoSeq and event ordering in the controller. Keep all I/O through MiniShell.
- **Follow-up boundary:** A behavior-preserving logging extraction only, with output
  fixtures and TX-start/ACK order tests. Do not combine with physical TX or a format
  migration, and do not move domain formats into MiniShell services.

#### F07 — MEDIUM — F: Log failure handling lacks a recoverable persistence boundary

- **Evidence:** `app_controller_tx.c:155-172, 299-345, 480-489`.
- **Observed:** Any failed Cabrillo `stat` triggers header creation with TRUNC, not
  just NOT_FOUND. Record insertion overwrites END-OF-LOG before completing the new
  record/marker. A short write/error can leave a tail that subsequent calls reject.
  ADIF append can similarly leave a partial record, or an uncertain write followed
  by a failed sync/close; withholding ACK alone does not undo already-written bytes.
- **Expected invariant:** Persistence owner distinguishes missing files from errors
  and defines what retry means after partial/uncertain writes. Success-only ACK is
  necessary but is not a transaction or an exactly-once guarantee.
- **Why it matters:** Transient stat failure may erase an existing log if a later
  truncate-open succeeds; partial Cabrillo insertion can prevent further logging.
  Retry after uncertain completion may duplicate output. These are traced error
  paths; no storage faults were injected against real media.
- **Recommended ownership/direction:** Put explicit failure/recovery policy in the
  application persistence owner, with independent per-format outcomes passed back
  to the controller. Preserve AutoSeq purity.
- **Follow-up boundary:** Separate persistence-hardening task after F06 ownership is
  agreed; fault-inject stat/write/sync/close and verify existing data/retry behavior.

#### F08 — MEDIUM — E: Linux exports private service/backend implementation symbols

- **Evidence:** `CMakeLists.txt:64` (`ENABLE_EXPORTS ON`);
  `nm -D --defined-only build-linux/minishell`; compare
  `platform/adv/adv_elf_loader.c:19-22` explicit `mini_api_get` export.
- **Observed:** Linux's dynamic symbol table includes
  `filesystem_handles_activate_file`, `filesystem_handles_release_file`, path/quota
  helpers, and other private runtime functions. Current apps use the public API,
  but the linker does not restrict them to it.
- **Expected invariant:** Application-visible service access is through the public
  API; private implementation helpers must not accidentally become import contracts.
- **Why it matters:** A module can link to private state-mutating helpers and bypass
  normal lifecycle/validation without the existing include checks noticing.
- **Recommended ownership/direction:** Runtime build owns an explicit application
  import/export boundary. This is architectural enforcement for trusted apps,
  not a claim that MiniShell is a security sandbox.
- **Follow-up boundary:** Linux symbol visibility/export audit and imported-symbol
  regression; preserve required toolchain/runtime symbols and `mini_api_get`.

#### F09 — MEDIUM — H: Passing architecture tests leave significant blind spots

- **Evidence:** `tests/app_dependency_boundary.py:10, 153-218`;
  `tests/ft8_platform_boundary.py:15-45, 104-126, 153-195`;
  `.github/workflows/linux.yml`; root `CMakeLists.txt` test registrations.
- **Observed:** Dependency checking resolves only quoted app-local headers; unresolved
  external headers are ignored and angle-bracket app-local dependencies escape that
  checker. FT8 checking recognizes more include syntax but omits ALSA and `sys/`
  prefixes and many direct platform calls; ambiguous basenames are dropped from
  ownership resolution. Only FT8 has a platform/purity scan. Neither checker derives
  actual calls, callbacks, symbol imports, state writes, or transitive purity.
- **Expected invariant:** Keyer, all portable apps/core, pure-domain API exclusion,
  and private runtime boundaries need enforceable coverage, not just FT8 include
  allowlists. The documented invariants are broader than the tools' actual claims.
- **Why it matters:** All direct architecture checks pass on this baseline despite
  the reported ownership defects. `app_dependency_boundary` runs in CI but is not
  part of root CTest; a local CTest-only run omits both application graph checks.
- **Recommended ownership/direction:** One maintained per-layer/module rule model,
  tested with negative fixtures for headers, direct calls/imports, pure-module API
  leakage, and unowned modules. Keep explicit host-tool/vendor exceptions narrow.
- **Follow-up boundary:** Test-only architecture enforcement task; do not treat a
  broader regex as a proof of lifecycle/policy correctness.

#### F10 — MEDIUM — G: Canonical architecture no longer represents implemented ownership

- **Evidence:** `docs/architecture/architecture.md:159-170, 206`;
  `design-principles.md:167-178`; `resident-vs-app.md:113-124`;
  `docs/MiniFT8/architecture.md:95-120, 394-430`;
  `docs/keyer/README.md:3, 277-301`.
- **Observed:** Canonical architecture still advertises `/flash/<app>.elf` and
  `/sd/<app>.elf` rather than the implemented `/flash/apps` and `/sd/apps`; it calls
  Digital I/O future. MiniFT8 architecture names removed `qso_scheduler` and future/
  paused RX. Keyer docs omit the already wired sidetone and speaker path
  (`app_controller.c:124-137, 184-193`, ADV backend `:124-126`, ELF CMake source list).
- **Expected invariant:** Current architectural dependency/resource maps must agree
  with code and public API. Implementation presence and hardware acceptance status
  must be described separately.
- **Why it matters:** These are designated canonical documents, not only historical
  stage prose. They can send implementation into the wrong owner or deployment path.
- **Recommended ownership/direction:** Supervisor updates canonical maps/status after
  accepting audit evidence; retain the higher-priority API and current runtime paths.
- **Follow-up boundary:** Documentation-only reconciliation, including F06 ownership
  resolution; do not infer K5 hardware completion merely from code/tests.

#### F11 — MEDIUM — G/H: CI's behavioral reference pin differs from the mandated reference

- **Evidence:** `.github/workflows/ft8-reference.yml:79-83` checks out
  `5bd3ef98f72388a850bebad04bd7300b90edb63c`; AGENTS.md and the current MiniFT8
  README/development pin `491e757ae6b1e4cfd2b9a6ba10f48b35643849e0`.
- **Observed:** The reference workflow uses a different V2 tree for golden artifacts.
- **Expected invariant:** Behavioral verification should identify and use the approved
  reference, or explicitly document why an older artifact pin remains valid.
- **Why it matters:** A green reference job alone does not establish equivalence to
  the currently mandated V2 revision. No claim is made here that the WAV bytes or
  algorithms actually differ; those revisions were not compared in T001.
- **Recommended ownership/direction:** Supervisor owns the reference decision; CI
  should encode it and distinguish golden-audio provenance from behavior provenance.
- **Follow-up boundary:** Reference-pin/artifact comparison and narrowly scoped CI/doc
  alignment, not an algorithm port or broad rebaseline.

#### F12 — MEDIUM — C/F: Keyer loop latency depends on unbounded speaker-write behavior

- **Evidence:** `apps/keyer/src/app_controller/app_controller.c:158-193`;
  `apps/keyer/src/sidetone/sidetone.c:34-61`;
  `platform/adv/adv_audio_speaker.cpp:195-214` (`speaker_write`).
- **Observed:** The controller performs synchronous sidetone writes between successive
  input/engine steps and omits the 1 ms sleep when streaming. The ADV write callback
  explicitly discards `timeout_ms` before calling the codec driver. Thus the supplied
  write timeout is not enforced by this provider, and sample block duration is not
  itself a bound on foreground blocking.
- **Expected invariant:** CW timing stays in the engine/controller; transport must
  expose a clear bounded blocking/error contract so optional sidetone cannot silently
  determine responsiveness. The Audio docs currently do not fully specify timeout
  semantics, so this is an ambiguity rather than a measured missed-key claim.
- **Why it matters:** A slow/stalled Audio write delays paddle sampling, key release,
  and exit handling. Host sidetone mocks do not establish real driver latency.
- **Recommended ownership/direction:** Architect should set the permitted latency
  contract; provider owns transport timeout realization, controller owns pacing.
- **Follow-up boundary:** Document/test the Audio TX blocking contract and measure ADV
  behavior before choosing any scheduling change. No speculative worker redesign.

#### F13 — LOW — G: Memory diagnostic polling documentation predates the complete model

- **Evidence:** `docs/MiniFT8/ui.md:240` versus
  `apps/ft8/src/app_controller/app_controller_instance.c:164-173`,
  `app_controller.c:613-645`, and `ft8_main.c:192-199`.
- **Observed:** Every complete model build queries Memory, irrespective of the visible
  screen; UI documentation says only V/Memory polls it.
- **Expected invariant:** Current UI/performance documentation must describe the
  controller snapshot contract without returning screen policy to lifecycle code.
- **Why it matters:** A stated polling guarantee is false, though no functional
  problem or material measured overhead was established.
- **Recommended ownership/direction:** Keep the C2 controller/UI boundary; reconcile
  documentation or use an explicitly approved controller-owned diagnostics policy.
- **Follow-up boundary:** Small documentation clarification unless measurement
  justifies a separate behavior change.

### Reviewed and accepted structures

- **Linux ALSA buffering placement:** `linux_services.c:16-17` wraps the underlying
  provider below the public Audio service. Native 48 kHz/S24_3LE -> 12 kHz/S16
  conversion preserves channel order; MiniFT8's frontend owns mono/downmix/6 kHz
  conversion. No FT8 slot count, QSO, parity, or QMX CAT policy is implemented in
  the ring worker. Fixed supported formats are provider capabilities, not channel
  semantics. FT8 motivation in a comment is not contamination.
- **Worker ownership and normal unwind:** One producer owns `write_count`, one
  consumer owns `read_count`, with acquire/release publication. Stop sets the atomic
  flag, joins the worker, then stops PCM; close releases the underlying stream.
  Thread-creation failure stops the newly started stream. Public app teardown closes
  streams before freeing app memory. ALSA/pthread library handles are cached for the
  process; they are not per-app leaked stream handles. Reconfiguration/reinitializing
  these cached provider structures was not stress-tested. F05 qualifies continuity,
  not the normal ownership split.
- **AutoSeq/logging event boundary:** Pure AutoSeq produces typed eligibility and owns
  per-QSO ACK flags (`auto_seq.c:705-751`). Controller captures intent/eligibility,
  writes each eligible format, ACKs its success separately, then ticks AutoSeq
  (`app_controller_tx.c:466-492`). No Filesystem or clock entered AutoSeq. F06/F07
  concern placement and error recovery, not that eligibility/ordering split.
- **Pure domains:** RxFrontend, RxSlotFramer, RxResultBuilder, TxLifecycle, AutoSeq,
  and Keyer engine take explicit inputs and own no device/clock. Engine workspace
  and hash state are explicit. Baseline FFT plans use caller-provided storage;
  vendor allocator branches and inactive `sys/types.h`/`alloca` options are not
  evidence of production platform access. Generalized non-baseline FFT allocation
  behavior is not proven by the AutoSeq-only heap check.
- **Application lifetime/data:** MiniFT8 owns RX allocation flags and one reverse
  cleanup path; framer callbacks are synchronous and controller-owned. RxBatch
  selection uses generation/index and the UI receives copied text, not pointers
  into decoder output. Configuration values copied into AutoSeq are deliberate
  controller-applied runtime settings, not uncontrolled sibling reads (except F01's
  persistent/effective-grid mix).
- **Keyer edges:** Init unwinds already-open KeyIn/KeyOut on later failure; ordinary
  run errors release KeyOut and main calls shutdown. KeyOut opens released/high-Z,
  restores release on shutdown, and leaves CW electrical meaning above Digital I/O.
  Sidetone owns synthesis and Audio handles, never calls KeyOut/engine, and gracefully
  permits unavailable Audio. Service cleanup is a fallback after edge cleanup, not
  a second domain-policy owner.
- **Resident/portable services:** Memory accounting, normalized paths/quota, opaque
  stream/line validation, Time/Location anchors, and Input queue semantics remain
  portable. Linux native file/terminal/clock/ALSA and ADV ESP-IDF/FATFS/GPIO/codec
  mechanisms remain in platform files. ADV GPS/input ingress supplies generic facts.
  The registered `usbmsc` function and compiled-in app entries are documented
  composition/platform exceptions, not reasons to permit private calls in apps.
- **Other applications:** Utility APIs remain public; `nano`'s buffer uses injected
  allocator callbacks while its file/UI modules use MiniShell edges. `cp` owns copy
  policy and closes acquired handles on errors, with the alias defect recorded in
  F04. Standard C string/format functions are not classified as POSIX access.

### Architecture-test coverage gaps

Existing direct checks passed, with exact output below. They enforce useful local
include rules and the explicit AutoSeq no-heap rule; they do not establish the
entire architecture claim in README. Follow-up enforcement should cover:

1. All production apps and portable services for private headers, native calls, and
   imported runtime symbols, with host tools/platform wrappers explicitly scoped.
2. Both include syntaxes, relative/transitive includes, ambiguous headers, direct
   extern declarations/callback coupling, and a single rule source for both checkers.
3. Keyer engine purity, MiniFT8 DSP/helper purity, and baseline allocator behavior,
   beyond the current AutoSeq textual heap scan.
4. Handle generation on close/reopen; partial init, stop/close failure, app teardown,
   and provider reconfiguration. Existing successful cleanup tests do not cover all
   error contracts.
5. Config absence vs read error; live-grid save/restart; equivalent copy paths;
   log partial-write/sync/stat failure and independent ACK behavior.
6. Buffered Audio delayed consumers, full ring, XRUN/recovery, stop while full/read
   pending, format rejection, and first-read phase. WAV replay bypasses the live ring.
7. Keyer transport latency/timeout, output release on provider failures, and optional
   Audio capability. Host mocks do not measure ADV pacing.
8. Correct pinned-reference provenance and complete local test discovery. Root CTest
   does not include the separate service/Keyer suite or CI's app graph/AS-8 commands.

### Recommended cleanup sequence

Each item is a proposed task boundary for architect/supervisor acceptance, not work
implemented or authorized by T001.

1. **Confirm ownership/contracts:** Accept/reject findings; resolve logging owner
   (F06), copy identity mechanism (F04), and Audio continuity/timeout contract
   (F05/F12). Do not expand the public API by inference.
2. **Protect persistent configuration:** Separate runtime grid (F01) and distinguish
   load failures (F02), as two small changes with independent regressions.
3. **Protect filesystem lifetimes and copying:** Repair token reuse (F03); follow with
   the approved alias-safe copy solution (F04). Keep application/backend ownership.
4. **Make Audio failure facts explicit:** Generic Linux continuity/error propagation
   with mock provider tests (F05); then a separate MiniFT8 phase-reset integration.
   Evaluate ADV TX latency separately (F12), with hardware measurement when authorized.
5. **Isolate logging, then harden it:** Behavior-preserving extraction (F06), followed
   by an independent failure-recovery change (F07); retain TX-start event ordering,
   independent ACKs, and existing output formats unless separately approved.
6. **Enforce imports and layers:** Narrow Linux exports (F08) and broaden architecture
   tests (F09) in distinct reviewable build/test tasks.
7. **Align evidence and canonical docs:** Resolve reference provenance (F11), update
   actual graphs/paths/implemented-vs-validated status (F10), and clarify memory
   polling (F13). Supervisor owns accepted canonical updates.
8. **Repair baseline expectations separately:** Review `linux_audio` output matching
   and the known `linux_ft8` queue assertion in a test-only follow-up; do not weaken
   behavior checks simply to obtain green CI.

### Commands / tests / searches performed

Initial `git status --short` was empty. Fetched `origin`, created
`codex/T001-architecture-boundary-audit` from `origin/main`, and verified the audited
HEAD above. Git metadata writes required sandbox escalation; no branch/source
changes from other tasks were present. All tests below ran before editing this
report, so their failures are pre-existing on the audited main revision.

Required baseline (existing `build-linux` directory; no production/test edits):

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
```

Configure/build **PASS**. CTest **18/20 PASS**, exit 8, about 3.94 seconds:

- `linux_audio` fails its output substring assertion: expected
  `audio_probe: PASS frames=180140 hash=f05f17c990b748e1`, while the current probe
  inserts `rate=0.000Hz peak=5439/5436 mean_abs=1173/1173 unequal_lr=155431` between
  frame count and the same hash. Both replay outputs have the expected frames/hash.
- `linux_ft8` times out at `tests/linux_ft8.py:207`, waiting for
  `N5CH     RPLY 0/3` after rotation. Actual first page ends with KQ4PUG after
  AG6X/W7RPS/AE7KJ/N7REB/WN0KS. This is the queue-order expectation called out in
  the task's non-goals. No assertion or behavior was changed.
- Remaining 18 tests, including `ft8_platform_boundary`, pass. Test failures are
  baseline test observations, not caused by this documentation-only audit.

Direct architecture commands (inspected invocation: app checker requires an app
argument, unlike the shorthand in the task):

```sh
python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer
python3 tests/app_dependency_boundary.py --self-test
python3 tests/ft8_platform_boundary.py .
```

```text
app_dependency_boundary: PASS (ft8)
app_dependency_boundary: PASS (keyer)
app_dependency_boundary self-test: PASS
ft8_platform_boundary: PASS (52 source/header files; platform-clean; dependency-clean; AutoSeq heap-free)
```

Additional repository-defined host suites to cover resident services and Keyer:

```sh
cmake -S tests/unit -B /tmp/T001-build-unit
cmake --build /tmp/T001-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T001-build-unit --output-on-failure
cc -std=c11 -Wall -Wextra -Werror -Wpedantic   -Iapps/ft8/src/auto_seq tests/ft8_auto_seq_as8_test.c   apps/ft8/src/auto_seq/auto_seq.c -o /tmp/T001-ft8-auto-seq-as8
/tmp/T001-ft8-auto-seq-as8
cmake -S tests/adv_registry -B /tmp/T001-build-adv-registry
cmake --build /tmp/T001-build-adv-registry -j"$(nproc)"
ctest --test-dir /tmp/T001-build-adv-registry --output-on-failure
```

Results: **14/14** service/utility/Keyer tests pass (including K5); AS-8 **PASS**,
`sizeof(QsoContext)=56`, `sizeof(AutoSeq)=1752`; ADV registry suite **2/2 PASS**
(registry and AS-8). These are host tests, not an ADV firmware/hardware run.

Static evidence commands included `rg --files`, `rg -n '#include|...'` over apps,
core and both platforms, searches for `alsa/`, `snd_`, `pthread`, `unistd`, `fcntl`,
`sys/`, `linux/`, `esp_`, `freertos`, `driver/`, M5, FATFS, GPIO, USB, native
file/clock calls and allocation calls; source reads with `cat`, `sed`, `nl -ba`;
review of root/unit/ADV CMake and `.github/workflows`; and
`nm -D --defined-only build-linux/minishell`. Matches were inspected in context,
including host-tool, vendor/preprocessor, and legitimate public API call matches.

Two disposable probes supplied additional evidence without modifying repository
tests or production source:

- Compiled the unchanged `filesystem_handles.c` to
  `/tmp/T001-filesystem-handles.so` using
  `cc -std=c11 -shared -fPIC -Iinclude -Icore/minishell_services
  core/minishell_services/filesystem_handles.c -o /tmp/T001-filesystem-handles.so`.
  Python ctypes called reset -> activate slot 0 -> lookup/release -> activate slot 0
  with a different native handle, for both file and directory. Both produced
  `65537 65537 stale lookup accepted: True` (F03).
- A Python subprocess ran `build-linux/minishell` with `MINISHELL_ROOT` set to a
  newly created `/tmp/T001-runtime-*` tree and `MINISHELL_APP_DIR` pointing at the
  built runtime apps. It created only an audit `a.txt` and oversized `station.txt`,
  then supplied `cp /flash/a.txt /flash/./a.txt` and `ft8`. File inspection confirmed
  zero-byte copy output and default station replacement (F02/F04). The noninteractive
  FT8 loop did not exit from piped shell input; the probe was interrupted after the
  mutations and results inspected separately. This is not a claimed UI/clean-exit
  test. No user files or repository fixtures were touched.

### Areas not fully verified

- No hardware/manual validation is required by T001, and none was performed. Real
  QMX/ALSA overrun behavior, ADV codec timing/electrical release, RTC/GPS concurrency,
  SD/flash/USB remount failure recovery, and external ELF execution remain outside
  this host audit. Existing hardware-success statements were treated as prior
  project evidence, not revalidated results.
- No ESP-IDF firmware build or external Keyer ELF import inspection was performed.
  The source export table and CI import guard were inspected; host registry tests
  do not prove the actual loader/toolchain/hardware behavior.
- The pinned V2 tree and optional golden WAV reference suite were not fetched/run.
  This audit makes no new V2 algorithm/output equivalence claim; it identifies the
  CI pin discrepancy and uses V3 code for ownership evidence. Optional reference
  WAV CMake settings were empty in the baseline build.
- No sanitizer/concurrency stress or full storage fault-injection campaign was run.
  Static findings explicitly identify conditional failure paths. Lack of an
  observed leak on a successful path is not a proof of all backend close failures.
- No fixes, new committed tests, canonical doc edits, or architectural decisions
  were made. Existing API/format/UI/domain behavior is preserved in this branch.

### Commit / PR

Implementation summary: audit report only; Status set to REVIEW and acceptance
checklist completed. Files changed: only
`docs/project/codex/T001-architecture-boundary-audit.md`.

Behavior/invariants preserved: all production code, tests, public API, persisted
formats, platform mechanics, UI, and ownership implementations are unchanged.
Tests/results, remaining risks, and hardware limitations are recorded above. No
implementation deviation from the audit-only task.

Branch: `codex/T001-architecture-boundary-audit`. The audit commit and PR are
identified in the PR body and final handoff; no self-referential commit hash is
embedded in this report.

## Supervisor review

Supervisor fills this only after reading the actual Codex report and independently checking material findings against the repository.

## Architect decision

Architect records which findings are accepted, rejected, or deferred and authorizes follow-up tasks.
