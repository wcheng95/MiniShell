# MiniFT8 `rx_frontend`

`rx_frontend` is the MiniFT8-owned receive adaptation boundary between the current MiniShell Audio transport contract and the cleaned FT8 engine.

```text
12 kHz / S16 / 2 ordered channels
        |
        v
    rx_frontend
        |
        v
6 kHz / mono / float
```

## Ownership

`rx_frontend` owns:

- ordinary-audio channel selection/downmix policy;
- S16-to-normalized-float conversion;
- 2:1 decimation phase across arbitrary transport block boundaries;
- frontend stream reset state.

It does **not** own:

- the MiniShell Audio stream handle or provider lifecycle;
- WAV/UAC/I2S/device parsing;
- UTC or FT8 slot identity;
- 960-sample engine-block accumulation;
- waterfall/FFT/candidate/LDPC/CRC work;
- UI, AutoSeq, TX, ADIF, or radio control.

## RX-3 baseline

The baseline ordinary-audio behavior preserves the pinned MiniFT8-V2 conversion order where applicable:

```text
normalize channel 0
normalize channel 1
average L/R
simple decimation
```

At the MiniFT8-V3 transport boundary the rate conversion is 12 kHz -> 6 kHz, so the frontend emits every other normalized mono frame. No anti-alias FIR, interpolation, FFT, or other DSP redesign is introduced during RX ownership cleanup.

The explicit audio modes are:

```text
RX_FRONTEND_AUDIO_AVERAGE    baseline ordinary stereo audio
RX_FRONTEND_AUDIO_CHANNEL_0  select channel 0
RX_FRONTEND_AUDIO_CHANNEL_1  select channel 1
```

I/Q processing is **not** implemented by RX-3. A future I/Q source profile must use an explicit I/Q DSP path rather than silently treating I/Q as ordinary stereo audio.

## Streaming rule

Transport chunk size must not affect output. `RxFrontend` retains one-bit 2:1 decimation phase so an odd-sized input block may end between a kept/skipped frame pair without changing the continuous 6 kHz sequence.

`rx_frontend_reset_stream()` resets that phase after a real stream discontinuity.

`RX_FRONTEND_ERR_OUTPUT_FULL` consumes no input and leaves state unchanged.

## Tests

`tests/rx_frontend_rx3_test.c` covers normalization, average/channel policies, arbitrary chunk boundaries, reset behavior, output-capacity failure, and invalid inputs.

`tests/rx_frontend_rx3_reference.c` takes the pinned RX-1A 6 kHz golden, represents it as an equivalent 12 kHz S16 stereo stream, intentionally cuts decimation pairs with 257-frame transport chunks, then proves:

```text
rx_frontend -> Ft8Engine -> CQ W1XYZ FN42
```

Canonical stage record: `docs/MiniFT8/rx-3-frontend.md`.
