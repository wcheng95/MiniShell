# SSTV architecture for MiniShell

Status: ARCHITECTURE ONLY — implementation deferred until RTTY is complete and accepted.

This document records the architecture decisions for adding SSTV to MiniShell. It is
not a Codex implementation task. Do not start SSTV code, create an SSTV task branch,
or change the MiniShell public API from this document alone.

The current implementation priority remains RTTY (T065-linux-rtty-wav-decoder.md).
SSTV work may begin only after T065 is accepted and the architect explicitly opens a
new implementation task.

## Goal

Add SSTV as one of MiniShell's five portable QRP operating modes.

The primary use case is a portable/POTA image QSO rather than a general-purpose SSTV
workstation:

~~~text
take park photo on iPhone
    |
    v
transfer by WebFS or USB MSC
    |
    v
ADV prepares 320x240 SSTV image
    |
    +--> crop/scale
    +--> overlay:
    |       CQ POTA AG6AQ
    |       US-3473
    |
    v
Robot 36 TX through QMX
    |
    v
receive SSTV reply
    |
    +--> save image
    +--> inspect callsign manually
    +--> manually log QSO
    |
    v
optional QSL-card-style closing image
~~~

The design should remain small and purpose-driven. MiniShell does not need to become a
universal SSTV workstation.

The portable software boundary remains:

~~~text
audio/file/image source
    |
    v
SSTV application
    |
    +--> pure portable SSTV codec/image core
    |
    +--> MiniShell Filesystem / Audio / Display
    |
    v
Linux runtime module for bring-up
ADV external ELF for field operation
~~~

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

SSTV protocol/DSP/image code belongs under apps/sstv/. Platform-specific USB, ALSA,
FATFS, LCD, ESP-IDF, or board code must not enter the SSTV core.

## Decision 2 — fixed audio boundary is 12 kHz S16 mono

The portable RX core consumes and the portable TX core produces:

~~~text
sample rate:   12000 Hz
sample format: signed 16-bit
channels:      1
signal:        ordinary real audio
~~~

Reasons:

- MiniShell/QMX already uses a 12 kHz audio path.
- SSTV signaling fits comfortably inside a 12 kHz sample rate.
- Keeping one fixed rate removes resampling from the embedded codec.
- Linux tests and ADV use the same audio boundary.

The SSTV core contains no resampler.

A WAV adapter may accept PCM16 or PCM24 and mono or stereo, but the WAV sample rate
for the first implementation must be exactly 12000 Hz. Stereo is downmixed before the
core.

Live QMX Audio RX/TX later uses the same 12 kHz S16-mono boundary.

## Decision 3 — Robot 36 is the first operating mode

The first complete MiniShell SSTV mode is Robot 36, for both RX and TX.

Robot 36 is selected because:

- it is widely supported by phone and desktop SSTV software;
- its roughly 36-second transmission is practical for portable/POTA use;
- it produces a useful 320x240 color image;
- it is already implemented by PicoSSTV, our primary MCU reference;
- implementing its Y/chroma reconstruction exercises the important embedded codec
  path without requiring many modes.

Robot 36 is the only mode required for the first field-capable implementation.

Martin M1 is a reasonable later addition, especially as a cross-check against a
different RGB-family protocol. Scottie and PD modes remain optional. Mode count is not
a goal by itself.

## Decision 4 — SSTV is a POTA image-QSO application

The intended exchange is deliberately simple:

1. Operator transfers a park photograph to ADV.
2. ADV prepares a transmit image with callsign and POTA reference.
3. Operator calls CQ using SSTV.
4. A received reply containing a recognizable callsign is sufficient for the operator
   to log the contact manually.
5. A QSL-card-like closing exchange may be sent when desired.

Automatic callsign recognition, automatic ADIF logging, or a mandatory closing image
is not required.

The software must not require a QSL-card exchange for a valid operating workflow.

## Decision 5 — canonical working image is 320x240

The first implementation uses a 320x240 RGB working image.

The iPhone source image does not need to be pre-sized exactly. ADV owns the final image
preparation step:

~~~text
source JPEG/PNG
    -> choose/crop 4:3 region
    -> scale to 320x240
    -> add text overlay
    -> save prepared image
    -> transmit
~~~

This keeps phone-side preparation minimal and makes the on-air image deterministic.

The first overlay is intentionally simple:

~~~text
CQ POTA AG6AQ
US-3473
~~~

The park reference is runtime data, not compiled into the application.

Future optional metadata may include UTC, band, or a short caption, but the MVP does
not require it.

## Decision 6 — source images arrive through existing MiniShell file paths

The normal portable source is a photo taken on iPhone and transferred to ADV using an
existing MiniShell mechanism:

- WebFS; or
- USB MSC.

Do not add a camera subsystem to SSTV.

Suggested storage ownership:

~~~text
/flash/sstv/
    inbox/          transferred source images
    tx/             prepared 320x240 images
    rx/             received images
    qsl/            optional reusable closing/QSL images
~~~

Exact filenames and retention policy belong to implementation tasks.

## Decision 7 — BMP is the authoritative prepared/received image

The authoritative SSTV image copy is a file, not LCD RAM and not an ESP32 framebuffer.

For the first implementation, use uncompressed BMP where practical because it permits
simple bounded-memory row access and deterministic tests.

The file copy is authoritative:

~~~text
BMP/file image = persistent truth
LCD controller GRAM = disposable display cache
ESP32 SRAM = small working buffers only
~~~

If the display is reset, another MiniShell app takes over the LCD, or LCD contents are
lost, the image can be reconstructed from the file without affecting the SSTV codec.

JPEG/PNG decoding may be supported for transferred source photographs, but the SSTV
codec must not depend on JPEG/PNG compression libraries internally.

## Decision 8 — image processing and SSTV codec are separate layers

Keep three distinct layers:

~~~text
image pipeline
    load / crop / scale / text overlay

SSTV codec
    Robot 36 encode / decode
    VIS / timing / tone mapping

application/UI
    file selection / RX / TX / image viewer / operator controls
~~~

The SSTV codec receives or emits scanlines/components. It does not know how the source
photo was cropped or how the UI selected the file.

This separation also allows later QSL templates or another SSTV mode without changing
the core DSP.

## Decision 9 — mode descriptions are data, not duplicated decoders

Use static mode descriptors/tables for protocol timing and image organization.

Conceptually each mode supplies:

~~~text
VIS code
width / height
color model
line organization
sync location and duration
porch/separator durations
scan durations
component order
pixel/component counts
~~~

The generic receiver owns:

~~~text
VIS acquisition
frequency demodulation
frequency-offset correction
sync detection
fractional sample clock
line clock/slant tracking
pixel/component sampling
image-sink calls
~~~

Mode-family code owns only reconstruction rules that genuinely differ, such as Robot
Y/chroma handling versus Martin RGB/GBR handling.

Do not implement one unrelated decoder state machine per SSTV mode.

## Decision 10 — streaming frequency demodulation; no FFT dependency

SSTV is treated as a continuously varying audio-frequency signal.

The preferred portable RX demodulator is:

~~~text
12 kHz real PCM
    -> light DC/amplitude conditioning
    -> quadrature mix around the SSTV video center
    -> low-pass I/Q
    -> phase-difference / FM discriminator
    -> instantaneous-frequency stream + confidence
~~~

The implementation may use floating point first. Optimize only if ADV measurement
shows it is necessary.

Do not introduce FFTW, liquid-dsp, OpenCV, SciPy, or another large runtime DSP/image
dependency. Do not require an FFT waterfall to decode SSTV.

The demodulator must be causal and bounded-memory.

## Decision 11 — VIS is the normal acquisition path

Normal receive state flow:

~~~text
HUNT
  -> leader/break sequence
  -> VIS
  -> supported mode
  -> image receive
  -> complete / reacquire
~~~

The receiver validates the VIS framing/parity before accepting the mode.

An unsupported but valid VIS code should be reported cleanly rather than decoded with
guessed timing.

A force-mode test option may exist for damaged or synthetic test vectors, but normal
portable operation uses VIS.

## Decision 12 — frequency offset and timing are continuously corrected

Real SSB reception can shift the whole SSTV audio spectrum when tuning is slightly
off. The receiver therefore maintains a small frequency-offset estimate derived from
known protocol tones.

Conceptually:

~~~text
corrected_hz = measured_hz - offset_hz
~~~

The same corrected frequency stream feeds VIS and image decoding.

All protocol timing is cumulative/fractional. Never round each pixel, component, or
segment independently when the nominal duration is not an integer number of 12 kHz
samples.

Horizontal synchronization closes the line timing loop. The decoder tracks predicted
versus observed sync position and applies slow line-period/sample-clock correction to
avoid image slant.

One missed sync may be bridged from predicted timing. Repeated invalid sync eventually
terminates the image or returns to acquisition.

## Decision 13 — no full-resolution image framebuffer in ESP32 SRAM

ADV/Cardputer has no PSRAM. SSTV must not depend on a 320x240 RGB/RGB565 framebuffer in
ESP32 SRAM.

A 320x240 RGB565 buffer alone would consume 153600 bytes, so the normal architecture
uses only small line/component/work buffers.

RX conceptually operates as:

~~~text
audio
    -> decode current Robot component/line
    -> reconstruct completed image row(s)
    -> write persistent BMP
    -> write display path
    -> reuse working buffers
~~~

TX conceptually operates as:

~~~text
prepared BMP
    -> read required row/component data
    -> Robot 36 encoder
    -> 12 kHz S16 audio
~~~

Future modes may require a small number of line buffers for chroma reconstruction, but
still no whole-image ESP32 framebuffer.

Linux should exercise the same bounded-memory model rather than taking advantage of a
desktop-sized full-image allocation inside the codec.

## Decision 14 — image output uses a sink interface

The decoder core does not know about BMP files, Linux windows, Cardputer LCDs, WebFS,
or USB MSC.

It emits metadata and completed RGB scanlines to an image sink:

~~~text
begin(mode, width, height)
row(y, RGB data)
end(status)
~~~

The same decoded row may feed more than one sink.

Expected sinks include:

- persistent BMP/file sink;
- ADV LCD/display sink;
- tests that capture selected rows;
- optional future export/network sinks.

## Decision 15 — use LCD controller GRAM as the ADV display-side image cache

ADV's visible LCD is 240x135 while the ST7789-class controller has substantially more
internal frame memory.

The SSTV display path should exploit controller GRAM rather than allocate a second
full image in ESP32 SRAM.

During RX or image preparation:

~~~text
completed image data
    +--> persistent BMP
    +--> LCD controller GRAM
~~~

The intent is to write the image into controller RAM once while it is being produced.
Arrow-key browsing should then move a 240x135 viewport over the stored 320x240 image
without rebuilding the image in ESP32 RAM.

Controls:

~~~text
Left / Right    pan horizontally
Up   / Down     pan vertically
~~~

For a 320x240 source and 240x135 visible area, the conceptual viewport origin is:

~~~text
x = 0 .. 80
y = 0 .. 105
~~~

Exact ST7789 register mapping, rotation, offsets, and whether both axes can be moved
purely through controller addressing/scroll state are hardware-driver validation
items. The architecture requirement is:

- use controller GRAM as display-side storage where possible;
- do not allocate a 153600-byte ESP32 framebuffer merely for viewing;
- do not make LCD readback part of the normal image-storage architecture;
- keep the BMP/file copy authoritative.

If one pan direction ultimately needs data to be reissued because of controller
scanout limitations, that is a display-provider detail and must not change the codec
or persistent-image architecture.

## Decision 16 — TX is streaming and does not need a synthesized whole-audio buffer

The Robot 36 encoder reads prepared image data incrementally and emits 12 kHz S16 mono
audio incrementally.

Do not build the complete approximately 36-second waveform in RAM.

The TX core owns protocol tone/timing generation. QMX CAT/PTT and physical audio output
belong to the MiniShell/application adapter.

The TX path should eventually follow the same ownership model already established by
other MiniShell radio applications:

~~~text
prepared image
    -> SSTV encoder
    -> MiniShell Audio TX
    -> QMX
~~~

Physical TX state, pause/resume behavior, and QMX CAT sequencing are integration tasks,
not SSTV protocol logic.

## Decision 17 — Linux bring-up mirrors ADV

Linux tests should exercise the same codec and bounded-memory paths intended for ADV.

Useful first flows are:

~~~text
12 kHz WAV
    -> Robot 36 decoder
    -> BMP
~~~

and:

~~~text
320x240 BMP
    -> Robot 36 encoder
    -> 12 kHz WAV
~~~

These permit deterministic round-trip and independent-reference tests without QMX or
ADV hardware.

Later live paths are:

~~~text
MiniShell Audio RX (QMX)
    -> same decoder
    -> BMP + display sink
~~~

and:

~~~text
prepared BMP
    -> same encoder
    -> MiniShell Audio TX (QMX)
~~~

Do not create a special NumPy/SciPy implementation that is later replaced on ADV.

## Decision 18 — manual logging is sufficient for the first field version

A received SSTV reply does not need OCR or automatic callsign extraction.

If the operator can recognize the remote callsign in the received image, the QSO may
be logged manually.

Therefore the first field version does not require:

- OCR;
- callsign database lookup;
- automatic ADIF creation;
- automatic POTA upload;
- automatic QSL-card generation.

Those features may be considered later only if field operation demonstrates a real
need.

## Expected implementation boundaries

When SSTV implementation is eventually opened, approximate ownership should be:

~~~text
apps/sstv/
    README.md
    main/
        application / UI
        WAV adapter
        BMP/source-image adapters
        MiniShell Audio adapter
        MiniShell Display adapter
    src/
        streaming demodulator
        VIS detector
        timing/sync tracker
        mode table
        Robot 36 reconstruction
        Robot 36 encoder
        image-sink interface
        bounded-memory image helpers
~~~

Exact filenames are intentionally not fixed by this architecture note.

The pure src/ layer must not call POSIX, ALSA, ESP-IDF, FreeRTOS, MiniShell
Filesystem, MiniShell Display, or platform code.

## Validation strategy

The implementation should include deterministic synthetic vectors and independently
generated/reference SSTV recordings/images.

Required categories should eventually include:

- valid Robot 36 VIS decode;
- parity/header rejection;
- 12 kHz PCM16 mono WAV;
- 12 kHz PCM24 stereo WAV adaptation;
- known gray/color patterns -> expected reconstruction;
- Robot alternating chroma handling;
- small carrier/audio-frequency offset corrected from protocol tones;
- sample-clock mismatch/slant corrected from repeated line sync;
- one or more dropped/noisy sync pulses;
- Robot 36 encode timing;
- independent decoder successfully decodes MiniShell-generated Robot 36 WAV;
- MiniShell decoder successfully decodes independent Robot 36 WAV;
- 320x240 BMP round trip;
- overlay/crop/scale tests separated from modem tests;
- no whole-file audio buffer;
- no whole-image ESP32 framebuffer requirement.

Internet downloads must not be required by the automated test suite.

## Explicit non-goals for the first field-capable SSTV implementation

- universal SSTV mode support;
- camera integration;
- photo editing UI;
- OCR/callsign recognition;
- automatic ADIF/POTA logging;
- mandatory QSL exchange;
- whole-file FFT processing;
- whole-audio TX buffer;
- whole-image ESP32 framebuffer;
- LCD RAM readback as persistent storage;
- direct Cardputer/LCD access from the portable codec;
- image post-processing/denoising beyond crop, scale, and simple text overlay.

## Reference material

Architecture and protocol decisions should be cross-checked against public
implementations/references. They are references, not runtime dependencies.

Primary MCU reference:

- **dawsonjon/PicoSSTV** — primary embedded SSTV reference for MiniShell. PicoSSTV
  demonstrates standalone microcontroller RX and TX, Martin/Scottie/Robot/SC2/PD mode
  support, SD-card image storage, image browsing, and a menu-driven UI on RP2040-class
  hardware. Use it to cross-check Robot 36 timing/reconstruction, bounded-memory
  encoder/decoder structure, scanline-oriented image handling, and portable UI/storage
  patterns. MiniShell code should remain independently structured around MiniShell's
  APIs and ADV constraints.

Additional references:

- JL Barber, N7CXI, "Proposal for SSTV Mode Specifications" ("Dayton paper").
- unexcellent/sstv: table-driven multi-mode encoder/decoder intended for
  microcontrollers.
- colaclanth/sstv: file-oriented Martin/Scottie/Robot decoder.
- F4JTV/sstv_decoder: streaming receive architecture with VIS detection and
  line-sync-based slant correction.
- JO3ALT/TinySSTV: compact MCU SSTV transmit reference.
- SSTV Handbook / published VIS tables.

These references should be used to cross-check protocol timing and interoperability,
not copied into MiniShell as architecture dependencies.

## Implementation gate

SSTV remains documentation-only now.

Do not open the implementation task until:

1. RTTY T065 is complete and accepted.
2. The architect reviews this SSTV architecture after the RTTY experience.
3. The architect explicitly selects the first SSTV implementation task.

At that point the first bounded implementation should stay Linux-oriented and verify
the portable Robot 36 core with deterministic files before QMX/ADV integration.

No SSTV code change is authorized by this architecture update.
