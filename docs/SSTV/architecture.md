# SSTV architecture for MiniShell

Status: ARCHITECTURE ONLY — implementation deferred until RTTY is complete and accepted.

This document records the architecture decisions for adding SSTV to MiniShell. It is
not a Codex implementation task. Do not start SSTV code, create an SSTV task branch,
or change the MiniShell public API from this document alone.

The current implementation priority remains RTTY (T065-linux-rtty-wav-decoder.md).
SSTV work may begin only after T065 is accepted and the architect explicitly opens a
new implementation task.

## Goal

Add SSTV as another portable MiniShell radio application while preserving the same
architecture boundary used by MiniFT8, Mini-CW, JS8, and RTTY:

~~~text
audio/file source
    |
    v
SSTV application
    |
    +--> pure portable SSTV RX core
    |
    +--> MiniShell Filesystem / Console / future raster Display
    |
    v
Linux runtime module now
ADV external ELF later
~~~

The first useful path is receive-only and file-oriented:

1. Linux WAV decode first.
2. Validate the decoder against deterministic and independent SSTV test audio.
3. Add live QMX Audio RX in a later task without changing the decoder core.
4. Add ADV external-ELF packaging in a later task.
5. Add live image preview only after MiniShell has a suitable portable raster Display
   capability.

## Decision 1 — SSTV is an external application

Runtime command:

~~~text
sstv
~~~

Linux may package it as:

~~~text
build-linux/runtime/apps/sstv.so
~~~

The eventual ADV artifact is:

~~~text
/flash/apps/sstv.elf
or
/sd/apps/sstv.elf
~~~

Do not add SSTV to the ADV compiled-in application registry.

SSTV protocol/DSP/image code belongs under apps/sstv/. Platform-specific USB,
ALSA, FATFS, LCD, ESP-IDF, or board code must not enter the SSTV core.

## Decision 2 — fixed decoder boundary is 12 kHz S16 mono

The portable RX core consumes:

~~~text
sample rate:   12000 Hz
sample format: signed 16-bit
channels:      1
signal:        ordinary real audio
~~~

Reasons:

- MiniShell/QMX already uses a 12 kHz audio path.
- SSTV signaling occupies roughly the 1100..2300 Hz region, comfortably inside a
  12 kHz sample rate.
- Keeping one fixed rate removes resampling from the embedded decoder.
- Martin M1 timing is intentionally non-integer at 12 kHz, which forces the design
  to handle fractional timing correctly rather than depending on rounded sample
  counts.

The SSTV core contains no resampler.

A WAV adapter may accept PCM16 or PCM24, mono or stereo, but the WAV sample rate for
the first implementation must be exactly 12000 Hz. Stereo is downmixed before the
core.

Live QMX Audio RX later feeds the same 12 kHz S16-mono boundary.

## Decision 3 — RX first; TX is outside the initial architecture

The first SSTV implementation is decode-only.

No SSTV transmit encoder, QMX CAT/PTT sequence, audio TX, or direct RF tone
generation is part of the first SSTV work.

TX will be designed separately after RX is operational. This avoids coupling receive
architecture to an unvalidated transmit path.

## Decision 4 — Martin M1 is the first decoded mode

The first implementation target is Martin M1:

~~~text
VIS:             44 / 0x2C
image:           320 x 256
color order:     Green -> Blue -> Red
sync:            1200 Hz, 4.862 ms
porch/separator: 1500 Hz, 0.572 ms
one color scan:  146.432 ms
pixel time:      457.6 us
video mapping:   1500 Hz black .. 2300 Hz white
~~~

Martin M1 is a good first target because:

- it is widely supported;
- its RGB/GBR organization is simpler than Robot Y/chroma reconstruction;
- 320-pixel rows fit a very small line-buffer architecture;
- it exercises all important SSTV problems: VIS, frequency demodulation, fractional
  pixel timing, horizontal sync, clock/slant error, and long-duration streaming.

The architecture must be mode-table driven from the beginning. Martin M1 is the only
mode required by the first implementation, but adding another mode must not require
rewriting the audio demodulator, VIS detector, image sink, or application shell.

Expected later mode order:

~~~text
Martin M1
Robot 36
Scottie S1
PD120
then additional modes as useful
~~~

This is a roadmap, not an implementation commitment.

## Decision 5 — mode descriptions are data, not duplicated decoders

Use a static mode descriptor/table for protocol timing and image organization.

Conceptually each mode supplies:

~~~text
VIS code
width / height
color model
line organization
sync location and duration
porch/separator durations
scan durations
channel/component order
pixel count per scan
~~~

The generic receiver owns:

~~~text
VIS acquisition
frequency demodulation
frequency-offset correction
sync detection
fractional sample clock
line clock/slant tracking
pixel sampling
image-sink calls
~~~

Mode-family code owns only the reconstruction rules that genuinely differ, for
example GBR versus Robot Y/R-Y/B-Y.

Do not implement one unrelated decoder state machine per SSTV mode.

## Decision 6 — streaming frequency demodulation; no FFT dependency

SSTV is treated as a continuously varying audio-frequency signal.

The preferred portable demodulator is:

~~~text
12 kHz real PCM
    -> light DC/amplitude conditioning
    -> quadrature mix around the SSTV video center
    -> low-pass I/Q
    -> phase-difference / FM discriminator
    -> instantaneous-frequency stream + confidence
~~~

The nominal video center is 1900 Hz, with useful image tones at 1500..2300 Hz.
VIS/sync detection also needs 1100, 1200, 1300, and 1900 Hz recognition.

The implementation may use floating point first. Optimize only if ADV measurement
shows it is necessary.

Do not introduce FFTW, liquid-dsp, OpenCV, SciPy, or another large runtime DSP/image
dependency. Do not require an FFT waterfall to decode SSTV.

The demodulator must be causal and bounded-memory.

## Decision 7 — VIS is the normal acquisition path

Normal receive state flow:

~~~text
HUNT
  -> 1900-Hz leader
  -> 1200-Hz break
  -> leader
  -> VIS start/data/parity/stop
  -> supported mode
  -> image receive
  -> complete / reacquire
~~~

VIS decode uses the standard tone meanings:

~~~text
VIS 1:  1100 Hz
sync:   1200 Hz
VIS 0:  1300 Hz
leader: 1900 Hz
~~~

The receiver validates start/stop and parity before accepting the mode.

The first implementation may provide an explicit Martin-M1 force option for damaged
or synthetic headers, but automatic VIS detection is the normal path.

An unsupported but valid VIS code should be reported cleanly rather than decoded
with guessed timing.

## Decision 8 — calibrate frequency offset from known SSTV tones

Real SSB reception can shift the whole SSTV audio spectrum when tuning is slightly
off. A decoder that assumes exact 1500/2300-Hz video tones will introduce brightness
and color errors.

The receiver therefore maintains one small frequency-offset estimate derived from
known protocol tones, primarily the 1900-Hz leader and 1200-Hz sync pulses.

Conceptually:

~~~text
corrected_hz = measured_hz - offset_hz
~~~

This is a global/slow correction, not a complex AFC loop.

The same corrected frequency stream feeds VIS and image decoding.

## Decision 9 — all timing is fractional and cumulative

At 12 kHz, Martin M1 durations are not integer sample counts. For example one pixel
is approximately 5.4912 samples.

Never round every pixel or segment independently.

Use a fractional sample/time accumulator, or an equivalent cumulative-time
representation, so timing is derived from absolute/cumulative positions.

Pixel values are sampled/integrated over their fractional windows. The exact
interpolation/filter implementation may be selected during the first implementation
task, but it must not collapse the protocol onto integer samples per pixel.

## Decision 10 — horizontal sync closes the timing loop

VIS identifies the mode; it does not provide enough timing stability for an
approximately two-minute image.

For Martin M1, every line's 1200-Hz sync pulse is used to correct line phase and
slow sample-clock/slant error.

The decoder maintains:

~~~text
predicted next sync position
observed sync position
line phase error
slow line-period / sample-clock correction
~~~

Use a small tracking loop or equivalent bounded estimator. Do not require storing the
whole raw audio transmission and re-rendering it later.

If one sync pulse is missed, the decoder may continue briefly from the predicted line
clock. Repeated missing/invalid sync eventually terminates the image or returns to
acquisition.

This is intentionally a streaming slant-correction architecture.

## Decision 11 — no full-resolution image framebuffer in the core

ADV/Cardputer has no PSRAM, so the architecture must not depend on a complete
320x256 RGB framebuffer.

For Martin M1, keep only the current row's component data:

~~~text
G[320]
B[320]
R[320]
~~~

plus small DSP/timing state.

After the red scan completes, combine the three channels into one RGB row, pass that
row to an image sink, and reuse the buffers for the next line.

This keeps the core suitable for ADV and also makes Linux exercise the embedded
memory model.

Future Robot/PD modes may require a small number of line buffers for chroma
reconstruction, but still no whole-image buffer.

## Decision 12 — image output uses a sink interface

The decoder core does not know about BMP files, Linux windows, Cardputer LCDs, or
WebFS.

It emits metadata and completed RGB scanlines to an image sink:

~~~text
begin(mode, width, height)
row(y, RGB data)
end(status)
~~~

This separation permits the same core to drive:

- a file writer;
- a future MiniShell raster Display sink;
- tests that capture rows in memory;
- optional future network/export sinks.

## Decision 13 — first persistent image format is uncompressed 24-bit BMP

For the first Linux WAV decoder, write an uncompressed 24-bit BMP.

Use top-down BMP row order so completed SSTV rows can be written sequentially as they
arrive. No full image buffer is needed.

Reasons for BMP rather than PNG/JPEG in the first implementation:

- trivial bounded-memory writer;
- no compression library;
- widely viewable;
- deterministic output for tests;
- maps directly from RGB scanlines.

PNG/JPEG may be added later only if there is a practical reason.

Initial file-oriented command shape:

~~~text
sstv <input.wav> <output.bmp>
~~~

No live Audio endpoint or CAT option belongs in the first SSTV task.

## Decision 14 — current MiniShell Display API is intentionally insufficient

MiniShell API v3 currently exposes text Display operations only. SSTV must not call
Cardputer/M5Stack/ESP-IDF graphics APIs directly to work around that boundary.

Therefore the first SSTV implementation has no graphical MiniShell preview.

A later independent runtime task should add a portable raster Display capability.
The required shape is scanline/span oriented, not a mandatory full framebuffer.
SSTV should be able to submit RGB565 or equivalent pixel spans incrementally so the
ADV provider can draw the image without allocating a full source image.

The SSTV core must not need modification when that future Display sink is added.

## Decision 15 — ADV preview will be downscaled while streaming

When raster Display support exists, ADV preview should be produced incrementally from
decoded rows.

Do not allocate a 320x256 display framebuffer just to scale it to the Cardputer
screen.

The preview sink may:

1. map source rows to destination rows;
2. scale 320 source pixels to the available display width;
3. convert RGB888 row data to the Display provider's portable raster format;
4. draw/present incrementally.

The saved BMP remains full decoded resolution.

## Decision 16 — Linux WAV bring-up must mirror the future live path

First implementation flow:

~~~text
12 kHz WAV
    -> WAV streaming adapter
    -> exact same S16-mono SSTV core used later by Audio RX
    -> BMP image sink
~~~

Later live flow:

~~~text
MiniShell Audio RX (QMX)
    -> same S16-mono SSTV core
    -> BMP sink
    + future raster preview sink
~~~

Do not create a special offline NumPy/SciPy decoder that would later be replaced for
ADV.

## Expected first implementation boundaries

When SSTV implementation is eventually opened, the approximate ownership should be:

~~~text
apps/sstv/
    README.md
    main/
        application / arguments
        WAV adapter
        BMP sink
    src/
        streaming demodulator
        VIS detector
        timing/sync tracker
        mode table
        Martin M1 reconstruction
        image-sink interface
~~~

Exact filenames are intentionally not fixed by this architecture note.

The pure src/ layer must not call POSIX, ALSA, ESP-IDF, FreeRTOS, MiniShell
Filesystem, MiniShell Display, or platform code.

## Future validation strategy

The first implementation task should include deterministic synthetic vectors and at
least one independently generated/reference SSTV recording.

Required categories should eventually include:

- valid Martin M1 VIS decode;
- parity/header rejection;
- 12 kHz PCM16 mono WAV;
- 12 kHz PCM24 stereo WAV adaptation;
- known gray ramp -> expected frequency/brightness mapping;
- known RGB bars -> correct G/B/R reconstruction;
- fractional 457.6-us pixel timing without cumulative shear;
- small carrier/audio-frequency offset corrected from protocol tones;
- sample-clock mismatch/slant corrected from repeated line sync;
- one or more dropped/noisy sync pulses;
- malformed/unsupported WAV rejection;
- valid unsupported VIS code reported cleanly;
- no whole-file audio buffer;
- no whole-image framebuffer requirement.

Internet downloads must not be required by the automated test suite.

## Explicit non-goals for the first SSTV implementation

- SSTV TX.
- CAT/PTT.
- QMX live audio.
- WebSDR/network capture.
- ADV ELF packaging.
- ADV hardware execution.
- graphical MiniShell UI.
- MiniShell public API changes.
- PNG/JPEG compression.
- all SSTV modes.
- whole-file FFT processing.
- whole-image framebuffer.
- direct Cardputer/LCD access from the application.
- weak-signal contest-grade DSP.
- OCR/callsign recognition.
- image post-processing or denoising.

## Reference material

Architecture and protocol decisions were cross-checked against these public
implementations/references; they are references, not runtime dependencies:

- JL Barber, N7CXI, "Proposal for SSTV Mode Specifications" ("Dayton paper").
- unexcellent/sstv: table-driven multi-mode encoder/decoder intended for
  microcontrollers.
- colaclanth/sstv: file-oriented Martin/Scottie/Robot decoder.
- F4JTV/sstv_decoder: streaming receive architecture with VIS detection and
  line-sync-based slant correction.
- JO3ALT/TinySSTV: compact Martin M1 timing reference and ESP32 implementation.
- SSTV Handbook / published VIS tables.

These references should be used to cross-check protocol timing. New MiniShell SSTV
code should remain independently structured around MiniShell's portable API and
embedded memory constraints.

## Implementation gate

SSTV remains documentation-only now.

Do not open the implementation task until:

1. RTTY T065 is complete and accepted.
2. The architect reviews this SSTV architecture after the RTTY experience.
3. The architect explicitly selects the first SSTV implementation task.

At that point, the first bounded task should be Linux-only:

~~~text
12 kHz WAV -> Martin M1 -> 24-bit BMP
~~~

Everything else remains later work.
