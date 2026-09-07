# MiniShell Audio ABI

Status: **V1 implemented on the Linux reference target; append-only growth active**

The Audio ABI provides platform-neutral PCM transport between an application and MiniShell-owned audio providers/backends. It is deliberately ignorant of digital-mode semantics and channel meaning.

## 1. Ownership boundary

```text
application
    |
    | MiniShell Audio ABI
    v
MiniShell Audio service
    |
    v
provider / platform backend
    |
    v
WAV source, ALSA, USB UAC, I2S, hardware, ...
```

MiniShell owns the physical/provider resource, backend handles, transport-format conversion, and cleanup at foreground-app exit. The application owns domain interpretation. MiniShell does not know whether two channels mean left/right audio, duplicated mono, I/Q, or another pairing.

## 2. Capabilities

```c
#define MINI_AUDIO_CAP_RX  (1ull << 0)
#define MINI_AUDIO_CAP_TX  (1ull << 1)
```

The Audio service is optional. `mini_api_t.audio == NULL` means the runtime has no usable Audio capability.

When Audio is present:

```text
MINI_AUDIO_CAP_RX set <=> audio->rx != NULL
MINI_AUDIO_CAP_TX set <=> audio->tx != NULL
```

A platform may provide RX only, TX only, or both.

## 3. Format

The public format is explicit:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t sample_rate_hz;
    uint32_t sample_format;
    uint32_t channels;
} mini_audio_format_t;
```

V1 defines `MINI_AUDIO_SAMPLE_S16` for signed 16-bit PCM samples in the native target C representation used by the application and resident MiniShell runtime.

Audio data is transferred in frames, not bytes. One frame contains one sample for every channel. Multi-channel samples are interleaved in channel-number order. For two-channel S16:

```text
frame 0: ch0, ch1
frame 1: ch0, ch1
frame 2: ch0, ch1
...
```

The Audio ABI does not assign semantic names to channel 0 or channel 1.

### MiniFT8 V1 requested format

```text
12000 Hz
S16
2 channels
```

That is a MiniFT8 application requirement, not a permanent global MiniShell audio format. MiniFT8 may interpret the same two-channel stream differently by source profile, for example:

```text
QMX-AUDIO -> ordinary audio channels -> select/downmix in MiniFT8
QMX-IQ    -> channel 0 = I, channel 1 = Q -> I/Q processing in MiniFT8
```

MiniShell does not distinguish these cases.

## 4. Stream handles

```c
typedef uint32_t mini_audio_stream_t;
#define MINI_AUDIO_STREAM_INVALID ((mini_audio_stream_t)0u)
```

A stream handle is owned by MiniShell and valid only for the foreground application instance that opened it. It must not be persisted or reused after `close()` or application exit.

The initial resident implementation permits one open RX stream and one open TX stream per foreground application. RX and TX are independent and may be active at the same time.

## 5. Endpoint argument

`open()` accepts an optional UTF-8 `endpoint` string.

```text
endpoint == NULL   provider/backend default
endpoint != NULL   provider/backend-specific logical endpoint name
```

An empty string is invalid in V1. Endpoint naming/discovery is deliberately not frozen yet; enumeration can be added later through append-only Audio API growth when real multiple-device use requires it.

## 6. RX API

```c
typedef struct {
    uint32_t struct_size;
    mini_result_t (*open)(const char *endpoint,
                          const mini_audio_format_t *format,
                          mini_audio_stream_t *out_stream);
    mini_result_t (*start)(mini_audio_stream_t stream);
    mini_result_t (*read)(mini_audio_stream_t stream,
                          void *frames,
                          uint32_t frame_capacity,
                          uint32_t *out_frames,
                          uint32_t timeout_ms);
    mini_result_t (*stop)(mini_audio_stream_t stream);
    mini_result_t (*close)(mini_audio_stream_t stream);
} mini_audio_rx_api_t;
```

Lifecycle:

```text
open -> start -> read... -> stop -> close
```

`start()` and `stop()` are idempotent for a valid open stream. `read()` requires a started stream. A finite provider such as a WAV source returns `MINI_ERR_END_OF_STREAM` when no more frames remain.

## 7. TX API

```c
typedef struct {
    uint32_t struct_size;
    mini_result_t (*open)(const char *endpoint,
                          const mini_audio_format_t *format,
                          mini_audio_stream_t *out_stream);
    mini_result_t (*start)(mini_audio_stream_t stream);
    mini_result_t (*write)(mini_audio_stream_t stream,
                           const void *frames,
                           uint32_t frame_count,
                           uint32_t *out_frames,
                           uint32_t timeout_ms);
    mini_result_t (*stop)(mini_audio_stream_t stream);
    mini_result_t (*abort)(mini_audio_stream_t stream);
    mini_result_t (*close)(mini_audio_stream_t stream);
} mini_audio_tx_api_t;
```

`abort()` is the fail-safe immediate-stop path for active TX. Normal application teardown aborts an active TX stream before closing it.

## 8. Exact-format request

V1 uses exact-format requests rather than implicit negotiation. If a provider/backend cannot supply the requested application-facing format, it returns `MINI_ERR_UNSUPPORTED`.

A backend may internally convert from a native hardware/file format to the exact requested ABI format. For example:

```text
QMX native UAC: 48 kHz / 24-bit / 2-channel
    -> backend conversion
    -> MiniFT8 request: 12 kHz / S16 / 2-channel
```

The backend preserves channel ordering through that conversion. It does not interpret the channel pair as stereo or I/Q.

## 9. Buffer ownership

RX and TX buffers remain application-owned. MiniShell uses them only for the duration of the synchronous `read()` or `write()` call and does not retain their pointers afterward.

## 10. Application lifecycle cleanup

At app teardown MiniShell performs best-effort cleanup:

```text
RX active -> stop -> close
TX active -> abort -> close
```

## 11. Non-responsibilities

The Audio ABI does not own or understand FT8/FT4/RTTY/CW, symbol timing, CPFSK, radio CAT commands, PTT policy, stereo/IQ semantics, complex DSP, or application downmix policy. Those belong above Audio in the application or, for radio control, in the separate Control ABI.

## 12. V1 verification

Unit and integration tests cover service absence/capabilities, `struct_size`, unsupported formats, independent RX/TX lifecycles, frame semantics, exact two-channel ordering, invalid lifecycle use, finite-source end-of-stream, stale handles, TX abort cleanup, and automatic close.

The first Linux provider integration uses `tests/kfs16b12k.wav`, which matches `12000 / S16 / 2-channel`. The WAV provider streams the two-channel PCM unchanged and is exercised by a runtime-loaded Audio probe and Linux integration test.
