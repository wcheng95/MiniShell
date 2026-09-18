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
