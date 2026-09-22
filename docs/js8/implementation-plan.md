# JS8Chat implementation plan

Status: **READY TO START**

This document is the implementation roadmap for the frozen JS8Chat v0.1 architecture.

The architecture itself remains defined by `architecture.md`. This file defines the staged implementation and validation sequence.

## Implementation rule

JS8Chat does **not** port the desktop JS8Call receive architecture.

Instead:

```text
implementation architecture: MiniFT8 / Ft8Engine
wire/interoperability reference: JS8Call-improved v3.0.3
```

The upstream JS8Call source is the normative behavior/reference for JS8 Normal framing, coding, message packing, JSC behavior, and interoperability. MiniFT8 is the implementation architecture for streaming audio, waterfall construction, candidate search, bounded work, ownership, and MCU resource discipline.

The goal is for JS8 Normal RX/TX to consume resources in the same general class as FT8. JSC adds flash/storage requirements later, but the PHY should not inherit the multi-megabyte desktop JS8Call decoder workspace.

## Why the MiniFT8 engine is the base

The current MiniFT8 engine operates at:

```text
engine sample rate       6000 Hz
engine block             960 samples
block duration           160 ms
FT8 symbols              79
```

JS8 Normal uses 1920 samples/symbol at 12 kHz. After the same 2:1 input reduction used by MiniFT8:

```text
1920 samples @ 12 kHz
        ->
960 samples @ 6 kHz
        ->
160 ms / symbol
```

Therefore one MiniFT8 engine block naturally corresponds to one JS8 Normal symbol.

JS8 Normal also uses 79 channel symbols, so the existing MiniFT8 waterfall/candidate architecture is the preferred starting point.

## First milestone

The first milestone is deliberately narrow:

> **Decode JS8 Normal test WAV files on Linux through a MiniFT8-style JS8 engine and recover valid JS8 frames/messages.**

No UI, chat state, heartbeat scheduler, JSC storage optimization, radio control, or ADV integration is required for this milestone.

Expected host utility shape:

```text
apps/js8chat/
    src/
        js8_engine/
            js8_engine.[ch]
            js8_monitor.[ch]
            js8_decoder.[ch]
            js8_ldpc.[ch]
            js8_crc.[ch]
            js8_frame_codec.[ch]

    tools/
        js8_decode.c
```

Exact file boundaries may evolve during implementation, but the ownership boundary must remain equivalent to MiniFT8:

```text
host WAV reader
    ->
6 kHz mono samples
    ->
Js8Engine
    ->
monitor / waterfall
    ->
candidate search
    ->
likelihood extraction
    ->
LDPC(174,87)
    ->
CRC-12
    ->
75-bit JS8 frame
    ->
minimal frame decode
    ->
stdout
```

## Engine invariants

The JS8 engine should preserve the MiniFT8 core rules:

- pure host-testable C where practical;
- no Qt, FFTW, Boost, Hamlib, Linux audio, or board dependencies;
- no MiniShell API calls inside the DSP/protocol engine;
- caller-owned/queryable workspace;
- bounded candidate storage;
- no large raw-audio slot buffer;
- no desktop-style multi-megabyte FFT workspace;
- no heap allocation inside the deployed engine;
- streaming waterfall/capture architecture;
- multi-signal decode remains a first-class goal;
- synchronous host decode may be implemented before the later bounded/incremental ADV decode service loop.

Do **not** refactor the known-good FT8 engine into a generic shared engine before JS8 decoding works. Start with a JS8 implementation derived from the FT8 architecture. Factor common DSP only after equivalence is proven and the sharing boundary is obvious.

## Milestone 1 stages

### M1A — CRC-12 and LDPC(174,87)

Implement the JS8 Normal channel-code boundary first.

JS8 Normal parameters:

```text
message bits        75
CRC                  12
information bits     87
LDPC codeword       174
data symbols         58
sync symbols         21
total symbols        79
tones                  8
```

Create golden vectors from JS8Call-improved v3.0.3:

```text
75-bit payload
    ->
CRC-12
    ->
87 information bits
    ->
LDPC(174,87)
    ->
174 encoded bits
```

Acceptance:

- exact CRC bits match v3.0.3;
- exact LDPC codeword matches v3.0.3;
- decode of the same soft/hard vector returns the exact original 75-bit payload.

### M1B — frame/tone encoder vectors

Establish the exact over-the-air symbol mapping before audio decoding.

Golden chain:

```text
75-bit payload
    ->
CRC + LDPC
    ->
58 3-bit data symbols
    ->
insert 3 x 7-symbol Costas sync
    ->
79 JS8 Normal tones
```

Acceptance:

- exact 79-tone vector matches JS8Call-improved v3.0.3;
- Costas placement and ordering are verified;
- no FT8 mapping is assumed identical without a vector comparison.

This encoder is also useful for deterministic synthetic waveform generation and later TX.

### M1C — MiniFT8-style monitor/candidate decoder

Adapt the MiniFT8 receive structure rather than upstream `DecodeMode<ModeA>`.

Preserve where applicable:

```text
6 kHz engine rate
960-sample blocks
streaming waterfall
KissFFT
time/frequency oversampling
bounded candidate list
candidate scoring
soft-symbol extraction
LDPC attempt
CRC validation
multi-signal candidate handling
```

Only change parameters/algorithms required by JS8 Normal.

The upstream desktop decoder's large raw PCM buffer, giant FFT arrays, Qt objects, FFTW plans, and desktop threading model are explicitly out of scope.

### M1D — exact payload from WAV

Before requiring human-readable application text, prove the PHY boundary.

For each decoded signal report enough diagnostics to validate:

```text
frequency
time offset
SNR/quality where available
exact 75-bit payload
CRC result
```

Acceptance:

- at least one pinned JS8 Normal WAV yields the exact expected payload;
- LDPC and CRC both pass;
- candidate timing/frequency are plausible;
- no upstream JS8Call DSP library is linked into the MiniShell implementation.

### M1E — Linux `js8_decode` utility

Create a host utility parallel to:

```text
apps/ft8/tools/ft8_decode.c
```

Target usage:

```sh
./build/js8_decode <normal-mode.wav>
```

The first useful parser only needs enough JS8 frame/application decoding to print the expected message(s) from the selected fixture. Full HB, FIRST/LAST, directed chat, Huffman, JSC, compound calls, and conversation state are later stages.

Milestone 1 is complete when:

```text
WAV
 -> candidate found
 -> LDPC(174,87) passes
 -> CRC-12 passes
 -> exact 75-bit payload matches reference
 -> expected JS8 text/frame is emitted
```

and the host test is automated under CTest.

## Upstream WAV regression corpus

JS8Call-improved v3.0.3 already contains rudimentary decoder WAV tests under:

```text
media/tests/
```

The upstream naming convention is:

```text
{MODE}_{DEPTH}_{EXPECTED_DECODES}.wav
```

The upstream README states these are intended to be run directly through the JS8 decoder.

For Normal mode (`A`), v3.0.3 provides:

```text
A_1_4.wav
A_2_1.wav
A_2_3.wav
A_2_5.wav
A_2_6.wav
A_2_9.wav
A_3_3.wav
```

The final number is the expected number of decodes for the upstream test configuration.

These should be the preferred initial external regression corpus. Start with the simplest one-decode fixture:

```text
media/tests/A_2_1.wav
```

Then progress through multi-signal fixtures, especially:

```text
A_2_3.wav
A_2_5.wav
A_2_6.wav
A_2_9.wav
```

The multi-signal files are valuable because multi-decode is part of the frozen JS8Chat v0.1 scope and should be preserved from the start rather than added after a single-signal decoder architecture has hardened.

Do not copy the upstream files into MiniShell unless we decide that repository size/licensing/test-fixture policy warrants it. The implementation may initially reference a locally checked-out pinned v3.0.3 source tree or use a separate fixture-fetch step. If any fixture is checked into MiniShell later, document its upstream tag/path and license provenance.

## Resource checkpoint after Milestone 1

Immediately after the Linux WAV decoder works, measure:

```text
sizeof(Js8Engine)
workspace bytes
waterfall bytes
candidate storage
LDPC scratch
largest stack requirement
host decode time
```

Compare directly with `Ft8Engine`.

Expected architectural result:

```text
JS8 Normal PHY RAM ~= FT8 PHY RAM class
```

A large divergence is a reason to inspect the implementation before proceeding to ADV.

JSC is intentionally excluded from this PHY RAM comparison. Its dictionary primarily adds read-only storage/flash requirements.

## After Milestone 1

The next implementation sequence is:

```text
M2  multi-signal Linux WAV regression
    -> prove expected decode counts against upstream A_* fixtures

M3  full JS8 frame/application codec subset
    -> standard calls
    -> compound calls
    -> CQ / CQ FIELD
    -> HB / HB ACK
    -> directed commands
    -> FIRST / LAST
    -> ACK / 73

M4  Huffman + JSC
    -> RX equivalence
    -> TX lookup equivalence
    -> packed resource measurement

M5  conversation/application core
    -> heard/reachable state
    -> per-peer histories
    -> independent RX reassembly contexts
    -> unread state
    -> TX scheduler

M6  MiniShell integration
    -> shared Audio/Time/Radio/Storage services
    -> 20x7 UI
    -> logging

M7  ADV resource/performance validation
    -> workspace measurement
    -> bounded decode scheduling
    -> USB-UAC live RX

M8  on-air interoperability
    -> JS8Call-improved v3.0.3 reference station
```

## Reference freeze

Normative interoperability source:

```text
repository: JS8Call-improved/JS8Call-improved
tag:        v3.0.3
```

Current upstream `master` may be used as a secondary compatibility check but must not silently change v0.1 behavior.

The implementation should use v3.0.3 for golden vectors, expected frame behavior, and the initial WAV regression corpus.
