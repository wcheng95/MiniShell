# T008 — Surface Audio RX discontinuity

Status: REVIEW

## Objective

Resolve T001 F05: a live Audio provider must not silently recover from lost sample continuity while MiniFT8 continues assuming an unbroken sample clock.

Introduce one generic recoverable stream result:

```text
MINI_ERR_DISCONTINUITY
```

Meaning for Audio RX:

```text
the stream is still open/started and may be read again,
but sample continuity since the previous successful read is not guaranteed.
No frames accompany this result.
```

Provider reports the fact. MiniFT8 owns the FT8 response.

## Ownership

Preserve this direction:

```text
Linux ALSA provider
    detects native recovery / lost continuity
        |
        v
MiniShell Audio
    exposes generic MINI_ERR_DISCONTINUITY
        |
        v
rx_audio_adapter
    maps to RX_AUDIO_ADAPTER_DISCONTINUITY
        |
        v
app_controller
    owns FT8 recovery policy
        |
        +-> reset RxFrontend stream phase
        +-> re-anchor RxSlotFramer from MiniShell UTC
        +-> emit existing STREAM_RESET -> Ft8Engine reset
```

No ALSA/QMX/FT8 semantics may leak across the wrong boundary.

## Public Audio contract

Add to `include/minishell/api.h`:

```c
#define MINI_ERR_DISCONTINUITY ((mini_result_t)-18)
```

This is additive. Do not change public struct layout and do not bump `MINISHELL_API_VERSION` in T008.

For `mini_audio_rx_api_t.read()`:

- on `MINI_ERR_DISCONTINUITY`, `out_frames` must be 0;
- stream remains valid and started;
- caller may immediately read again;
- applications that do not understand the new result still fail safely because it is nonzero.

Document this in `docs/api/audio-api.md`.

## MiniShell Audio service

`core/minishell_services/audio_service.c` must pass the provider result through.

For `MINI_ERR_DISCONTINUITY`, force `*out_frames = 0` even if a buggy backend supplied a nonzero count.

Do not stop/close/restart the stream automatically.

## Linux ALSA provider

In `platform/linux/linux_audio_wav.c`:

Current successful `snd_pcm_recover()` calls hide a possible capture gap.

Change recovered ALSA errors to:

```text
successful recover
    -> reset native decimation phase
    -> discard any frames produced in that provider read call
    -> out_frames = 0
    -> return MINI_ERR_DISCONTINUITY
```

A failed recovery remains `MINI_ERR_IO`.

Be conservative: if ALSA required recovery, continuity is not guaranteed. Do not try to infer FT8 policy here.

WAV/file playback never emits discontinuity.

## Linux buffered live-capture wrapper

`platform/linux/linux_audio_buffered.h` must keep the worker alive across a recoverable discontinuity.

Required semantics:

1. Underlying `MINI_ERR_DISCONTINUITY` is not a terminal worker error.
2. Publish one pending discontinuity to the consumer.
3. Frames already queued from the old continuity epoch must never be delivered after the event.
4. While discontinuity is pending, the worker must keep draining the underlying live source but discard those frames. This avoids causing another ALSA overrun while synchronous FT8 decode is still running.
5. Consumer `read()` observes the discontinuity before returning any queued frames:
   - flush/drop the old ring contents;
   - clear/acknowledge the pending epoch safely;
   - return `MINI_ERR_DISCONTINUITY` with zero frames.
6. After acknowledgement, worker resumes enqueueing fresh frames and later reads return normal data.

Use atomics; preserve the current one-producer/one-consumer ownership. Do not add locks to the hot path.

Avoid a race where the worker can enqueue new-epoch frames while the consumer is flushing the old epoch. A small atomic state/handshake is preferred over a plain boolean.

Fatal worker errors remain terminal as today.

## MiniFT8 adapter

Add:

```text
RX_AUDIO_ADAPTER_DISCONTINUITY
```

as a non-error/non-EOS status.

`rx_audio_adapter_read()` maps:

```text
MINI_ERR_DISCONTINUITY
    -> RX_AUDIO_ADAPTER_DISCONTINUITY
    -> out_frames = 0
    -> last_result = MINI_ERR_DISCONTINUITY
```

The adapter does not reset DSP/timing and does not close/restart Audio.

## MiniFT8 controller recovery

`RxFrontend` already provides:

```c
rx_frontend_reset_stream()
```

`RxSlotFramer` already provides:

```c
rx_slot_framer_reset_stream()
```

Use them; do not invent a new timing layer.

On `RX_AUDIO_ADAPTER_DISCONTINUITY`:

1. do not treat it as fatal;
2. reset `RxFrontend` stream phase immediately;
3. mark RX timing as pending;
4. if a framer stream had already been established, remember that the next timing establishment must use `rx_slot_framer_reset_stream()`, not a fresh init;
5. return to the main loop without processing stale frames.

On the next nonzero frontend output:

1. obtain current UTC using existing `utc_to_slot_reference()`;
2. backdate by the newly produced 6 kHz sample count using existing `backdate_slot_reference()`;
3. if this is first establishment, use `rx_slot_framer_init()`;
4. if recovering from discontinuity, use `rx_slot_framer_reset_stream(..., rx_emit_event, rx)`;
5. then process the fresh samples normally.

The existing STREAM_RESET event must continue to make `Ft8Engine` clear DSP stream continuity while retaining protocol/hash knowledge.

A nonzero new sample offset naturally causes the framer to discard the first partial slot and resume at the next complete 15 s boundary.

If UTC is unavailable when re-anchoring is required, fail RX rather than guessing timing.

Do not erase an already completed retained `RxBatch` merely because the live stream reset; it remains historical UI data until replaced by a later decode.

## Explicit-timing mode

A transport discontinuity invalidates any prior sample-count timing reference, including one originally supplied through `--rx-slot`.

If a provider ever reports discontinuity in such a stream, recovery uses current MiniShell UTC. Do not continue the synthetic slot identity across missing samples.

WAV fixture providers do not emit discontinuity, so deterministic fixture behavior remains unchanged.

## Tests

### 1. MiniShell Audio unit

Extend `tests/unit/test_audio.c`.

Fake provider sequence must prove:

- provider returns `MINI_ERR_DISCONTINUITY`;
- public Audio returns the same result;
- public `out_frames == 0` even if fake provider sets a bogus nonzero value;
- stream remains started/usable;
- next normal read succeeds without reopen/restart.

### 2. RxAudioAdapter unit

Extend `tests/rx_audio_adapter_rx6_test.c`.

Prove:

- `MINI_ERR_DISCONTINUITY` -> `RX_AUDIO_ADAPTER_DISCONTINUITY`;
- zero frames;
- last raw result preserved;
- next read may succeed on the same stream.

### 3. Linux buffered wrapper focused test

Add a deterministic host test for `linux_audio_buffered.h` using a fake underlying live provider.

Required sequence:

```text
old-epoch frames queued
underlying discontinuity
post-gap frames while consumer is busy
consumer read
    -> DISCONTINUITY first
    -> zero frames
old/pre-ack data is not delivered
next read(s)
    -> only fresh post-ack epoch frames
worker remains running
```

Also verify fatal underlying error is still surfaced terminally.

The test may use real pthreads locally but must not require ALSA hardware.

### 4. Linux ALSA recovery mapping

Add the narrowest practical regression around the recovery mapping.

If direct ALSA fault injection would require invasive seams, keep the production change local and add a small private helper/testable decision function rather than introducing a test-only endpoint.

Do not require physical QMX hardware for T008.

### 5. FT8 controller/framer behavior

Add focused coverage proving the controller response to adapter discontinuity:

- discontinuity is nonfatal;
- frontend phase is reset;
- stale partial framer state is discarded;
- existing `STREAM_RESET` reaches engine reset;
- next fresh samples re-anchor from UTC;
- first partial slot after re-anchor is discarded;
- subsequent complete slot can proceed.

Prefer a focused white-box host unit over modifying the already known-failing `linux_ft8.py`.

If a clean controller test requires a tiny private helper extraction, keep it inside `app_controller` ownership and document it. Do not expose a new public application API.

### 6. Existing pure framer test

Keep `test_stream_reset()` passing. Extend only if necessary to prove an invariant not already covered.

## API / documentation constraints

Expected changed production files include:

```text
include/minishell/api.h
docs/api/audio-api.md
core/minishell_services/audio_service.c
platform/linux/linux_audio_wav.c
platform/linux/linux_audio_buffered.h
apps/ft8/src/rx_audio_adapter/rx_audio_adapter.h
apps/ft8/src/rx_audio_adapter/rx_audio_adapter.c
apps/ft8/src/app_controller/app_controller.c
```

Tests/CMake as required.

ADV WAV provider should require no behavior change; it simply never returns the new result.

Do not change Audio TX in T008.

## Non-goals

Do not:

- implement F12 TX blocking/latency changes;
- change ring capacity as the fix;
- drop samples silently instead of reporting discontinuity;
- restart/close Audio automatically in MiniShell service;
- put FT8 slot logic in Linux provider;
- add QMX-specific public API;
- redesign RxSlotFramer;
- fix `linux_audio` stale output-substring baseline failure;
- fix `linux_ft8` stale queue-order baseline failure;
- perform F08/F09/F10/F11 cleanup.

## Local build/test gate

Run locally, no GitHub Actions wait:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux   -R 'audio|rx_audio_adapter|rx_slot_framer|discontinuity'   --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer
python3 tests/ft8_platform_boundary.py .

cmake -S tests/unit -B /tmp/T008-build-unit
cmake --build /tmp/T008-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T008-build-unit --output-on-failure

git diff --check

ctest --test-dir build-linux --output-on-failure
```

Record exact local results. The two accepted baseline failures may remain.

## Branch workflow

Use existing branch:

```text
codex/T008-audio-discontinuity
```

Before handoff:

1. set this task to `Status: REVIEW`;
2. fill implementation/test notes;
3. commit and push the bounded implementation;
4. return the commit SHA;
5. do not open a PR;
6. do not wait for GitHub Actions.

After supervisor review and merge/fast-forward to `main`, delete local and remote T008 branches.

## Acceptance criteria

- [x] generic `MINI_ERR_DISCONTINUITY` public result exists with documented RX meaning;
- [x] public Audio forces zero frames for discontinuity and leaves stream usable;
- [x] Linux ALSA successful recovery surfaces discontinuity instead of hiding it;
- [x] Linux buffered worker remains alive and drains/discards while event is pending;
- [x] old epoch frames cannot leak after discontinuity notification;
- [x] adapter exposes a distinct discontinuity status;
- [x] controller treats discontinuity as recoverable;
- [x] frontend stream phase resets;
- [x] framer re-anchors from UTC and emits existing STREAM_RESET;
- [x] first partial slot after reset is discarded;
- [x] WAV fixture behavior remains unchanged;
- [x] no Audio TX/F12 changes;
- [x] focused tests pass locally;
- [x] architecture checks pass;
- [x] full local suite result recorded;
- [x] no unrelated cleanup.

## Codex implementation notes

### Implementation summary

Added recoverable Audio RX discontinuity propagation from Linux ALSA through
buffered capture, public Audio, and the FT8 adapter to controller-owned recovery.
No task deviations or new application API. WAV transport, Audio TX, ring capacity,
AutoSeq, and the two baseline tests are unchanged.

### Public Audio discontinuity contract

`MINI_ERR_DISCONTINUITY` is additive result -18. Public Audio forces zero frames
on this result and keeps the stream started/usable. The adapter exposes distinct
`RX_AUDIO_ADAPTER_DISCONTINUITY`, preserves the raw result, and performs no DSP
reset or stream restart. Public struct layouts/API version are unchanged; the
RX meaning and caller responsibility are documented in `docs/api/audio-api.md`.

### Linux provider / buffered epoch design

Both ALSA wait-error and read-error recovery paths return discontinuity on
successful recovery, reset native decimation phase, and discard any data produced
in that call. Recovery failure still returns IO. Output count remains zero.

The buffered wrapper uses atomic RUNNING/PENDING/ACKNOWLEDGED states. Only the
producer publishes PENDING; it continues reading but discards data until consumer
acknowledgement. Only the consumer advances its read counter to flush the ring,
then publishes ACKNOWLEDGED. The producer acquires that acknowledgement before
starting a fresh read. A read begun while pending is discarded even if ACK arrives
while it is in flight. Every provider discontinuity unconditionally publishes
PENDING, including a read begun while pending whose previous event was acknowledged
during that read. Gaps coalesce only if the consumer has not acknowledged yet. A second
pending check after copying prevents delivery when a gap was published during
that copy. Fatal errors remain terminal. No hot-path locks were added and the
single-producer/single-consumer ring-counter ownership is preserved.

### FT8 recovery design

The controller resets frontend phase immediately, leaves RX active, and marks
timing pending without touching the retained batch. Existing `framer_initialized`
state distinguishes first establishment from recovery, including explicit-slot
streams. On the next nonzero frontend output it obtains UTC, backdates by the
new sample count, and initializes or resets the existing framer as appropriate.
Reset uses the existing callback/STREAM_RESET path to clear engine DSP continuity
while retaining hash knowledge. A partial first slot is discarded. Missing UTC
returns failure rather than continuing a guessed timing reference.

### Files changed

- `include/minishell/api.h`, `docs/api/audio-api.md`: additive result and RX contract.
- `core/minishell_services/audio_service.c`: zero-frame enforcement.
- `platform/linux/linux_audio_wav.c`: ALSA recovery mapping.
- `platform/linux/linux_audio_buffered.h`: atomic epoch handshake/drain policy.
- `apps/ft8/src/rx_audio_adapter/rx_audio_adapter.h` and `.c`: distinct status mapping.
- `apps/ft8/src/app_controller/app_controller.c`: existing frontend/framer recovery.
- `tests/unit/test_audio.c`, `tests/rx_audio_adapter_rx6_test.c`: propagation/usability.
- `tests/linux_audio_discontinuity_test.c`: deterministic threaded wrapper test.
- `tests/linux_alsa_discontinuity_test.c`: private ALSA callback fault injection.
- `tests/ft8_rx_discontinuity_test.c`: white-box production RX stack test.
- `CMakeLists.txt`: new focused tests and existing pure framer test registration.
- This task packet: handoff evidence and REVIEW status.

### Tests added

Public Audio and adapter tests deliberately receive bogus nonzero counts with
discontinuity, assert zero output and preserved lifecycle, then read successfully
without reopening/restarting.

The pthread wrapper test queues old data, reports a gap, drains multiple pending
reads, acknowledges while another read is in flight, and verifies only a fresh
post-ACK read is delivered. Repeated pending gaps coalesce, worker stays alive,
and a later IO error is terminal. Synchronization uses explicit test tickets and
bounded waits rather than timing-dependent sleeps as the proof.

The ALSA test includes the private provider implementation and replaces its
existing function pointers: both recovery sites, partial production before a
gap, phase reset, failed recovery, and subsequent successful reading are tested
without hardware. It passes locally with ALSA headers; hosts lacking those
headers explicitly skip only this provider-specific test (CTest return 77).

The controller test includes the implementation for private-state inspection and
links the real frontend/framer/engine stack. It proves frontend phase reset,
UTC replacement of explicit timing, discarded partial framer state, cleared
engine sample history, preserved nonempty hash knowledge and retained historical
batch, a subsequent complete decoded window, missing-UTC failure, and gap before
initial timing establishment. Existing `test_stream_reset()` runs unchanged in
the newly registered pure framer target. No production test seam was needed.

### Local tests run and results

Base: `63d437db33bc578cbbc2c54f1a4f82cbb71cad4a`.

- `git status --short`: clean on the existing T008 branch before implementation.
- `cmake -S . -B build-linux`: PASS.
- `cmake --build build-linux -j"$(nproc)"`: PASS; final rebuild also PASS.
- `ctest --test-dir build-linux -R 'audio|rx_audio_adapter|rx_slot_framer|discontinuity' --output-on-failure`:
  5/6 PASS; only the documented `linux_audio` stale substring failure.
- Final focused rerun after strengthening history/hash and ALSA assertions:
  `ctest --test-dir build-linux -R 'rx_audio_adapter|rx_slot_framer|discontinuity' --output-on-failure`:
  PASS, 5/5, including the real ALSA mapping test (not skipped).
- `python3 tests/app_dependency_boundary.py . ft8`: PASS.
- `python3 tests/app_dependency_boundary.py . keyer`: PASS.
- `python3 tests/ft8_platform_boundary.py .`: PASS (54 source/header files).
- `cmake -S tests/unit -B /tmp/T008-build-unit`: PASS.
- `cmake --build /tmp/T008-build-unit -j"$(nproc)"`: PASS.
- `ctest --test-dir /tmp/T008-build-unit --output-on-failure`: PASS, 14/14.
- `git diff --check`: PASS.
- `ctest --test-dir build-linux --output-on-failure`: 26/28 PASS. Only accepted
  baseline failures remain: `linux_audio` (stale frames/hash substring) and
  `linux_ft8` (stale queue-order expectation). Neither was modified.

Supervisor race follow-up: the buffered regression now acknowledges the first
event while provider read 5 is in flight, returns a second discontinuity from
that read, and requires a second zero-frame notification. Read 6, begun while
pending, is still discarded after acknowledgement; fresh read 7 is delivered.
The extended test failed on the original conditional publication and passed with
unconditional PENDING publication. No other production behavior changed.

Reran the complete T008 local gate after this correction: Linux configure/build
PASS; required audio/discontinuity filter 5/6 PASS (only baseline `linux_audio`);
all three architecture commands PASS; unit configure/build PASS and CTest 14/14
PASS; `git diff --check` PASS; full Linux CTest 26/28 PASS with only the same
`linux_audio` and `linux_ft8` baseline failures. Existing implementation commit
amended as requested.

All checks ran locally. No PR opened and no GitHub Actions wait.

### Hardware/manual validation still required

None required for merge. An induced live QMX overrun may provide additional
hardware evidence later; deterministic fake-provider tests cover this handoff.

### Known limitations / risks

The existing ring-full backpressure policy is unchanged; T008 does not address
F12 latency/blocking. Pending recovery intentionally discards samples until a
safe consumer acknowledgement, then FT8 discards the initial partial slot.
Real backend/device recovery behavior has not been tested on hardware here.
The two known unrelated full-suite failures remain.

### Commit

One bounded implementation commit on `codex/T008-audio-discontinuity` containing
this report. The pushed SHA is returned in the handoff. Branch deletion remains
deferred until supervisor review and merge/fast-forward to main.

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and local evidence.

## Architect test result

No hardware validation is required for merge, but a later induced QMX overrun test may be useful as extra evidence.
