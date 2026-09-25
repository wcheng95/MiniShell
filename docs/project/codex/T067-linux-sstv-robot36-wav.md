# T067 — Linux SSTV Robot 36 WAV decoder

Status: COMPLETE

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

Implemented the Linux Robot 36 WAV receive path as an external MiniShell app.
The pure core uses a 19-tap streaming Hilbert/phase discriminator, standard VIS
acquisition/parity checking, slow frequency-offset tracking, fractional Robot 36
line/pixel timing, horizontal-sync line-clock tracking, and bounded two-line
Y/Cr/Cb reconstruction. Completed RGB rows stream directly into a top-down 24-bit
BMP sink.


### Files changed

- `apps/sstv/README.md`
- `apps/sstv/src/sstv_core.c/.h`
- `apps/sstv/main/sstv_main.c`
- `apps/sstv/main/sstv_wav.c/.h`
- `apps/sstv/main/sstv_bmp.c/.h`
- `apps/sstv/tools/generate_test_wav.py`
- `tests/sstv_test.c`
- `tests/sstv_wav_test.py`
- `CMakeLists.txt`
- `apps/README.md`


### Tests run

Local standalone validation in the supervisor environment:

- all new C sources compile with `-std=c11 -Wall -Wextra -Werror -Wpedantic`;
- deterministic 12-kHz Robot 36 `test.wav` auto-detects VIS 8 and completes 240 rows;
- caller chunk sizes 1, 137, and 4096 samples produce byte-identical RGB output;
- PCM16/PCM24 mono/stereo WAV adapter cases pass;
- invalid VIS parity and valid unsupported VIS are rejected without a partial BMP;
- output BMP is 320x240, top-down, 24-bit, 230454 bytes;
- sampled color-bar/gray-ramp pixels are within 12/255 maximum channel error in the
  local clean-vector check;
- malformed/sample-rate rejection and cleanup checks pass.

The full repository CMake/CTest run and actual MiniShell runtime-loader portion of
`sstv_wav_test.py` remain to be run on pc-1 because this execution environment
cannot clone/materialize the user's GitHub working tree.


### Known limitations / risks

- T067 is deliberately clean-WAV bring-up, not weak-signal/on-air optimization.
- Live WebSDR/QMX audio is deferred to T068.
- The current instantaneous-frequency estimator is intentionally small and may need
  threshold/filter tuning after real WebSDR recordings.
- No SSTV TX or graphical Display path is included.


### Commit

`4a1e47197a968fb7b65bec37d99c3e1ddb7ca124`

## Supervisor review

Initial code/diff review: PASS for architect pc-1 validation.

The implementation stays inside T067 scope: external Linux app, pure bounded streaming
core, 12 kHz S16-mono boundary, standard Robot 36 VIS 8, top-down 24-bit BMP sink,
no whole-image framebuffer, and no Display/Audio/Serial/CAT/public-API/ADV-registry
changes.

The deterministic generator, core chunk-invariance checks, PCM16/PCM24 mono/stereo
WAV adaptation, malformed-input cleanup, VIS parity/unsupported-mode handling, and
actual MiniShell runtime-loader regression are present in the branch.

pc-1 full CTest exposed one WAV-container edge case in `sstv_wav`: the Robot 36
decoder completed line 240 before the declared WAV data chunk ended, so truncating
only the post-image tail was incorrectly accepted. The adapter was corrected in
`19ae81268960dd1f84b7851eb049ec1f495744cb` to drain/validate the remainder of
the declared data chunk after successful image decode and remove the completed BMP
if that validation fails.

Remaining evidence before merge is a clean full repository CTest rerun and the
architect's real pc-1 generation/decode/view of `test.wav -> test.bmp`.

## Architect test result

### pc-1 CTest

After the truncated-WAV fix, the architect reran the full Linux suite on pc-1:

```text
119/119 PASS
```

The focused `sstv_wav` regression and the full repository CTest both pass.

Automated acceptance is complete. Architect manual validation also passed: the generated Robot 36 `test.wav` decoded through MiniShell and the resulting 320x240 BMP displayed the expected color-bar image.


### Manual image acceptance — PASS

Architect generated the deterministic Robot 36 WAV, decoded it through the actual
MiniShell runtime using the normal `/flash` path, and visually inspected the output
BMP.

Result:

```text
Robot 36 color-bar image decoded correctly
```

T067 is COMPLETE. The accepted baseline is the portable Linux
`12 kHz WAV -> Robot 36 -> 320x240 BMP` receive path. Live WebSDR capture is the
next SSTV milestone.
