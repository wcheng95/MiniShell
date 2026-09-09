# MiniFT8-V3 RX-3 — 12 kHz Transport to 6 kHz Engine Frontend

Status: **IMPLEMENTED / CI VALIDATED**

RX-3 implements the MiniFT8-owned receive frontend between the current MiniShell Audio transport contract and the cleaned `Ft8Engine` input contract.

The governing rule remains:

> Preserve the proven MiniFT8-V2 receive behavior while cleaning ownership and interfaces. Do not mix filtering/resampling redesign into the structural migration.

## 1. Boundary

```text
MiniShell Audio transport contract
12 kHz / S16 / 2 ordered channels
        |
        v
rx_frontend
        |
        v
Ft8Engine-native stream
6 kHz / mono / float
```

`rx_frontend` is part of MiniFT8-V3. It does not call MiniShell.

The future `rx_audio_adapter` owns the MiniShell Audio stream handle and supplies bounded interleaved S16 transport blocks. RX-3 begins at the MiniFT8-owned transport block and ends at the continuous engine-native 6 kHz sample stream.

## 2. Ownership

`RxFrontend` owns only:

```text
ordinary-audio channel selection/downmix policy
S16 -> normalized float conversion
2:1 decimation phase across transport blocks
stream-reset state for that phase
```

It does not own:

```text
MiniShell Audio provider/stream lifecycle
WAV/UAC/I2S/device behavior
UTC or slot identity
960-sample engine-block accumulation
Ft8Engine state/workspace
UI / AutoSeq / TX / ADIF / radio control
```

The application-level ownership remains:

```text
rx_audio_adapter   MiniShell Audio handle + bounded read buffer
rx_frontend        channel semantics + 12k -> 6k adaptation
rx_slot_framer     slot/sample accounting + 960-sample accumulation
Ft8Engine          FT8 DSP/protocol state
```

## 3. Baseline conversion behavior

The pinned V2 production ordinary-audio conversion normalizes each S16 channel, averages the channels, and uses simple decimation. RX-3 preserves that behavior at the new MiniFT8 transport rate.

Baseline:

```text
for each 12 kHz stereo frame:
    ch0 = s16_0 / 32768.0
    ch1 = s16_1 / 32768.0
    mono = (ch0 + ch1) / 2

emit every other mono frame
```

The exact ordinary-audio operation ordering is retained in the implementation:

```c
float mono = 0.0f;
mono += (float)frame[0] / 32768.0f;
mono += (float)frame[1] / 32768.0f;
mono /= 2.0f;
```

No anti-alias FIR or other new filter is introduced in RX-3. Any future frontend/filter/resampler improvement is a separate DSP experiment after the structural RX path is stable.

## 4. Explicit channel policy

RX-3 exposes three ordinary-audio policies:

```text
RX_FRONTEND_AUDIO_AVERAGE
RX_FRONTEND_AUDIO_CHANNEL_0
RX_FRONTEND_AUDIO_CHANNEL_1
```

`AVERAGE` is the current baseline.

This does not define I/Q behavior. I/Q must remain an explicit source-profile/DSP path later; RX-3 must not silently average I/Q channels as though they were ordinary stereo audio.

## 5. Streaming semantics

The 2:1 decimation phase belongs to the `RxFrontend` instance.

```text
phase 0: next 12 kHz frame is emitted
phase 1: next 12 kHz frame is skipped
```

The phase persists across calls, so arbitrary provider/read chunk sizes do not change the continuous 6 kHz sequence.

Example:

```text
continuous frames: 0 1 2 3 4 5 ...
kept frames:       0   2   4   ...

transport calls:
[0]
[1 2 3]
[4 5]

output is still:
0 2 4
```

`rx_frontend_reset_stream()` restores phase 0 after a real stream discontinuity.

## 6. Buffer/error contract

`rx_frontend_process()` accepts caller-owned interleaved S16 input and caller-owned float output storage.

Normal operation allocates nothing.

Before consuming input, the frontend computes the exact number of output samples required from the current phase. If caller output capacity is insufficient:

```text
RX_FRONTEND_ERR_OUTPUT_FULL
```

is returned, no input is consumed, and frontend state is unchanged.

This makes retry/error handling deterministic for the future `app_controller`/framer composition.

## 7. Tests

Unit test:

```text
tests/rx_frontend_rx3_test.c
```

Coverage:

```text
baseline L/R average + 2:1 decimation
channel 0 selection
channel 1 selection
odd transport-block boundary continuity
stream reset
output-full leaves phase unchanged
invalid/uninitialized behavior
```

Pinned reference test:

```text
tests/rx_frontend_rx3_reference.c
```

The reference uses the pinned RX-1A 6 kHz golden as the authoritative engine-native sample sequence. Each 6 kHz S16 sample is represented as two identical 12 kHz stereo frames, then delivered through deliberately odd **257-frame** transport chunks.

That repeatedly cuts transport blocks between the keep/skip decimation pair and proves that block boundaries do not affect output.

Reference path:

```text
pinned 6 kHz CQ golden
    -> equivalent synthetic 12 kHz S16 stereo stream
    -> rx_frontend in 257-frame chunks
    -> 6 kHz float stream
    -> Ft8Engine
    -> Ft8ProtocolSlot
```

Expected and CI-validated result:

```text
CQ W1XYZ FN42
```

The dedicated workflow is:

```text
.github/workflows/rx3-reference.yml
```

## 8. What RX-3 deliberately does not implement

RX-3 does not add:

```text
MiniShell Audio stream ownership
real WAV/UAC/provider integration
UTC/slot timing
960-sample partial-block accumulation
multi-slot framing
I/Q DSP
anti-alias/resampling redesign
station-aware RxBatch classification
UI integration
ADV backend code
```

Those remain later boundaries.

## 9. Next stage

RX-4 owns **streaming slot framing**:

```text
continuous 6 kHz frontend samples
        |
        v
rx_slot_framer
        |
        +-- exact slot identity/sample accounting
        +-- bounded partial 960-sample accumulator
        +-- BEGIN_WINDOW / ENGINE_BLOCK / FINALIZE_WINDOW
        `-- STREAM_RESET
        |
        v
Ft8Engine
```

Per the current development rule, RX-4 should remain on the Linux backend. The real ADV backend is not involved until a hardware-specific constraint actually requires it.
