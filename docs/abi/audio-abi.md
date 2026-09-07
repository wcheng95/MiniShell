# MiniShell Audio ABI

Status: **V1 service core implemented; Linux WAV RX provider implemented; append-only growth active**

The Audio ABI provides platform-neutral PCM transport between an application and
MiniShell-owned audio providers/backends. It is deliberately ignorant of digital
mode semantics and of channel meaning.

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

MiniShell owns the physical/provider resource, backend handles, transport-format
conversion, and cleanup at foreground-app exit.

The application owns domain interpretation. MiniShell does not know whether two
channels mean left/right audio, duplicated mono, I/Q, or another pairing.

## 2. Capabilities

```c
#define MINI_AUDIO_CAP_RX  (1ull << 0)
#define MINI_AUDIO_CAP_TX  (1ull << 1)
```

The Audio service is optional. `mini_api_t.audio == NULL` means the runtime has no
usable Audio capability.

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

V1 defines:

```c
#define MINI_AUDIO_SAMPLE_S16  1u
```

`S16` means signed 16-bit PCM samples in the native target C representation used
by the application and resident MiniShell runtime.

Audio data is transferred in **frames**, not bytes. One frame contains one sample
for every channel. Multi-channel samples are interleaved in channel-number order.
For two-channel S16:

```text
frame 0: ch0, ch1
frame 1: ch0, ch1
frame 2: ch0, ch1
...
```

The Audio ABI does not assign semantic names to channel 0 or channel 1.

### MiniFT8 V1 requested format

MiniFT8 currently requests:

```text
12000 Hz
S16
2 channels
```

That is a MiniFT8 application requirement, not a permanent global MiniShell audio
format. The interface remains format-capable.

MiniFT8 may interpret the exact same two-channel stream differently by source
profile, for example:

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

A stream handle is owned by MiniShell and valid only for the foreground
application instance that opened it. It must not be persisted or reused after
`close()` or application exit.

The initial resident implementation permits one open RX stream and one open TX
stream per foreground application. RX and TX are independent and may be active at
the same time.

## 5. Endpoint argument

`open()` accepts an optional UTF-8 `endpoint` string.

```text
endpoint == NULL   provider/backend default
endpoint != NULL   provider/backend-specific logical endpoint name
```

An empty string is invalid in V1.

The endpoint string is borrowed only for the duration of `open()`. A backend that
needs it later must copy it.

Endpoint naming/discovery is deliberately not frozen yet. Enumeration can be
added later through append-only Audio API growth when real multiple-device use
requires it.

The Linux WAV RX provider interprets an absolute MiniShell logical path such as:

```text
/flash/MiniFT8/test.wav
```

It does not expose or accept a Linux host path as part of the application contract.

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

`start()` and `stop()` are idempotent for a valid open stream.

`read()` requires a started stream. On `MINI_OK`, `out_frames` is never greater
than `frame_capacity`.

A finite provider such as a WAV source returns:

```c
MINI_ERR_END_OF_STREAM
```

when no more frames remain.

For live/nonblocking providers, `MINI_WAIT_NONE`, finite millisecond timeouts, and
`MINI_WAIT_FOREVER` use the same timeout constants already shared by MiniShell.

A deterministic file provider does not sleep to emulate real-time pacing.

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

Lifecycle:

```text
open -> start -> write... -> stop -> close
```

`abort()` is the fail-safe immediate-stop path for active TX. It leaves the stream
open so an application may start it again if appropriate.

Normal application teardown aborts an active TX stream before closing it.

## 8. Exact-format request

V1 uses exact-format requests rather than implicit negotiation.

If a provider/backend cannot supply the requested application-facing format, it
returns:

```c
MINI_ERR_UNSUPPORTED
```

A backend may internally convert from a native hardware/file format to the exact
requested ABI format. For example:

```text
QMX native UAC: 48 kHz / 24-bit / 2-channel
    -> backend conversion
    -> MiniFT8 request: 12 kHz / S16 / 2-channel
```

The backend preserves channel ordering through that conversion. It does not
interpret the channel pair as stereo or I/Q.

The initial Linux WAV provider deliberately implements the smaller exact-match
case: PCM WAV input must already match the requested sample rate, S16 format, and
channel count. Later format conversion can be added without changing the ABI.

## 9. Buffer ownership

RX buffers are application-owned. MiniShell writes frames only during `read()` and
does not retain the pointer after the call returns.

TX buffers are application-owned. MiniShell reads frames only during `write()` and
does not retain the pointer after the call returns.

A future asynchronous API would require a separate explicit ownership contract;
it must not silently change these synchronous semantics.

## 10. Application lifecycle cleanup

Audio streams are associated with the current foreground application.

At normal or abnormal app teardown MiniShell performs best-effort cleanup:

```text
RX active -> stop -> close
TX active -> abort -> close
```

This prevents a crashed/exited application from leaving an audio device or
transmitter-side audio path active.

## 11. Non-responsibilities

The Audio ABI does not own or understand:

```text
FT8 / FT4 / RTTY / CW
symbol timing
CPFSK
6.25 Hz tone spacing
radio CAT commands
PTT policy
left/right semantic meaning
I/Q semantic meaning
complex DSP
application downmix policy
```

Those belong above Audio in the application or, for radio control, in the separate
Control ABI.

## 12. V1 verification requirements

Unit tests are primary and cover:

- service absent when no backend capability is present;
- RX-only, TX-only, and RX+TX capability exposure;
- `struct_size` validation;
- unsupported format handling;
- independent RX and TX lifecycles;
- frame-count rather than byte-count semantics;
- exact preservation of two-channel sample ordering;
- read-before-start / write-before-start rejection;
- finite-source `MINI_ERR_END_OF_STREAM` propagation;
- idempotent start/stop;
- bad/stale handle rejection;
- active TX abort during app teardown;
- automatic RX/TX close during app teardown.

The Linux provider integration test uses `tests/kfs16b12k.wav`, which matches the
MiniFT8 requested `12000 / S16 / 2-channel` transport format. A runtime-loaded
`audio_probe` reads the entire fixture, verifies the frame count and byte hash,
and repeats the operation in the same MiniShell session to exercise cleanup and
reopen behavior.
