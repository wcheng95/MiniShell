# T067 — Linux SSTV Robot 36 WAV decoder

Status: IMPLEMENTING

## Architect intent

Start SSTV implementation now that RTTY T065 is COMPLETE.

The first bounded milestone is:

1. generate a deterministic 12 kHz `test.wav`;
2. decode its standard Robot 36 VIS/header and image;
3. write a full-resolution 320x240 24-bit BMP;
4. exercise the real MiniShell Filesystem/runtime path;
5. keep the RX core identical to the one that will later receive WebSDR/QMX audio.

Live WebSDR capture is the next task after this WAV path is accepted.

## Objective

Implement a portable streaming SSTV RX core plus Linux MiniShell application:

```text
sstv <input.wav> <output.bmp>
```

Robot 36 is the only supported SSTV mode.

## Source of truth

- `docs/SSTV/architecture.md`
- `include/minishell/api.h`
- `apps/rtty/` for the established external-app/WAV-adapter pattern
- public Robot 36 timing:
  - VIS 8 / 0x08
  - 320x240
  - Y + alternating R-Y / B-Y chroma
  - 9.0 ms 1200-Hz sync
  - 3.0 ms 1500-Hz sync porch
  - 88.0 ms luminance scan
  - alternating 1500/2300-Hz separator
  - 1.5 ms porch
  - 44.0 ms chroma scan
  - 1500..2300-Hz image-tone mapping

## Architectural constraints

- external application; Linux `sstv.so`, later ADV `sstv.elf`
- do not add to ADV compiled-in registry
- pure decoder core is causal, streaming, bounded-memory, platform-independent
- core input boundary: 12000-Hz signed-16-bit mono real PCM
- no resampler in the core
- no FFTW/liquid-dsp/OpenCV/SciPy/runtime image library
- floating point is acceptable
- no whole-file audio buffer
- no whole-image framebuffer
- bounded line/component buffers only
- automatic standard VIS detection is the normal path
- validate VIS parity/start/stop
- estimate slow audio-frequency offset from known header tones
- use fractional/cumulative timing; never round each segment/pixel independently
- use repeated line sync to correct line phase and slow slant/sample-clock error
- reconstruct Robot 36 color from alternating chroma lines without a full-frame buffer
- output through an image-sink boundary
- first image sink: uncompressed top-down 24-bit BMP
- MiniShell Display remains unused; no direct LCD access
- no public MiniShell API changes

## First implementation flow

```text
12 kHz WAV
  -> streaming WAV adapter
  -> SSTV FM demodulator
  -> VIS detector
  -> Robot 36 sync/timing tracker
  -> luminance + alternating chroma reconstruction
  -> RGB scanlines
  -> BMP sink
```

## Deterministic test.wav

Add a stdlib-only generator under `apps/sstv/tools/`.

It must generate a standard Robot 36 transmission containing:

- 1900-Hz leader
- 1200-Hz break
- second 1900-Hz leader
- standard VIS start/data/parity/stop for code 8
- 240 Robot 36 image lines
- deterministic color bars / gradients suitable for pixel-level validation

The generator must use cumulative fractional timing and continuous phase.

The automated test creates a temporary file named `test.wav`; a binary WAV does
not need to be committed.

## Expected ownership

```text
apps/sstv/
    README.md
    main/
        sstv_main.c
        sstv_wav.c/.h
        sstv_bmp.c/.h
    src/
        sstv_core.c/.h
    tools/
        generate_test_wav.py
tests/
    sstv_test.c
    sstv_wav_test.py
```

## Acceptance criteria

- [ ] Linux build produces `build-linux/runtime/apps/sstv.so`
- [ ] generated `test.wav` is exactly 12 kHz ordinary PCM
- [ ] decoder automatically recognizes Robot 36 VIS 8
- [ ] VIS parity/header errors are rejected without crashing
- [ ] output BMP is 320x240, top-down, 24-bit uncompressed
- [ ] selected decoded pixels match the deterministic source image within a reasonable demodulation tolerance
- [ ] clean test.wav completes the full image
- [ ] core remains stable for arbitrary input chunk sizes
- [ ] core uses bounded state and line/component buffers only
- [ ] WAV adapter supports PCM16/PCM24, mono/stereo, exactly 12 kHz
- [ ] malformed/unsupported WAV input is rejected cleanly
- [ ] application uses only MiniShell public Filesystem/Console services
- [ ] no Display, Audio, CAT, Serial, ADV registry, or public API changes
- [ ] existing tests remain green

## Non-goals

- live WebSDR or QMX RX
- SSTV TX
- CAT/PTT
- ADV packaging or hardware execution
- graphical preview
- modes other than Robot 36
- PNG/JPEG
- weak-signal optimization

## Required validation

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
python3 apps/sstv/tools/generate_test_wav.py /tmp/test.wav
```

Then through MiniShell:

```text
M$> sstv /tmp/test.wav /tmp/test.bmp
```

Record actual output and BMP validation before T067 is accepted.

## Next task

After T067 is accepted, T068 will connect the same core to captured WebSDR audio.
The primary on-air target is 14.230 MHz USB; 7.171 MHz is another common SSTV
calling frequency.

## Implementation notes

### Implementation summary

### Files changed

### Tests run

### Known limitations / risks

### Commit

## Supervisor review

## Architect test result
