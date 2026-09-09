# JS8 DSP Notes and JS8Chat Project Idea

Status: exploratory research / future project.

These notes record the JS8 physical-layer details investigated on 2026-09-09 and define the initial idea for a future **JS8Chat** application. They are not yet a complete implementation specification.

## Source-of-truth rule

There does not appear to be a single standalone normative JS8 DSP specification comparable to a formal modem standard. The JS8Call documentation describes the implementation, but the upstream source should be treated as authoritative when details disagree.

The findings below were checked against the current `js8call/js8call` `main` source, especially:

- `JS8.cpp` — encoder/decoder, CRC, LDPC, symbol generation
- `JS8.hpp` — Costas arrays
- `JS8Submode.cpp` — submode timing, tone spacing, bandwidth
- `commons.h` — sample rates, symbol samples, periods, start delays
- `Modulator.cpp` — transmitted audio waveform
- older/reference Fortran under `lib/js8/`

Upstream repository: <https://github.com/js8call/js8call>

## Core JS8 frame

JS8 uses an 8-tone FSK waveform with 79 channel symbols.

| Parameter | Value |
| --- | ---: |
| Modulation alphabet | 8-FSK, tones 0-7 |
| Total channel symbols | 79 |
| Synchronization symbols | 21 |
| Data symbols | 58 |
| User/frame payload before type | 72 bits |
| Frame type | 3 bits |
| CRC | 12 bits |
| Information block | 87 bits |
| FEC | systematic LDPC(174,87) |
| Parity bits | 87 |
| Coded bits | 174 |
| Bits per data symbol | 3 |
| Coded-data symbols | 58 |
| JS8Call RX sample rate | 12 kHz |
| JS8Call current TX audio rate | 48 kHz |

The 87-bit information block is:

```text
72 bits  message data: 12 x 6-bit words
 3 bits  frame type
12 bits  CRC-12
-------
87 bits
```

Current `JS8.cpp` computes the CRC with Boost `augmented_crc<12, 0xc06>` and XORs the result with decimal 42. This should be reproduced from known test vectors before treating our implementation as interoperable.

The 87 information bits generate 87 LDPC parity bits. Both halves are grouped directly into 3-bit words, producing 29 parity tones and 29 information tones.

## Channel-symbol layout

Current encoder output order is:

```text
symbols   0..6    Costas A       7 symbols
symbols   7..35   LDPC parity   29 symbols
symbols  36..42   Costas B       7 symbols
symbols  43..71   information   29 symbols
symbols  72..78   Costas C       7 symbols
                              -----------
                                 79 total
```

Equivalent diagram:

```text
+----------+---------------------+----------+---------------------+----------+
| Costas A | 29 parity symbols   | Costas B | 29 information      | Costas C |
|  7 sym   | 87 parity bits      |  7 sym   | symbols / 87 bits   |  7 sym   |
+----------+---------------------+----------+---------------------+----------+
```

The current C++ encoder builds each tone by accumulating three bits into a 3-bit word and writes that value directly to the tone array. Therefore the working interpretation is direct binary tone mapping:

```text
000 -> tone 0
001 -> tone 1
010 -> tone 2
011 -> tone 3
100 -> tone 4
101 -> tone 5
110 -> tone 6
111 -> tone 7
```

This should still be covered by an encoder test against upstream JS8Call because interoperability, not nomenclature, is the goal.

## Costas synchronization arrays

Normal mode retains the original FT8 Costas sequence for all three synchronization blocks:

```text
A = 4 2 5 6 1 3 0
B = 4 2 5 6 1 3 0
C = 4 2 5 6 1 3 0
```

Slow, Fast, Turbo, and the currently disabled Ultra mode use the modified JS8 arrays:

```text
A = 0 6 2 3 5 4 1
B = 1 5 0 2 3 6 4
C = 2 5 0 6 4 1 3
```

## Submodes

The current code derives tone spacing as:

```text
tone_spacing = 12000 / symbol_samples
```

so the tone spacing equals the symbol rate. Eight tone slots therefore occupy `8 * tone_spacing` by the convention used in the JS8Call code.

| Mode | Samples/symbol @ 12 kHz | Symbol time | Symbol rate / tone spacing | Code bandwidth value | 79-symbol waveform | Period |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Slow | 3840 | 320 ms | 3.125 Hz | 25 Hz | 25.28 s | 30 s |
| Normal | 1920 | 160 ms | 6.25 Hz | 50 Hz | 12.64 s | 15 s |
| Fast | 1200 | 100 ms | 10 Hz | 80 Hz | 7.90 s | 10 s |
| Turbo | 600 | 50 ms | 20 Hz | 160 Hz | 3.95 s | 6 s |

The code also defines an Ultra mode:

| Mode | Samples/symbol @ 12 kHz | Symbol time | Symbol rate / tone spacing | Code bandwidth value | 79-symbol waveform | Period |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Ultra | 384 | 32 ms | 31.25 Hz | 250 Hz | 2.528 s | 4 s |

However, `commons.h` currently has `JS8_ENABLE_JS8I 0`, and `JS8Submode.cpp` describes Ultra as known but generally disabled. Do not make Ultra a baseline JS8Chat requirement yet.

Current nominal start delays are:

```text
Normal  500 ms
Fast    200 ms
Turbo   100 ms
Slow    500 ms
Ultra   100 ms
```

## Transmit waveform

The current JS8Call `Modulator.cpp` generates audio at 48 kHz. For each symbol it sets:

```text
tone_frequency = base_frequency + tone * tone_spacing
phase_increment = 2*pi*tone_frequency / 48000
```

The phase accumulator is not reset at symbol transitions. The implementation therefore produces a continuous-phase 8-FSK waveform at the modulator source.

No Gaussian frequency-transition shaping is visible in this current modulation path. For our purposes, describe the current waveform conservatively as **continuous-phase 8-FSK** rather than assuming FT8-style GFSK behavior.

The transmitter applies an amplitude fade near the end of the 79-symbol transmission.

## Receiver implementation observations

Current JS8Call decoding is substantially more complicated than the minimum physical-layer description above. `JS8.cpp` contains mode-parameterized synchronization/search, FFT/downsampling work, soft-bit generation, LDPC belief-propagation decoding, CRC checking, and candidate handling.

For an MCU implementation, we should not begin by porting the desktop decoder literally. Instead, use upstream behavior as the interoperability reference and design an MCU-sized decoder from first principles, similar to the approach used for MiniFT8.

Important items still to document before implementation:

- exact receive synchronization/search algorithm worth retaining on MCU
- FFT sizes and oversampling actually required for acceptable sensitivity
- soft-bit / LLR generation for the eight tones
- LDPC parity matrix representation suitable for MCU flash/RAM
- BP decoder memory and iteration budget
- frequency/time refinement strategy
- multi-signal candidate handling
- subtraction/cancellation, if any part is worth implementing
- SNR estimation
- golden JS8 audio and symbol test vectors
- encoder/decoder interoperability tests against upstream JS8Call

## Future project: JS8Chat

**JS8Chat** is a planned future MiniShell application/project.

Initial goal:

> Implement an MCU-friendly JS8 DSP engine plus a useful subset of the JS8Call messaging/networking protocol.

The project should follow the same general philosophy as MiniFT8-V3:

- portable application core
- MiniShell owns platform/hardware services
- small, understandable modules
- host/Linux reference implementation first where useful
- MCU memory and compute constraints considered from the beginning
- interoperability tested against the established desktop application
- do not blindly port a large desktop codebase

Initial conceptual split:

```text
JS8Chat
|
+-- js8_engine
|   +-- frame packing / unpacking
|   +-- CRC-12
|   +-- LDPC(174,87)
|   +-- Costas sync
|   +-- 8-FSK modulation
|   +-- synchronization / demodulation
|   +-- soft metrics + LDPC decode
|
+-- js8_protocol
|   +-- selected JS8Call frame/message types
|   +-- addressing / callsign handling
|   +-- selected directed-message functions
|   +-- selected store/forward or networking functions, if worthwhile
|
+-- js8chat_core
|   +-- application state
|   +-- conversation/message flow
|   +-- scheduling
|   +-- UI-independent behavior
|
+-- MiniShell API
    +-- audio
    +-- display/input
    +-- time/location as needed
    +-- filesystem/logging
    +-- radio/CAT path as later defined
```

### Scope rule

Do **not** attempt to reproduce all of JS8Call initially.

The first useful target should be:

1. interoperable JS8 TX/RX physical layer,
2. basic callsign-addressed text exchange,
3. only the protocol features needed to make that exchange useful,
4. add higher-level networking functions incrementally after the DSP and basic messaging are solid.

The exact messaging/networking subset remains intentionally open until the protocol is studied separately.

## Research direction

The next JS8 research step should be separate from MiniFT8-V3 work unless JS8Chat becomes active. When resumed:

1. derive golden encoder vectors from upstream JS8Call,
2. document the frame/message protocol above the 72-bit payload,
3. map the receive DSP pipeline stage by stage,
4. estimate MCU RAM/CPU cost for each stage,
5. identify what can be reused conceptually from the MiniFT8 DSP work without coupling the two application cores.
