# T008 — Surface Audio RX discontinuity

Status: READY

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

- [ ] generic `MINI_ERR_DISCONTINUITY` public result exists with documented RX meaning;
- [ ] public Audio forces zero frames for discontinuity and leaves stream usable;
- [ ] Linux ALSA successful recovery surfaces discontinuity instead of hiding it;
- [ ] Linux buffered worker remains alive and drains/discards while event is pending;
- [ ] old epoch frames cannot leak after discontinuity notification;
- [ ] adapter exposes a distinct discontinuity status;
- [ ] controller treats discontinuity as recoverable;
- [ ] frontend stream phase resets;
- [ ] framer re-anchors from UTC and emits existing STREAM_RESET;
- [ ] first partial slot after reset is discarded;
- [ ] WAV fixture behavior remains unchanged;
- [ ] no Audio TX/F12 changes;
- [ ] focused tests pass locally;
- [ ] architecture checks pass;
- [ ] full local suite result recorded;
- [ ] no unrelated cleanup.

## Codex implementation notes

### Implementation summary

### Public Audio discontinuity contract

### Linux provider / buffered epoch design

### FT8 recovery design

### Files changed

### Tests added

### Local tests run and results

### Hardware/manual validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and local evidence.

## Architect test result

No hardware validation is required for merge, but a later induced QMX overrun test may be useful as extra evidence.
