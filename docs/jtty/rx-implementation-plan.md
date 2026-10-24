# RX-only implementation plan — rpi3 first, ADV-compatible

Status: **planned / not implemented**

Date: 2026-10-23

## Goal

Build a standard-JTTY receive-only implementation inside MiniShell, with **rpi3 + QMX** as the first target platform.

The first implementation should prioritize correctness, protocol understanding, and clean architecture over aggressive RAM/CPU optimization. The DSP core must nevertheless be designed so a later **Cardputer ADV compile-in application** does not require a rewrite.

The first meaningful success criterion is:

> MiniShell on rpi3 decodes standard on-air JTTY from QMX audio and renders the recovered message correctly.

## Architecture decisions

These are considered settled for the first implementation.

### Portable DSP core

The JTTY DSP/protocol core must not depend on:

- POSIX
- ALSA
- pthreads
- ESP-IDF
- FreeRTOS
- MiniShell UI implementation details

Platform-specific scheduling and audio mechanics stay outside the portable core.

### Canonical DSP sample-rate boundary

Use:

```text
12 kHz
S16
mono
```

as the canonical JTTY DSP input boundary.

Linux/rpi3 and ADV adapters may receive audio in other native formats, but they convert to this boundary before the JTTY core.

### Soft-metric boundary

The demodulator outputs one soft-metric set per JTTY data symbol:

```text
46 symbols × 4 tone metrics
```

The TBCC decoder must know nothing about PCM or FFT implementation details.

### Frame/message separation

Keep physical-frame decoding separate from logical-message assembly:

```text
PCM
 ↓
sync / demod
 ↓
TBCC + CRC
 ↓
validated 34-bit frame payload
 ↓
source atom decode
 ↓
EOM-aware multi-frame message assembly
 ↓
UI message
```

One 1.888-second JTTY frame is not assumed to equal one complete message.

### Full standard RX source decoding

RX should support all current standard JTTY source types from the start:

- CALL actions
- STRUCT30
- TEXT5

This is important for real interoperability with desktop WSJT-X users. Real on-air traffic already includes structured CALL forms such as:

```text
LC5DGO TU
```

### Bounded work

Do not allow unbounded audio or decode queues.

Initial policy:

- one bounded decode worker/job at a time;
- continuous capture continues independently;
- if work cannot keep up, count/drop candidates rather than accumulating memory.

This mirrors the existing MiniShell design principle used in other digital-mode applications.

### UI boundary

The UI receives neutral decoded-message objects only.

DSP internals such as TBCC state, CRC details, sync score, or source bitfields must not leak into UI ownership.

## RX signal chain

```text
QMX USB-UAC
    ↓
MiniShell Audio API
    ↓
12 kHz S16 mono
    ↓
JTTY RX frontend
    ↓
sync search
    ↓
frequency/timing refinement
    ↓
46 × 4 soft tone metrics
    ↓
TBCC / WAVA decoder
    ↓
CRC-12 + reserved-bit validation
    ↓
34-bit payload
    ↓
full JTTY source decoder
    ↓
multi-frame message assembler
    ↓
decoded message object
    ↓
minimal RX display
```

## Initial search bandwidth

For the first live rpi3/QMX implementation, search:

```text
1250–1750 Hz audio
```

This is a 500 Hz window centered around 1500 Hz.

Reasons:

- wide enough to catch several nearby JTTY signals;
- much simpler than a full ~3 kHz passband search;
- keeps the first sync/acquisition implementation bounded and easy to validate.

Later expansion target:

```text
~200–2900 Hz audio
```

for a normal SSB passband.

Full-band search is explicitly **not** required for the first live decode.

## Implementation stages

### T001 — source/FEC reference core

Implement deterministic protocol components first:

- CRC-12 polynomial `0x80F`;
- K=10, memory ν=9 convolutional-code definition;
- generator polynomials `1167` / `1545` octal;
- Gray dibit-to-tone mapping;
- small TBCC encoder helper for reference tests;
- full source decoder:
  - CALL
  - STRUCT30
  - TEXT5
- 34-bit payload validation.

Acceptance:

```text
known 34-bit payload
→ CRC
→ TBCC encode
→ 46 tones
```

must match WSJT-X reference behavior.

The encoder is a test/reference helper even though the first application is RX-only.

### T002 — TBCC decoder

Input boundary:

```text
46 × 4 soft tone metrics
```

Output:

```text
validated 34-bit payload
decode metric / quality metadata
valid / invalid
```

Initial scope:

- tail-biting Viterbi / WAVA-style decoding;
- circular-path constraint;
- CRC-12;
- reserved-zero validation;
- source-validity checks.

Reference tests should cover:

- perfect metrics;
- one or more corrupted symbols;
- weak/ambiguous symbols;
- random noise rejection.

List size and candidate policy remain tunable and should be driven by testing.

### T003 — official WSJT-X sample WAV decode

Use the same official JTTY sample already verified in WSJT-X:

```text
260807_134110.wav
```

Build:

```text
12 kHz PCM
→ sync/demod
→ T002
→ standard JTTY text
```

Acceptance:

> Our JTTY engine decodes the same intended message(s) from the official sample as WSJT-X.

No QMX dependency yet.

### T004 — live rpi3/QMX narrow-window RX

Connect through MiniShell Audio and decode live QMX audio.

Initial search window:

```text
1250–1750 Hz
```

No UTC slot scheduling is needed because JTTY is free-running.

The live architecture should use:

```text
continuous capture
→ rolling sync detector
→ bounded frame candidate
→ one decode worker
→ publish result
```

### T005 — full logical-message assembly

Use standard JTTY EOM semantics:

```text
frame 1 EOM=0
frame 2 EOM=0
frame 3 EOM=1
        ↓
one logical message
```

Keep source atoms and message assembly separate.

### T006 — minimal RX UI

Initial UI should prove decoder integration only.

Example fields:

```text
UTC  SNR  Hz  MESSAGE
```

Scrolling output is sufficient initially.

The final field UI is intentionally deferred.

### Later — wider passband and multiple signals

After narrow-window live RX is solid:

- expand toward ~200–2900 Hz;
- scan for multiple sync peaks;
- generate multiple time/frequency candidates;
- decode and de-duplicate candidates;
- evaluate signal subtraction/adaptive re-decode only if field use justifies it.

## Deferred decisions

These are intentionally left open until measurements or reference tests justify a choice:

- exact sync-search bin spacing;
- exact timing/frequency refinement method;
- float vs fixed-point metrics on ADV;
- TBCC list size;
- traceback representation;
- number of simultaneous candidates;
- full-passband strategy;
- signal subtraction;
- exact rolling-audio history depth.

These are performance/implementation choices, not architecture decisions.

## Explicit non-goals for the first RX implementation

Do not include yet:

- TX
- CAT/PTT
- conversation-mode UI
- JChat conventions
- full-band multi-signal search
- signal subtraction
- logging sophistication
- automatic SOTA/POTA/callsign extraction
- ADV build validation

## Portability direction

rpi3 is the first target because it gives a forgiving Linux environment for learning and validation.

However, the portable core should be suitable later for a compile-in ADV application:

```text
same source decoder
same CRC/TBCC core
same sync/demod architecture
same 12 kHz mono DSP boundary
different platform composition
```

The rpi3 implementation must not rely on architecture choices that would force an ADV rewrite.
