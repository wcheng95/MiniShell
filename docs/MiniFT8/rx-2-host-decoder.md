# MiniFT8-V3 RX-2 — Linux Host Decoder

Status: **IMPLEMENTED / awaiting pc-1 user validation**

RX-2 is the first human-facing use harness around the completed cleaned `Ft8Engine` boundary.

It is deliberately **not** a MiniShell runtime application and does not use MiniShell Audio, Display, Input, Filesystem, or presentation profiles. It is a Linux development utility owned by the MiniFT8 project.

## 1. Purpose

RX-2 proves that the cleaned FT8 receive core is directly usable as a small understandable program:

```text
6 kHz mono S16 PCM WAV
        |
        v
host WAV reader
        |
        | streaming conversion to 6 kHz mono float
        v
Ft8Engine
        |
        v
Ft8ProtocolSlot
        |
        v
one console line per unique decoded message
```

For the pinned RX-1A/V2 reference WAV the expected output is exactly:

```text
CQ W1XYZ FN42
```

## 2. Source and build target

```text
apps/ft8/tools/ft8_decode.c
CMake target: ft8_decode
```

Typical build:

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)" --target ft8_decode
```

Run:

```bash
./build/ft8_decode <one-window.wav>
```

## 3. Input contract

RX-2 intentionally accepts only the engine-native reference format:

```text
RIFF/WAVE
PCM format 1
6000 Hz
mono
signed 16-bit little-endian
one FT8 decode window
```

The WAV reader is host-only code outside `Ft8Engine`.

Samples are streamed block-by-block. RX-2 does not load the whole WAV or whole slot into RAM.

A trailing partial engine block is ignored. This allows an ordinary 15-second WAV whose final samples do not make another complete 960-sample engine block.

If the file contains enough complete blocks to overflow the one-window `Ft8Engine` waterfall, RX-2 rejects it with an explicit message. Consecutive-slot framing belongs to RX-4.

## 4. Output contract

For every unique protocol message returned by `Ft8Engine`:

```text
canonical_text\n
```

Therefore a window containing several valid simultaneous signals may produce:

```text
CQ W1XYZ FN42
K6ABC AG6AQ -10
CQ K7XYZ DM43
```

The engine already owns candidate search, valid-payload collection, and exact 10-byte payload dedupe. The RX-2 utility simply iterates the returned `Ft8ProtocolSlot`.

If a valid protocol payload has no canonical text, the utility falls back to a compact payload/type/parse diagnostic line rather than silently hiding it.

If the window has no valid messages, RX-2 prints:

```text
No FT8 messages decoded.
```

## 5. Boundary rule

RX-2 consumes only the `Ft8Engine` edge. It must not reach into monitor, candidate, LDPC, CRC, hash-store, or message-codec implementation details.

As a small public-edge cleanup, `ft8_engine.h` now exposes engine-native aliases:

```text
FT8_ENGINE_SAMPLE_RATE_HZ
FT8_ENGINE_BLOCK_SIZE
```

The current values remain the locked RX structural baseline:

```text
6000 Hz
960 samples/block
```

## 6. Linux / ADV-profile development policy

Our current development rule remains:

```text
backend       Linux
presentation  ADV when running the MiniFT8 application UI
```

RX-2 itself has no presentation profile because it is a console development utility rather than the `ft8` MiniShell application.

When RX returns to application/UI integration, Linux continues as the backend and the 20x7 ADV presentation is used until a real ADV backend dependency must be exercised.

## 7. Validation

A dedicated GitHub workflow checks out the pinned MiniFT8-V2 baseline:

```text
5bd3ef98f72388a850bebad04bd7300b90edb63c
```

and runs:

```text
ft8_cq_w1xyz_fn42.wav
    -> ft8_decode
```

The workflow requires stdout to equal exactly:

```text
CQ W1XYZ FN42
```

The initial RX-2 CI run passed. The full Linux CI also passed on the same code-bearing head.

## 8. Exit criterion

RX-2 will be marked fully complete after the same utility is built and run successfully on `pc-1` by the user.

After that, RX-3 adds the MiniFT8-owned frontend boundary:

```text
12 kHz S16 two-channel
        -> rx_frontend
        -> 6 kHz mono float
        -> same Ft8Engine
```
