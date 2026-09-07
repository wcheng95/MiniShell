# MiniShell Audio ABI

Status: **V1 implemented on Linux; append-only growth active**

MiniShell Audio provides platform-neutral PCM transport. It owns logical stream lifecycle and cleanup; providers own platform/file/device transport. The application owns channel/domain meaning.

## Capabilities

```c
MINI_AUDIO_CAP_RX
MINI_AUDIO_CAP_TX
```

Audio is optional. RX and TX are independent sub-APIs.

## Format

Audio requests carry sample rate, sample format, and channel count. V1 defines signed 16-bit PCM. Data moves in frames; one frame contains one sample from each channel, interleaved in channel-number order.

MiniFT8 currently requests:

```text
12000 Hz
S16
2 channels
```

MiniShell does not define whether those channels mean stereo, duplicated mono, or I/Q.

## Lifecycle

RX:

```text
open -> start -> read... -> stop -> close
```

TX:

```text
open -> start -> write... -> stop -> close
```

TX also exposes `abort()` as the fail-safe immediate-stop path. App teardown stops/closes RX and aborts/closes active TX.

V1 permits one RX and one TX stream per foreground app. Handles are MiniShell-owned and app-instance scoped.

## Exact-format behavior

V1 uses exact requested application-facing formats. A provider returns `MINI_ERR_UNSUPPORTED` when it cannot deliver the request. Providers may convert hardware-native format below the ABI while preserving channel order.

## Semantics intentionally above Audio

Audio does not know FT8/FT4, CAT/PTT, symbol timing, CPFSK, stereo/IQ meaning, complex DSP, or app downmix policy.

## Verification

Unit tests cover capability exposure, structure validation, unsupported formats, independent RX/TX lifecycles, frame semantics, channel ordering, invalid lifecycle use, end-of-stream, stale handles, and teardown cleanup.

The Linux WAV provider integration uses `tests/kfs16b12k.wav` and verifies exact 12 kHz/S16/two-channel replay plus reopen/cleanup behavior.
