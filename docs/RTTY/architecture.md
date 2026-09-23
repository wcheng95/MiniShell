# RTTY Architecture

## 1. Invariant

RTTY is a MiniShell protocol application. Its DSP and protocol logic depend on
MiniShell concepts, never directly on Linux, ALSA, ESP-IDF, USB, QMX drivers, or
another platform implementation.

```text
rtty application
      |
      v
MiniShell public API
      |
      v
MiniShell service semantics
      |
      v
private backend/provider boundary
      |
      v
Linux / Cardputer ADV / hardware / file sources
```

The same RTTY application source and decoder core must be usable on Linux and on
Cardputer ADV. Linux is the first bring-up and regression environment; ADV is a
later deployment target, not a separate implementation.

RTTY remains an **external application** on ADV. It must not be added to the
compiled-in ADV application registry.

```text
Linux development/runtime package:
    build-linux/runtime/apps/rtty.so

ADV deployment package:
    /flash/apps/rtty.elf
or
    /sd/apps/rtty.elf
```

The container/loader format is not part of the RTTY application contract.

---

## 2. Current scope

The first implementation stage is T065:

```text
docs/project/codex/T065-linux-rtty-wav-decoder.md
```

T065 establishes the portable decoder using deterministic WAV input on Linux.
Live WebSDR and QMX receive are deliberately later stages.

Initial protocol profile:

```text
sample rate     12000 Hz at the decoder-core boundary
sample format   signed 16-bit mono PCM
baud            45.45
shift           170 Hz
audio search    500..1500 Hz
sense           normal LSB
MARK            upper tone
SPACE           MARK - 170 Hz
coding          ITA2 / Baudot, 5 data bits, LSB first
line idle       continuous MARK
```

This profile is intentionally narrow **and is the MiniShell RTTY product
profile**, not merely the first entry in a future matrix. Other baud rates,
shifts, and reverse-sense protocol profiles are outside scope.

Feature depth inside this classic 45.45-baud / 170-Hz profile may grow when it
improves portable operation. TX, UI, acquisition robustness, AFC, AGC, logging,
and contest-oriented workflow are valid future work as long as they do not turn
RTTY into a comprehensive multi-profile workstation.

---

## 3. Application ownership

The RTTY application owns all RTTY-domain meaning:

- RTTY tone-pair acquisition policy;
- MARK/SPACE interpretation;
- 45.45-baud symbol timing;
- start/data/stop framing;
- ITA2 LETTERS/FIGURES state;
- decoded-character production;
- future RTTY-specific settings, tuning policy, logging, TX waveform generation,
  and operator UI.

MiniShell owns generic services:

- application lifecycle and loading;
- filesystem handles and storage semantics;
- Console/Display/Input semantics;
- Audio stream transport and lifetime;
- memory services;
- time/location services;
- serial/CDC byte-stream transport;
- platform resource ownership and cleanup.

MiniShell must not know what MARK, SPACE, Baudot, FIGS, LTRS, 170-Hz shift, or
45.45 baud mean.

---

## 4. Module boundary

The application is split into a thin MiniShell-facing edge and a portable RTTY
core.

```text
apps/rtty/
    main/
        application lifecycle
        WAV / MiniShell Audio adaptation
        Console / future UI adaptation
            |
            v
    src/
        rtty_core
        rtty_ita2
        RTTY-domain state only
```

The exact source-file names may evolve, but the ownership boundary does not.

### 4.1 Portable core

The decoder core consumes one canonical stream:

```text
12000 Hz
S16
mono
real PCM
```

Conceptually:

```c
rtty_init(...);
rtty_feed(..., const int16_t *samples, size_t count, ...);
```

The exact C API is implementation-owned, but it must preserve these properties:

- causal streaming operation;
- bounded state and bounded memory;
- no whole-recording buffer;
- no dependency on MiniShell APIs;
- no POSIX/ALSA/ESP-IDF/FreeRTOS calls;
- no required heap allocation;
- fractional 45.45-baud timing;
- portable C suitable for later ADV external-ELF packaging.

Floating-point DSP is acceptable initially.

### 4.2 Source adapters

Source conversion is outside the core.

```text
12-kHz WAV
    -> WAV adapter
    -> S16 mono
       |
       +--------------------+
                            v
                        rtty_core

MiniShell Audio / QMX       |
    -> source adapter       |
    -> S16 mono ------------+

WebSDR audio                |
    -> source adapter       |
    -> S16 mono ------------+
```

A source adapter may select or downmix channels, but it must not implement RTTY
framing or ITA2 semantics.

No resampler belongs inside `rtty_core`. The canonical RTTY core rate is
12 kHz.

---

## 5. Audio boundary

MiniShell Audio transports audio frames; RTTY assigns application meaning.

For future live MiniShell Audio use, the provider may expose 12-kHz S16 stereo,
as already used elsewhere in MiniShell. The RTTY edge converts that transport
to the core's canonical mono stream.

```text
MiniShell Audio
12 kHz / S16 / N-channel transport
            |
            | RTTY source adapter
            v
12 kHz / S16 / mono
            |
            v
        rtty_core
```

The decoder core therefore has no QMX-specific path. QMX, WebSDR, and WAV are
different sources of the same canonical audio stream.

This is a hard architectural boundary:

```text
audio transport / file parsing      adapter / MiniShell edge
RTTY signal interpretation          rtty_core
```

---

## 6. Receive DSP architecture

The initial decoder is intentionally small:

```text
12 kHz S16 mono PCM
        |
        v
500..1500-Hz tone-pair acquisition
        |
        v
locked MARK / SPACE energy detectors
        |
        v
MARK-vs-SPACE decision
        |
        v
fractional 45.45-baud timing
        |
        v
start + 5 data + stop framing
        |
        v
ITA2 LETTERS / FIGURES
        |
        v
decoded text
```

There is no waterfall, FFT dependency, HMM/Viterbi decoder, or FT8-style block
search in the first architecture.

---

## 7. Tone acquisition and lock

The decoder does not assume a fixed audio center such as 2125 Hz.

Instead it searches for normal-LSB amateur RTTY within:

```text
500 Hz <= SPACE < MARK <= 1500 Hz
MARK - SPACE = 170 Hz
```

### 7.1 Acquisition

While unlocked, use a lightweight bounded scan based on Goertzel, quadrature
correlation, or an equivalent narrowband detector.

The acquisition implementation may exploit the RTTY idle property:

```text
idle = continuous MARK
```

Therefore a strong candidate upper tone can seed:

```text
MARK  = candidate
SPACE = candidate - 170 Hz
```

Pair energy during actual data may also be used as supporting evidence.

An FFT is not required for this search and should not be introduced merely for
convenience.

### 7.2 Locked operation

Once a plausible pair is acquired, ordinary demodulation evaluates only the
selected MARK and SPACE tones. The whole 500..1500-Hz search does not continue
on every sample.

### 7.3 Reacquisition

If the locked decoder fails to obtain valid framing for a bounded interval, it
may discard the lock and return to acquisition.

The exact scan spacing, integration time, lock threshold, and reacquisition
timeout are implementation/tuning parameters, not architecture.

Continuous AFC is not required initially.

---

## 8. Timing architecture

RTTY provides a start transition for every character. The initial decoder uses
that structure instead of a general-purpose symbol timing loop.

```text
MARK idle
   |
   | MARK -> SPACE
   v
candidate start bit
   |
   +-> sample start-bit center
   +-> sample five data-bit centers
   +-> validate MARK stop region
   |
   v
accept character
```

Timing is represented fractionally:

```text
samples_per_bit = 12000 / 45.45
```

The implementation must not round the protocol to 45 or 50 baud and must not
assume integer samples per bit.

The start transition re-seeds character timing. This avoids a long free-running
bit clock and is the default architecture unless real-signal testing proves it
insufficient.

A Gardner or Mueller-and-Müller timing loop is a possible future internal
upgrade. It is not part of the initial architecture and must not change the
adapter/core boundary.

---

## 9. Framing and ITA2

The initial receive framing is:

```text
1 start bit   SPACE
5 data bits   LSB first
stop region   MARK, nominally 1.5 bits
```

The decoder should tolerate transmitters that provide a longer MARK stop region.

ITA2 state belongs above the tone/timing layer:

```text
demodulated 5-bit symbol
        |
        +-> LTRS   -> LETTERS state
        +-> FIGS   -> FIGURES state
        +-> CR/LF/space
        +-> ordinary symbol lookup
```

An invalid symbol or malformed frame must not crash the application.

USOS and other compatibility behavior are future extensions.

---

## 10. State model

A simple top-level receive state model is sufficient:

```text
UNLOCKED
   |
   | tone pair acquired
   v
LOCKED / IDLE-MARK
   |
   | MARK -> SPACE
   v
CHARACTER
   |
   | valid frame
   +-----------------> LOCKED / IDLE-MARK
   |
   | invalid frame
   +-----------------> LOCKED / IDLE-MARK
   |
   | prolonged failure / lost pair
   v
UNLOCKED
```

"Idle" is not a third modulation tone. Physically it is continuous MARK. It is
a link/framing condition.

---

## 11. WAV reference path

The first Linux implementation uses deterministic 12-kHz WAV files.

Supported T065 input is ordinary uncompressed PCM with:

- 12000-Hz sample rate;
- mono or stereo;
- 16-bit or 24-bit integer PCM.

The WAV adapter streams the file through MiniShell Filesystem, converts samples
to the core's S16-mono contract, and never loads the whole file.

Non-12-kHz files are rejected in the first implementation rather than
resampled.

The earlier `wcheng95/rtty_decoder` repository remains useful as protocol and
test research, especially its continuous-phase AFSK encoder and QMX experiments,
but its SciPy/offline decoder is not the MiniShell architecture.

Pinned historical references:

```text
repository  wcheng95/rtty_decoder

71f1803e13d72f5655b2b641125fd6239c298741
    original QMX-I/Q Python decoder

9f50ca6f0387946f7656207767fad9d3ade6d5ab
    continuous-phase RTTY AFSK encoder, validated over the air

eb4c762b72b8d1f30464d354713479474d44e605
    decoder design analysis and milestone research
```

---

## 12. Live RX evolution

After deterministic WAV acceptance, live receive adds source adapters without
redesigning the decoder.

### 12.1 QMX

```text
QMX
  -> MiniShell Audio provider
  -> RTTY audio adapter
  -> 12-kHz S16 mono
  -> rtty_core
```

QMX-specific USB/UAC ownership remains below MiniShell Audio. RTTY does not
include USB-host code.

### 12.2 WebSDR

```text
WebSDR / host audio capture
  -> host/provider adapter
  -> 12-kHz S16 mono
  -> rtty_core
```

Network/browser capture mechanism is not part of the RTTY DSP architecture.

---

## 13. Future TX boundary

RTTY TX is not part of T065, but its ownership is already clear:

```text
text
  -> ITA2 encoder
  -> start/data/stop framing
  -> continuous-phase MARK/SPACE waveform
  -> RTTY TX adapter
  -> MiniShell Audio
  -> radio/backend
```

RTTY owns symbol coding and waveform generation. MiniShell owns transport and
hardware lifecycle.

The existing research encoder demonstrated continuous-phase AFSK and is a useful
reference, but a future MiniShell TX implementation must use the MiniShell
application/service boundaries rather than Python host APIs.

---

## 14. Explicit non-goals of the initial architecture

The first decoder does not require:

- multiple baud rates;
- multiple shifts;
- reverse RTTY sense;
- AFC;
- AGC;
- squelch;
- weak-signal sequence estimation;
- HMM/Viterbi decoding;
- Gardner/M&M timing recovery;
- FFT/waterfall processing;
- full-screen UI;
- CAT control;
- TX;
- networking;
- QMX-specific DSP inside the core.

These may be added only when real testing demonstrates a need or a new product
requirement.

---

## 15. Regression principles

Every future change must preserve these architecture-level tests:

1. A deterministic 12-kHz WAV can exercise the same decoder core used by live
   sources.
2. Tone-pair location is not hard-coded; clean 170-Hz pairs at different
   positions inside 500..1500 Hz decode.
3. 45.45 baud remains fractional.
4. The decoder is causal and bounded-memory.
5. RTTY core code builds without platform headers.
6. Linux and ADV use the same RTTY-domain source.
7. ADV RTTY remains an external `rtty.elf`, not a compiled-in application.
8. Source/provider changes do not introduce QMX/WebSDR semantics into
   `rtty_core`.

This boundary is the stable foundation. DSP internals may improve after
real-signal testing without changing the MiniShell/application architecture.
