# RTTY WAV receiver

RTTY is an external MiniShell application. T065 builds the Linux runtime module
`build-linux/runtime/apps/rtty.so`. A later task will package the portable source
as `rtty.elf`, resolved from `/flash/apps` or `/sd/apps`; it is not registered as
an ADV compiled-in application.

```text
rtty /flash/test.wav
```

The app uses only public MiniShell Filesystem and Console services. It emits
decoded characters without an added banner or newline; usage/errors are concise
Console text with a nonzero app result. No Audio, Serial, Display or TX path exists.

Input is RIFF/WAVE uncompressed PCM, exactly 12000 Hz, mono/stereo, 16/24 bits.
Stereo channels are averaged using 32-bit arithmetic; signed 24-bit samples are
scaled to S16 (division truncates toward zero) before averaging. RIFF ancillary
chunks and padding are skipped; an unpadded final odd-sized data chunk is accepted,
as produced by Python's wave writer. Unsupported formats/rates, malformed headers,
short data and FS failures report errors. Output already emitted before a later
read failure cannot be retracted. Only the first data chunk is decoded.

The pure core consumes arbitrary chunks of 12 kHz S16 mono. Caller-owned state
is 2704 bytes on the Linux build; there is no allocation, API access, file buffer
or resampling in the core. The WAV adapter additionally holds a 1536-byte PCM
transport buffer and 512-byte mono buffer. Memory use does not grow with input.

The fixed profile is normal LSB: upper MARK, SPACE 170 Hz lower, both within
500..1500 Hz, 45.45 baud, five data bits LSB first, MARK idle, ITA2 LETTERS/FIGURES.
Tables follow the pinned encoder's figures variant (including `$`, `!`, `#`, `&`).
There is no USOS. Start SPACE is checked at half a bit, data at 1.5..5.5 bits,
and MARK stop at 6.5 and 7.0 bits. Each start edge reseeds a fractional
264.026402640-sample bit clock; 1.5- and 2-bit stops are supported.

Acquisition scans a 100 ms block with Goertzel bins every 5 Hz, seeking a
dominant idle MARK within 670..1500 Hz and inferring SPACE. Once locked, only
the two selected Goertzel detectors run over the most recent 120 samples,
with a decision every 12 samples. Three seconds without valid framing drops
lock and resets LETTERS before reacquisition. No FFT or large dependency is used.

This is a clean-signal bring-up receiver. Supply an idle MARK preamble (the
reference uses about 0.5 seconds). Arbitrary mid-message entry, interfering
tones, weak signals, drift after acquisition and sense reversal are not promised.
There is no AFC, AGC, squelch, or contest-grade performance claim.

Generate a deterministic development WAV with Python's standard library:

```sh
python3 apps/rtty/tools/generate_wav.py /tmp/test.wav
python3 apps/rtty/tools/generate_wav.py /tmp/test16.wav --mark 1400 --width 2 --channels 1
```

Copy it to the MiniShell-visible `/flash/test.wav` and invoke `rtty` above.
The generator is test tooling, not production TX or a runtime dependency.

Protocol/DDS reference: `wcheng95/rtty_decoder`, `rtty_encode.py` at
`9f50ca6f0387946f7656207767fad9d3ade6d5ab`; design analysis at
`eb4c762b72b8d1f30464d354713479474d44e605`. The generator preserves the encoder's
continuous-phase waveform, shift rules and actual two-stop-bit framing, at
12 kHz and in-band tones. Tests also exercise 1.5 stops, band edges/off-grid
tones, chunk invariance, invalid starts/stops, reacquisition, PCM conversions,
short FS reads, error cleanup and actual runtime module loading.
