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

## Optional continuous tone owner (T043)

`MINI_AUDIO_CAP_TONE` and the appended `mini_audio_api_t::tone` table provide a
continuous speaker-tone owner. API version remains v3: existing PCM fields and
RX/TX timeout, short-progress, stop and abort semantics are unchanged. Discover
the new table only after checking `audio->struct_size` through the end of `tone`,
the capability bit, and the tone table's size/callbacks. An older/PCM-only provider
need not supply the extension.

`mini_audio_tone_config_t` contains its size, pitch (300–999 Hz) and volume
(0–99). The generic interface contains no CW letters or Keyer concepts:

| Operation | Contract |
| --- | --- |
| `open(config, out_tone)` | Acquire the exclusive speaker-tone owner, configured and ready after silent startup priming. |
| `configure(tone, config)` | Change pitch/codec level without restarting the stream or phase. |
| `enqueue(tone, duration_ms)` | Append one finite tone of 1–60,000 ms. A full 64-entry ring returns `MINI_ERR_NO_SPACE`; nothing is added. |
| `hold(tone, 1)` | Replace queued work with an indefinite tone, releasing the old cursor first. Repeated hold-on is idempotent. |
| `hold(tone, 0)` | Release an active hold and clear its queued work; inactive hold-off is a no-op. |
| `stop(tone)` | Flush all queued work and request a shaped release of the current cursor. |
| `busy(tone, out_busy)` | True for a hold or finite keyed samples not yet committed toward DMA. False after preempt/flush; release tails and downstream DMA drain are not included. |
| `close(tone)` | Release, drain, silence and relinquish ownership. Consumes the public handle even on failure. |

Tone and ordinary PCM TX ownership are exclusive. PCM RX remains independent.
Stale/zero handles, invalid ranges and undersized configurations are rejected.
App begin/end automatically release a retained tone owner. Provider I/O failures
propagate through the API; they must not leave stale continuing sound.

ADV uses the pinned Mini-CW V1.2 continuous worker: 48 kHz S16 mono, 240-frame
chunks, eight initial zero chunks before unmute, priority 5, 6144-byte stack and
4×120-frame I2S DMA geometry. Finite segments and hold/release use the original
quarter-sine squared envelope and retained oscillator phase. The codec stays
unmuted between elements and zero-PCM gaps. This path uses `esp_codec_dev_write`;
ordinary PCM `speaker_write` continues to use direct I2S with T010's timeouts.
Producer mutex waits are 20 ms; closing waits at most 3 seconds for worker
retirement. If retirement times out, the provider retains private ownership and
a later tone/PCM open reaps the retired worker before reuse. No running worker
is forcibly deleted or freed. Initialization has bounded startup/retirement waits
in addition to codec/driver initialization calls.

Linux supplies a silent deterministic simulation of the same renderer and busy
accounting driven by monotonic time at API calls. It does not create a real-time
thread or replace ordinary Linux PCM backends. Its volume is validated but has
no audible output. This is software test support, not hardware audio acceptance.
