# MiniShell Audio ABI

Status: **V1 implemented on Linux**

Audio is a platform-neutral PCM transport service. MiniShell owns logical stream lifecycle/cleanup; providers own file/device transport; applications own domain/channel meaning.

MiniFT8 V1 requests `12000 Hz / S16 / 2 channels`. Frames are interleaved by channel number. MiniShell does not label the pair stereo or I/Q.

RX lifecycle: `open -> start -> read -> stop -> close`.
TX lifecycle: `open -> start -> write -> stop -> close`, with `abort()` for fail-safe TX shutdown. RX/TX are independent and app teardown cleans both.

V1 requests exact application-facing formats; providers may perform native-format conversion below the ABI while preserving channel order.

The Linux WAV provider uses `tests/kfs16b12k.wav` and integration tests verify exact two-channel replay and cleanup/reopen behavior.
