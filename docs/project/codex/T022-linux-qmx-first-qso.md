# T022 — Linux QMX integrated FT8 TX and first QSO

Status: TESTING

## Architect intent

This is the first end-to-end physical MiniFT8-V3 transmit task.

The target is a real Linux/pc-1 + QMX FT8 exchange using the already-accepted
layers:

```text
T018  Linux live QMX RX defaults
T019  MiniShell Serial/CDC + QMX CAT receive sync
T020  QMX CAT TX primitives
T021  pure FT8 TX encoder + immutable 79-tone plan
```

T020 standalone RF validation was intentionally deferred to this task.

RxTxLog is mandatory for the complete integrated test. We need the daily RT trace
to diagnose the first QSO if anything is wrong.

## Objective

Replace the AS-7 instantaneous TX simulation with a real physical QMX CAT TX path
when a QMX CAT session is open:

```text
UTC slot boundary
    -> AutoSeqTxIntent
    -> T021 Ft8TxPlan
    -> T020 radio_control_begin_tx()
    -> TA tone updates, slot-anchored every 160 ms
    -> radio_control_end_tx()
    -> clean RX restart/re-anchor
    -> AutoSeq tick only after successful physical completion
```

When no CAT session is open, keep the existing simulated TX behavior for host/
regression use.

Also port the smallest V2-compatible RxTxLog slice needed for QSO debugging.

## Accepted baseline

```text
main 78c2ac50055cef87d05e946b8d87009787e89102

Linux bare FT8:
M$> ft8

Linux FT8 with QMX CAT:
M$> ft8 --cat serial:<QMX-CDC-path>
```

T021 plan:

```text
79 tones
160 ms/symbol
6.25 Hz spacing
tone index 0..7
```

T020 QMX primitives:

```text
begin TX   MD6; TX;
tone       TA%04d.%02d;
end TX     RX;
```

## Source of truth

Read before editing:

```text
AGENTS.md
docs/MiniFT8/architecture.md
docs/MiniFT8/development.md
docs/project/progress.md

apps/ft8/main/ft8_main.c

apps/ft8/src/app_controller/
apps/ft8/src/auto_seq/
apps/ft8/src/tx_lifecycle/
apps/ft8/src/tx_encoder/
apps/ft8/src/radio_control/
apps/ft8/src/rx_audio_adapter/
apps/ft8/src/rx_frontend/
apps/ft8/src/rx_slot_framer/
apps/ft8/src/log_service/
apps/ft8/src/config_service/

platform/linux/linux_audio_buffered.h
platform/linux/linux_audio_wav.c

docs/project/codex/T020-linux-qmx-cat-tx.md
docs/project/codex/T021-ft8-tx-encoder.md
```

Pinned V2 timing/logging reference:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0

main/main.cpp
tests/tx_e2e/tx_state_machine.cpp
tests/tx_e2e/test_l3_tx_poll_timing.cpp
tests/tx_e2e/test_ta_format.cpp
```

## Architecture

Keep ownership:

```text
AutoSeq
    semantic QSO policy only

tx_lifecycle
    slot/parity eligibility only

tx_encoder
    pure semantic intent -> immutable 79-tone plan

app_controller
    coordinates physical TX lifecycle

radio_control / radio_qmx
    QMX CAT semantics

MiniShell Serial
    raw CDC transport/lifecycle

MiniShell Audio RX
    RX stream lifecycle

log_service
    RT / ADIF / Cabrillo persistence policy
```

Do not move CAT strings into app_controller, MiniShell, or the scheduler.

## Part A — real TX executor

Add a small MiniFT8 TX-execution state owned by app_controller, either directly in
`AppTxState` or in a private `tx_executor` module.

It must hold enough state to execute one immutable `Ft8TxPlan`:

```text
active
plan
slot_id
slot_start_monotonic_us
next_tone_index
last_tone_index / last frequency for TA dedupe
RX-paused state
physical/simulated completion state
```

No heap allocation for the executor.

### Real-vs-simulated selection

If `app->radio.stream` is not open:

- preserve current simulated AS-7 completion behavior;
- do not call CAT;
- do not change deterministic no-radio tests unnecessarily.

If `app->radio.stream` is open:

- use the physical T020/T021 path;
- do not advance AutoSeq until physical TX completes successfully.

No radio backend identity switch is needed. The existence of the open control
session selects the physical CAT path.

## Part B — slot-anchored scheduling

Preserve the V2 timing rule:

```text
tone_target_time =
    slot_start_monotonic_us + tone_index * 160000 us
```

Never schedule the next tone relative to the previous CAT write completion.

### Boundary anchor

When T022 observes a valid TX slot:

1. obtain UTC slot position and monotonic time in the same controller step;
2. derive:
   ```text
   slot_start_monotonic_us =
       monotonic_now_us - ms_into_slot * 1000
   ```
3. calculate:
   ```text
   first_tone = ms_into_slot / 160
   ```
4. if `first_tone >= 79`, do not transmit;
5. otherwise start at that tone, preserving the V2 late-entry/skip behavior.

The existing T021 plan itself remains unchanged/immutable.

### Polling

Real TX must be non-blocking across the whole 12.64-second transmission.

Do **not** sleep synchronously for the entire message inside one controller call.

`app_controller_step_tx()` should advance the active executor incrementally.

While physical TX is active, `ft8_main` must poll frequently enough for the
160 ms schedule. A 5 ms application/input poll target is appropriate for Linux
T022.

Expose a simple `app_controller_tx_active()` or equivalent if needed so the main
loop can select:

```text
TX active:   input wait <= 5 ms
RX active:   existing nonblocking behavior
idle:        existing behavior
```

### Tone advancement

At each step:

- read monotonic time;
- if the next tone target has not arrived, do nothing;
- when due, send the plan's tone frequency through
  `radio_control_set_tone_hz()`;
- update the next absolute target;
- identical consecutive tone frequencies may be deduplicated, matching V2's
  effective behavior.

Do not intentionally send multiple overdue tones back-to-back merely to "catch up"
after a large scheduling stall.

If current time has advanced beyond one or more complete symbol intervals, advance
to the tone appropriate for the current absolute slot position and send that tone,
rather than compressing missed 160 ms symbols into a burst.

This late-step behavior must be deterministic and tested.

### End time

End TX at or after:

```text
slot_start_monotonic_us + 79 * 160000 us
= 12.640 seconds after slot start
```

Then send `RX;`.

No tone is held intentionally to the 15-second boundary.

## Part C — RX pause and clean restart

This is a hard requirement.

Linux live RX has a parallel ALSA capture worker and a large canonical ring.
During a 12.64-second transmission, that audio is not valid receive content.

Before keying QMX:

1. if live RX is active, call `rx_audio_adapter_stop()`;
2. mark RX paused-for-TX;
3. reset `rx_frontend` decimation state;
4. mark timing re-anchor pending.

If RX stop fails:

- do not key the radio;
- leave AutoSeq state unchanged;
- report failure safely.

After successful `RX;` at TX end:

1. restart the same RX stream with `rx_audio_adapter_start()`;
2. ensure the Linux buffered provider ring is reset by stop/start;
3. reset frontend continuity;
4. set `timing_pending=true`;
5. let the first new receive samples re-anchor the framer from current UTC using
   the existing timing path.

Do not feed TX-period or pre-stop buffered samples into the decoder.

While paused-for-TX, `app_controller_rx_active()` should report false for
polling/UI purposes even though the underlying RX object remains allocated/open.

If RX restart fails, the physical TX is complete but the application must report
an error rather than pretending receive recovery succeeded.

## Part D — TX failure semantics

Any physical TX failure must fail safe.

### Before TX is keyed

Failures in:

- plan encoding;
- RxTxLog TX record;
- RX pause;
- `radio_control_begin_tx()`;

must not advance AutoSeq.

### After TX may be keyed

Failures in:

- initial TA;
- any later TA;
- scheduler state;
- explicit end;

must immediately make a best-effort `radio_control_end_tx()` / `RX;` attempt.

Then restore/restart RX if it had been paused.

Do not call `auto_seq_tick()` for a failed physical TX.

Leave the semantic AutoSeq intent available for a later retry according to existing
queue policy.

`app_controller_shutdown()` / normal app quit must also terminate an active TX
through the existing T020 fail-safe radio close path and clean Audio RX resources.

## Part E — AutoSeq completion and QSO logging

Current AS-7 immediately calls `auto_seq_tick()` at the slot boundary.

For physical TX:

- snapshot the AutoSeq intent at start;
- encode once to `Ft8TxPlan`;
- retain the plan unchanged for the whole transmission;
- only after all 79 symbol times have elapsed, `RX;` succeeds, and RX restart
  succeeds:
  - complete any eligible ADIF/Cabrillo persistence;
  - call `auto_seq_tick()`;
  - mark model changed.

A failed physical TX must not consume a retry as though it was successfully sent.

For the no-CAT simulated path, preserve existing AS-7 behavior unless a small
shared helper can unify it without changing test semantics.

## Part F — RxTxLog

RxTxLog is mandatory debug instrumentation for T022.

### Configuration

Add:

```text
rxtx_log=1
```

to V3 station configuration.

Requirements:

- `ConfigService` gains a boolean RxTxLog field;
- default is **ON**, matching pinned V2's `g_rxtx_log = true`;
- missing key in an existing station file therefore means ON;
- parse `rxtx_log=0|1`;
- serialize it;
- no UI toggle is required in T022.

The first-QSO hardware test must run with RxTxLog ON.

### Filename

Use the station/config directory and V2 daily name:

```text
/flash/ft8/RT[YYMMDD].txt
```

Example:

```text
/flash/ft8/RT260918.txt
```

Derive UTC/date only through MiniShell Time/Location.

### Line format

Preserve V2-compatible semantics:

```text
T [YYYYMMDD HHMMSS][14.074] W1ABC K9XYZ -12 1500
R [YYYYMMDD HHMMSS][14.074] K9XYZ W1ABC R-08 -12 1500
```

Exact format:

```text
T [YYYYMMDD HHMMSS][freq_MHz] <canonical_text> <offset_hz>\n
R [YYYYMMDD HHMMSS][freq_MHz] <canonical_text> <snr_db> <offset_hz>\n
```

Frequency is the canonical dial frequency from the selected band, shown to three
decimal MHz digits like V2.

### Persistence

Implement through `log_service` and MiniShell Filesystem only.

A direct append path is appropriate:

```text
open WRITE|CREATE|APPEND
write complete line
sync
close
```

Do not use native stdio/POSIX.

TX/RX log persistence failures must be observable to the controller.

When RxTxLog is ON, a failure to write the **TX** line must abort that physical TX
before keying QMX. This guarantees the first-QSO RF test is not intentionally run
without its required trace.

An RX log failure should be reported but must not corrupt the decoder/AutoSeq
state. Decide whether it is application-fatal only if needed for consistency;
record the chosen behavior.

### RX logging point

For every newly finalized unique decoded `RxMessage` in a batch, write one R
record before/alongside AutoSeq projection.

Use:

- `message.canonical_text`;
- `message.snr_db`;
- `message.offset_hz`;
- current selected band.

Do not log only addressed messages. We need the surrounding band activity for QSO
debugging, matching V2's useful RT trace.

Avoid duplicate logging of the same retained batch on repeated UI/controller
steps. One finalized batch generation is logged once.

### TX logging point

Write one T record for every physical FT8 plan attempt just before keying the
radio, using:

- `plan.canonical_text`;
- `plan.base_hz`;
- current selected band.

No per-symbol TA records are required in RT files.

## Part G — previous-slot decode freshness

Do not transmit a stale retry if the previous receive slot has not yet been
finalized/applied.

Track the most recent RX batch slot whose messages have been projected into
AutoSeq.

At a candidate TX slot:

```text
required RX slot = tx_slot_id - 1
```

If live RX is active and the required previous slot is not yet applied:

- defer physical start within the existing early-slot window rather than firing
  stale state;
- once decode is applied, start late using the absolute `first_tone` skip rule;
- if the start window expires, skip that TX opportunity and preserve AutoSeq state.

Do not create a catch-up transmission in a later wrong-parity slot.

The no-live-RX deterministic/test path may retain existing behavior.

## Part H — station identity from station.txt

The first-QSO identity must come from the normal MiniFT8 configuration file:

```text
/flash/ft8/station.txt
```

For the architect's first-QSO setup it must contain:

```text
callsign=AG6AQ
grid=CM97
```

Requirements:

- `ft8` must load these values through the existing `ConfigService` / storage path;
- do not hardcode `AG6AQ` or `CM97` in production code;
- the loaded callsign/grid must be applied to AutoSeq before any CQ/reply intent is produced;
- T021 TX encoding must therefore generate text using the loaded station identity,
  e.g. `CQ AG6AQ CM97` for an ordinary CQ;
- ADIF/RxTxLog station facts must use the same loaded callsign and selected grid;
- if a valid live GPS grid later overrides the runtime effective grid, preserve the
  existing V3 live-location policy, but the persistent/manual base identity still
  comes from `station.txt`;
- malformed/unreadable station configuration must fail safely rather than transmit
  with an empty or compiled-in identity.

Add deterministic coverage that loads a station fixture containing:

```text
callsign=AG6AQ
grid=CM97
```

and proves the values propagate through `AppController -> AutoSeqTxIntent ->
Ft8TxPlan` without substitution.

Hardware acceptance must explicitly verify the active station configuration before
the first RF transmission:

```text
callsign  AG6AQ
grid      CM97
```

## Part H — operator behavior

No new physical-radio selection UI is required.

For Linux first-QSO testing:

```text
M$> ft8 --cat serial:<QMX-CDC-path>
```

RX remains the T018 default:

```text
alsa:hw:2,0
```

Use the existing UI/AutoSeq controls to select a CQ or reply and TX parity.

No CAT endpoint default/discovery is introduced in T022.

## Tests

### 1. Scheduler timing unit

Use fake monotonic time + mocked radio.

Prove:

- slot start anchor is absolute;
- late entry skips `floor(ms_into_slot / 160)` tones;
- first selected tone is sent immediately after begin;
- subsequent tone targets are based on slot start, not previous write time;
- 1/5/10/20 ms poll intervals produce bounded positive lateness and no cumulative
  drift;
- a large step over multiple symbols skips to the current symbol rather than
  bursting stale TA commands;
- TX ends at 79*160 ms anchor;
- consecutive identical tones may be deduplicated without changing the plan.

### 2. Exact CAT stream integration

With mocked Serial/PTTY and one fixed T021 plan, prove:

```text
T019 sync
MD6;TX;
TA....
...
RX;
```

Use real `radio_control` and real `tx_encoder`.

Do not hardcode a duplicate CAT formatter in the test.

### 3. Failure-state tests

Inject failures at:

- RxTxLog TX append;
- RX stop;
- begin TX;
- initial TA;
- middle TA;
- RX/end;
- RX restart.

Prove:

- best-effort RX restoration;
- Audio restart when applicable;
- AutoSeq does not advance on failed physical TX;
- Serial/radio remains cleanup-safe;
- no false successful-completion counter/state.

### 4. AutoSeq completion test

Physical successful TX:

- exact intent snapshot encoded;
- only one plan used through TX;
- QSO/AutoSeq tick happens only after successful physical completion;
- log/event ACK behavior remains correct;
- retry counters/state progress exactly once.

No-CAT mode:

- existing simulated AS-7 behavior remains deterministic.

### 5. RxTxLog unit/integration

Prove:

- default ON;
- parse/serialize 0/1;
- exact filename by UTC date;
- exact T/R lines;
- 20m/40m frequencies;
- append preserves earlier lines;
- sync+close happen;
- RX batch logged only once;
- all unique decoded messages are logged, not just addressed ones;
- disabled setting emits no RT writes;
- TX write failure blocks physical keying.

### 6. RX pause/restart

With a fake Audio service prove:

- live RX stop occurs before CAT TX;
- no RX read during TX;
- start occurs after RX CAT restoration;
- frontend/framer timing state is marked for re-anchor;
- first post-TX samples use current UTC timing rather than pre-TX sample position.

Linux provider regression must prove ALSA buffered stop/start resets the ring.

### 7. Architecture

Keep/pass:

```text
app_dependency_boundary
app_platform_boundary
ft8_platform_boundary
serial_protocol_boundary
architecture_rules
```

Add mutation coverage for any new executor/logging boundary if appropriate.

## Non-goals

Do not implement:

- Linux Audio TX/UAC OUT;
- FT4;
- ADV physical CAT TX;
- nonstandard/hashed-call TX;
- CAT response parsing;
- CAT device discovery/default endpoint;
- new radio-selection abstraction;
- per-symbol RT logging;
- UI redesign;
- a new worker thread unless deterministic evidence shows the 5 ms step model
  cannot meet FT8 timing on Linux;
- a successful-QSO claim from automated tests alone.

## Software acceptance criteria

- [x] T021 plan is used unchanged for physical TX;
- [x] real CAT TX occurs only with open radio control;
- [x] simulated no-CAT path remains;
- [x] first/late tone selection is slot-anchored;
- [x] ongoing tone deadlines are absolute;
- [x] overdue multi-symbol gaps skip rather than burst;
- [x] TX end occurs at 12.64 s plan end;
- [x] AutoSeq advances only after successful physical completion;
- [x] failed physical TX does not consume semantic completion/retry;
- [x] RX is stopped before keying and restarted/re-anchored after RX;
- [x] no TX-period buffered audio is decoded afterward;
- [x] previous RX slot freshness guard is implemented;
- [x] station identity is loaded from `/flash/ft8/station.txt`, not hardcoded;
- [x] `callsign=AG6AQ` and `grid=CM97` propagate through AutoSeq into the first-QSO TX plan;
- [x] RxTxLog defaults ON;
- [x] RT filename and R/T format match V2 semantics;
- [x] all finalized RX messages are RT-logged exactly once;
- [x] each physical TX attempt is RT-logged once before keying;
- [x] RxTxLog TX failure prevents physical keying;
- [x] existing ADIF/Cabrillo behavior remains regression-covered;
- [x] Linux full CTest passes;
- [x] portable units pass;
- [x] architecture checks pass;
- [x] ASan/UBSan for new pure/private timing helpers where practical;
- [x] real ADV build passes;
- [x] no unrelated cleanup.

## Required hardware acceptance — pc-1/QMX

T022 is not COMPLETE until real hardware is exercised.

### Preparation

Use an appropriate legal low-power setup. Begin with a dummy load if desired, then
move to the normal antenna for the actual QSO.

Build the reviewed branch and verify:

```text
/flash/ft8/station.txt

callsign=AG6AQ
grid=CM97
rxtx_log=1
```

The callsign/grid lines are required test identity for the first-QSO run; `rxtx_log` may be explicit or supplied by the accepted default-ON behavior.

Identify the QMX CDC node:

```bash
ls -l /dev/serial/by-id/ 2>/dev/null || true
ls -l /dev/ttyACM* 2>/dev/null || true
```

Prefer the stable `/dev/serial/by-id/...` path when available.

### Inherited T020 RF proof

Before or as part of the first integrated message, confirm:

- QMX actually keys;
- TA changes the transmitted audio/RF tone;
- TX ends and QMX returns to RX;
- no stuck PTT.

The existing bounded T020 diagnostic may be used once first if desired:

```text
ft8 --cat serial:<QMX> --cat-test-tone 1500 --cat-test-ms 500
```

but a successful full T022 FT8 transmission can also satisfy these inherited
hardware points if its behavior is directly observed.

### Integrated FT8/QSO run

Launch:

```text
M$> ft8 --cat serial:<QMX-CDC-path>
```

Use existing UI/AutoSeq controls to make a standard-call FT8 contact.

Acceptance requires:

- [ ] normal live QMX RX decodes before TX;
- [ ] selected TX occurs in correct parity slot;
- [ ] QMX keys only during the FT8 message;
- [ ] 79-tone plan executes for the appropriate remaining slot interval;
- [ ] QMX returns to RX at about 12.64 s after slot start;
- [ ] next receive slot decodes normally;
- [ ] AutoSeq progresses from real RX/TX, not simulation;
- [ ] at least one full real QSO completes on Linux/QMX;
- [ ] ADIF entry is sensible for the completed contact;
- [ ] no stuck CAT/tty/audio resource after `q`;
- [ ] repeated launch after the QSO works.

### Mandatory RxTxLog evidence

After the test, retain and inspect:

```text
/flash/ft8/RT[YYMMDD].txt
```

The relevant section must show the QSO sequence with both R and T records.

Paste the relevant RT lines into the T022 architect result.

If the QSO fails, **do not discard the trace**. The RT log is the primary debugging
artifact for deciding whether failure came from decode, AutoSeq policy, TX text,
slot timing, or radio execution.

## Automated test gate

Run:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T022-build-unit
cmake --build /tmp/T022-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T022-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/serial_protocol_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

Run focused sanitizer coverage for new scheduler/log helpers if they are separable
from MiniShell mocks.

No GitHub Actions wait.

## Branch workflow

Use:

```text
codex/T022-linux-qmx-first-qso
```

Codex:

1. read T020/T021 handoffs and current architecture;
2. implement the smallest correct physical-TX executor + RxTxLog slice;
3. preserve no-CAT simulation;
4. run all local gates;
5. set Status to REVIEW;
6. record exact files, timing semantics, failure semantics, tests, and limitations;
7. commit and push one reviewable implementation commit;
8. return commit SHA;
9. no PR;
10. no Actions wait;
11. no real RF testing by Codex.

Supervisor reviews the actual diff before the architect keys QMX.

## Codex implementation notes

### Implementation summary

Implemented the open-CAT physical executor using the unchanged T021 encoder and
T020 radio controller. No-CAT execution retains AS-7 simulation. The controller
owns the immutable plan, pending slot, absolute schedule, RX pause and completion
state; the executor allocates no heap. Main uses a 5 ms input wait while TX is
active. Queue/configuration actions are ignored and live-grid updates are held during
active TX so the encoded intent and completion facts cannot change; quit remains
available. No shared DSP, MiniShell public API, platform transport, or CAT
formatter changes.

Inspected the pinned V2 `main/main.cpp` and TX timing/TA tests at
`491e757ae6b1e4cfd2b9a6ba10f48b35643849e0` from the local Mini-FT8 checkout.
Preserved its absolute-symbol timing and daily R/T format through V3 ownership.

### Files changed

- `apps/ft8/src/app_controller/app_controller_tx_physical.c`: physical lifecycle,
  freshness deferral, identity validation, RT prerequisite and failure recovery.
- `apps/ft8/src/app_controller/app_tx_schedule.c` and `.h`: pure private timing.
- `apps/ft8/src/app_controller/app_controller_tx.c`: shared log snapshot/commit
  helpers, retained no-CAT simulation, active-plan protection.
- `apps/ft8/src/app_controller/app_controller.c`, `.h`,
  `app_controller_internal.h`, `app_controller_instance.c`: RX lifecycle,
  generation-aware RX logging/projection, active facade/state and grid protection.
- `apps/ft8/main/ft8_main.c`: active-TX input polling.
- `apps/ft8/src/config_service/config_service.c` and `.h`: default-on `rxtx_log`.
- `apps/ft8/src/log_service/log_service.c` and `.h`: daily direct RT append.
- `CMakeLists.txt`, `platform/adv/main/CMakeLists.txt`: source/test composition.
- `tests/ft8_tx_schedule_test.c`, `tests/ft8_physical_tx_test.c`: new regression tests.
- `tests/linux_audio_discontinuity_test.c`: buffered stop/start reset regression.
- `tests/linux_ft8_config_load.py`: expected serialization includes the authorized
  new key; original configuration failure/preservation assertions remain.
- `tests/architecture_rules.py`, `tests/serial_protocol_boundary.py`: private
  scheduler ownership, allowed encoder dependency and CAT mutation checks for
  executor/scheduler/logging paths.
- This task packet: software evidence and pending hardware handoff.

### Timing model

UTC and monotonic time are sampled in the candidate step. The anchor is
`monotonic_us - ms_into_slot * 1000`; all symbols use that anchor. Start retains
the existing <1000 ms window and rechecks it after RX stop/RT persistence. Begin
latency is included by resampling monotonic time before the initial tone. Each
step selects the current absolute 160 ms symbol, skips overdue symbols without
bursting, and deduplicates unchanged tones. End occurs on the first poll at or
after 12,640 ms. Backward monotonic time fails safe.

A pending live candidate waits for the preceding slot's applied batch (including
empty batches). Expiration skips that opportunity without consuming AutoSeq or
catching up on the wrong parity. Integration proves that an addressed previous-slot
FD decode changes the pending reply to `W6ABC AG6AQ RR73` before encoding.

### RX pause/restart model

Stop Audio before keying; preserve the open RX object, mark it inactive, reset
frontend continuity and mark timing pending. No RX reads occur during TX. Restore
radio RX before restarting the same stream. The first new samples re-anchor from
current UTC through the existing framer reset path. The Linux provider regression
queues old samples, stops/starts its worker, and proves only fresh samples emerge.
Shutdown restores/closes radio before closing/freeing Audio resources.

### RxTxLog behavior

Missing `rxtx_log` defaults to ON; explicit 0/1 parses and serializes. Each physical
plan attempt appends its canonical T line before keying; every finalized unique RX
message appends an R line before projection, including unaddressed activity. A
retained batch generation is handled once. Files use the configuration directory,
`RTYYMMDD.txt`, UTC from MiniShell, three-decimal band MHz and V2-compatible lines.
Append uses MiniShell WRITE|CREATE|APPEND, complete-write handling, sync and close.

TX RT failure prevents keying. RX RT failure increments a diagnostic counter and
reports through System.write, then continues decoder/AutoSeq processing without
retrying/duplicating the retained batch. Disabled logging emits no RT writes.
Existing ADIF/Cabrillo persistence and independent acknowledgments remain intact;
physical completion performs them only after successful end and Audio restart,
then ticks AutoSeq exactly once. Station fixture AG6AQ/CM97 flows through normal
configuration loading, AutoSeq, encoder and log facts; no production identity is
hardcoded. Encoder validation also protects free-text attempts from invalid station
identity by validating an ordinary CQ projection first.

### Failure/cleanup semantics

Plan, RX stop, RT append, begin, initial/later tone, clock and end failures do not
consume AutoSeq retries or successful physical counts. If radio restoration is
owed, immediately attempt end/RX; restart paused Audio even if restoration fails.
A recoverable failed attempt retains its semantic intent for a later eligible
slot. Failed restoration/restart returns an application error so normal shutdown
performs final cleanup. Successful RF end with failed Audio restart is explicitly
not counted or ticked. Tests inject both errors and short CAT writes.

### Local tests run

All commands ran locally; no hardware or GitHub Actions wait.

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: 54/54, including checker self-tests and existing AS-7/ADIF/Cabrillo tests

cmake -S tests/unit -B /tmp/T022-build-unit
cmake --build /tmp/T022-build-unit -j8
ctest --test-dir /tmp/T022-build-unit --output-on-failure
# PASS: 15/15

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/serial_protocol_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
# All exit 0; Serial checks include new path mutations

cmake -S . -B /tmp/T022-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/T022-build-sanitize --target ft8_physical_tx_unit ft8_tx_schedule_unit -j8
ctest --test-dir /tmp/T022-build-sanitize --output-on-failure -R 'ft8_physical_tx|ft8_tx_schedule'
# PASS: 2/2 with ASan/UBSan and leak detection. Initial sandbox execution hit
# LeakSanitizer's ptrace restriction; the same tests passed outside the sandbox.

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: real ESP32-S3 firmware; minishell_adv.bin 0xbb9e0 bytes, 88% app partition free

git diff --check
# PASS
```

Focused timing coverage runs 1/5/10/20 ms polling, late entry, delayed CAT writes,
large stalls, absolute completion and backward-clock failure. Integration uses
production controller/encoder/radio/logging with mocked MiniShell services,
checks exact CAT bytes through the production formatter, immutable plans,
RX recovery, station fixture identity, failure recovery, completion ACKs and
no-CAT simulation. RT tests cover exact date/path/lines, both bands, short writes,
zero writes, append/sync/close errors, default/disabled configuration and batch
idempotency.

### Hardware validation still required

Supervisor diff review followed by the architect's pc-1/QMX acceptance above:
verify AG6AQ/CM97 and RT logging ON, real key/tone/RX timing, correct parity,
post-TX decoding, full QSO, ADIF, quit/relaunch and retained R/T trace. No RF or
hardware tests were performed; no successful-QSO claim is made.

### Known limitations / risks

- Host scheduling and CAT transport latency still need physical measurement;
  deterministic fake-time tests do not establish real RF timing or reception.
- Direct append is not transactional: a failed write/sync/close can leave a partial
  or already-written RT record; TX is nevertheless aborted on that failure.
- Existing UI CQ controls remain placeholders; existing received-message selection
  supports reply initiation. No new CQ/parity UI was added.
- T019 synchronizes the radio dial when opening CAT. Select the intended station
  band before launch; changing bands in-session does not add radio retuning here.
- No task-scope deviations or platform/DSP behavior changes. ADV builds this shared
  code but still has no physical CAT provider.

### Commit

One implementation commit on `codex/T022-linux-qmx-first-qso`, titled
`T022: integrate Linux QMX FT8 transmit and RxTxLog`. The full pushed SHA is returned
in the handoff; this packet is part of that commit. No PR is opened.

## Supervisor review

Supervisor reviews:

- exact physical-vs-simulated selection;
- slot anchor math;
- missed-symbol behavior;
- T020/T021 API use;
- RX stop/start/re-anchor;
- AutoSeq completion ordering;
- RT append format/lifecycle;
- all RF-failure cleanup paths.

Only then does T022 move to TESTING.


## Supervisor review — integrated TX accepted for hardware testing

PASS for pc-1/QMX hardware/QSO testing on
`b723156359521256a1f323d3c496400febe39027`.

Reviewed the single implementation commit from T022 task head
`4e85ab59133bb3854d5438b1f02c55af5ddc5f98` and the complete
`main..b7231563` delta.

Accepted physical path:

```text
previous RX slot finalized/applied
    -> immutable T021 plan
    -> stop live Audio RX
    -> append+sync+close RT T record
    -> T020 MD6; TX;
    -> absolute-slot TA progression
    -> RX;
    -> restart Audio RX
    -> frontend/framer UTC re-anchor
    -> ADIF/Cabrillo eligibility commit
    -> AutoSeq tick exactly once
```

Timing review:

- slot anchor = monotonic observation minus UTC milliseconds-into-slot;
- first tone = floor(ms_into_slot / 160);
- all later tone decisions derive from absolute slot time;
- overdue symbols are skipped rather than emitted as a burst;
- repeated adjacent equal tones may omit redundant TA writes;
- completion is first poll at/after 12.640 s;
- backward monotonic movement fails safe;
- pre-key filesystem/Audio work is rechecked against the <1 s start window.

RX review:

- live Audio is stopped before any radio key command;
- Linux buffered stop/start resets the capture ring;
- no RX reads occur while physical TX is active;
- frontend phase is reset and timing is marked pending;
- post-TX start uses the existing UTC backdated first-sample re-anchor;
- failed RX restart does not count/tick a successful transmission.

Failure review:

- plan/identity, RX stop, RT T-line, begin, TA, clock and end failures do not
  consume AutoSeq completion;
- after a possible key-up, cleanup makes best-effort `RX;` and restarts Audio;
- T020 uncertain-TX `rx_required` semantics remain intact;
- normal shutdown still routes active/uncertain radio state through
  `radio_control_close()`.

RxTxLog review:

- missing setting defaults ON;
- only explicit `rxtx_log=0|1` is accepted;
- daily path is `/flash/ft8/RT[YYMMDD].txt`;
- R and T formats match the V2 semantics required by T022;
- append handles short writes, requires sync, and requires close;
- failed T record prevents keying;
- RX record failure is diagnosed but decoded/AutoSeq facts continue;
- retained batch generations cannot be logged/projected twice.

Station identity review:

- production contains no hardcoded AG6AQ/CM97 identity;
- the hardware fixture loads `callsign=AG6AQ`, `grid=CM97` through the normal
  station/config path;
- the test proves propagation into AutoSeq and the actual T021 plan
  `CQ AG6AQ CM97`.

Accepted evidence:

```text
Linux CTest          54/54 PASS
portable units       15/15 PASS
ASan/UBSan focused   PASS
architecture checks  PASS
real ADV build       PASS
git diff --check     PASS
```

No blocking software finding.

### First-QSO UI limitation

The current V3 O -> CQ / Beacon menu remains a placeholder; T022 does not add
manual CQ/beacon controls. Therefore the first real T022 QSO should use the
implemented responder path:

1. receive and decode a standard-callsign CQ;
2. on the RX screen select that CQ with its numbered line;
3. AutoSeq creates the reply on the opposite parity;
4. the physical executor sends the resulting plan.

This is sufficient to validate the first integrated QSO. Originating CQ/beacon
from V3 UI remains later UI work and is not claimed by T022.

T022 is now TESTING.

## Architect partial hardware result — TX proven on air

Hardware testing on pc-1/QMX has begun.

Observed by the architect:

```text
QMX physical FT8 TX             PASS
on-air FT8 decodability         PASS
received/spotted by stations    PASS
RxTxLog generation/content      PASS
full completed QSO              NOT YET
```

The fact that other stations spotted/decoded the transmission is accepted as
real-RF evidence that the T020 CAT key/tone/end path plus the T021 79-tone plan
produce a decodable FT8 signal on air.

This also satisfies the deferred T020 hardware proof for:

- actual QMX keying;
- TA-controlled FT8 tone transmission;
- bounded return from TX;
- usable physical FT8 emission.

RxTxLog is confirmed working on the real run, so the mandatory T022 debug trace
requirement is also hardware-validated.

T022 remains TESTING because the task's final acceptance still requires a full
real QSO and confirmation of the complete RX -> TX -> RX -> AutoSeq progression
through contact completion. Do not downgrade the proven TX/RxTxLog evidence if a
later QSO attempt fails for propagation, operator timing, or remote-station
reasons.

## Architect test result

Record:

- branch/commit;
- QMX CDC endpoint;
- band/dial frequency;
- callsign/grid configuration;
- RxTxLog setting;
- inherited T020 key/tone/RX evidence;
- first integrated TX behavior;
- first real QSO result;
- post-TX RX recovery;
- ADIF result;
- relevant `RT[YYMMDD].txt` lines;
- any failure diagnosis.
