# MiniShell Audio API

Status: **V1 implemented on Linux**

Audio is a platform-neutral PCM transport service. MiniShell owns logical stream lifecycle/cleanup; providers own file/device transport; applications own domain/channel meaning.

MiniFT8 V1 requests `12000 Hz / S16 / 2 channels`. Frames are interleaved by channel number. MiniShell does not label the pair stereo or I/Q.

RX lifecycle: `open -> start -> read -> stop -> close`.
TX lifecycle: `open -> start -> write -> stop -> close`, with `abort()` for fail-safe TX shutdown. RX/TX are independent and app teardown cleans both.

V1 requests exact application-facing formats; providers may perform native-format conversion below the API while preserving channel order.

The Linux WAV provider uses `tests/kfs16b12k.wav` and integration tests verify exact two-channel replay and cleanup/reopen behavior.

The API is still under active development. Audio source compatibility and binary compatibility are not frozen; applications in the MiniShell tree are rebuilt when the public API changes.

## RX discontinuity

`read()` may return `MINI_ERR_DISCONTINUITY` when sample continuity since the
previous successful read is no longer guaranteed. `out_frames` is zero; no
frames accompany this result. The stream remains open and started, and callers
may immediately read again. The Audio service does not stop, restart, or close
it. Applications own any DSP/timing reset required by a gap.

Linux ALSA recovery reports this result and resets native decimation phase.
Buffered capture drains/discards while the event awaits consumer acknowledgement;
old queued frames are flushed before normal delivery resumes. WAV playback does
not emit discontinuity. This additive result does not change public layouts or
`MINISHELL_API_VERSION`; callers treating unknown nonzero results as errors
continue to fail safely.

## TX write wait budget and partial progress

For `write(stream, frames, frame_count, out_frames, timeout_ms)`, the public
service initializes `out_frames` to zero before calling the provider. Providers
must report at most `frame_count`; successful over-reporting is rejected as IO.

- `MINI_WAIT_NONE`: do not intentionally wait for additional transport capacity.
- Finite `timeout_ms`: the caller's maximum permitted transport wait budget for
  the entire write call.
- `MINI_WAIT_FOREVER`: unbounded transport waiting is permitted.

Scheduler, interrupt, and call overhead are not a hard-real-time guarantee.
Providers must not knowingly substitute unbounded blocking for a finite budget.
If at least one frame is accepted, return `MINI_OK` with the accepted count;
partial progress is legal and the caller may submit the remainder. If no frame
can be accepted within a finite budget (including `MINI_WAIT_NONE`), return
`MINI_ERR_TIMEOUT` with zero frames. Other transport failures return their normal
error; `out_frames` must not claim unaccepted/uncommitted progress.

These are intended provider requirements, not evidence that every current
backend complies. T009 records ADV's resolved codec path and supplies a hardware
latency probe; it does not change that provider or Keyer scheduling. Public
struct layouts and API version remain unchanged.
