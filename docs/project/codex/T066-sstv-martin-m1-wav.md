# T066 — Linux SSTV Martin M1 WAV decoder

Status: IMPLEMENTING

## Architect intent

Start SSTV implementation now that RTTY T065 is COMPLETE.

The first bounded milestone is deliberately narrow:

1. generate a deterministic 12 kHz `test.wav`;
2. decode its standard Martin M1 VIS/header and image;
3. write a full-resolution 24-bit BMP;
4. exercise the real MiniShell Filesystem/runtime path;
5. keep the RX core identical to the one that will later receive WebSDR/QMX audio.

Live WebSDR capture is the next task after this WAV path is accepted.

## Objective

Implement a portable streaming SSTV RX core plus Linux MiniShell application:

```text
sstv <input.wav> <output.bmp>
```

The first supported mode is Martin M1 only.

## Source of truth

- `docs/SSTV/architecture.md`
- `include/minishell/api.h`
- `apps/rtty/` for the established external-app/WAV-adapter pattern
- public Martin M1 timing: VIS 44 / 0x2C, 320x256, G-B-R,
  1200-Hz 4.862-ms sync, 1500-Hz 0.572-ms porch/separators,
  146.432-ms color scans, 1500..2300-Hz video

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
- Martin M1 row buffering only
- automatic standard VIS detection is the normal path
- validate VIS parity/start/stop
- estimate slow audio-frequency offset from known header tones
- use fractional/cumulative timing; never round each pixel duration independently
- use repeated line sync to correct line phase and slow slant/sample-clock error
- output through an image-sink boundary
- first image sink: uncompressed top-down 24-bit BMP
- MiniShell Display remains unused; no direct LCD access
- no public MiniShell API changes

## First implementation flow

```text
12 kHz WAV
  -> streaming WAV adapter
  -> SSTV frequency demodulator
  -> VIS detector
  -> Martin M1 line timing/sync tracker
  -> G/B/R scanline reconstruction
  -> BMP scanline sink
```

## Deterministic test.wav

Add a stdlib-only generator under `apps/sstv/tools/`.

It must generate a standard Martin M1 transmission containing:

- 1900-Hz leader
- 1200-Hz break
- second 1900-Hz leader
- standard VIS start/data/parity/stop for code 44
- 256 Martin M1 image lines
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

Exact internal helpers may differ if simpler.

## Acceptance criteria

- [ ] Linux build produces `build-linux/runtime/apps/sstv.so`
- [ ] generated `test.wav` is exactly 12 kHz ordinary PCM
- [ ] decoder automatically recognizes Martin M1 VIS 44
- [ ] VIS parity/header errors are rejected without crashing
- [ ] output BMP is 320x256, top-down, 24-bit uncompressed
- [ ] selected decoded pixels match the deterministic source image within a reasonable demodulation tolerance
- [ ] clean test.wav completes all 256 rows
- [ ] core remains stable for arbitrary input chunk sizes
- [ ] core uses bounded state and row buffers only
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
- modes other than Martin M1
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

Record actual output and BMP validation before T066 is accepted.

## Next task

After T066 is accepted, T067 will connect the same core to captured WebSDR audio,
with 14.230 MHz USB as the primary on-air target and 7.171 MHz as another common
SSTV calling frequency.

## Implementation notes

### Implementation summary

### Files changed

### Tests run

### Known limitations / risks

### Commit

## Supervisor review

## Architect test result
