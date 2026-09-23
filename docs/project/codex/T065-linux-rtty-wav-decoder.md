# T065 — Linux RTTY WAV decoder

Status: READY

## Architect intent

Add RTTY as the next MiniShell mode, but begin with the smallest useful path:

1. Linux only for functional bring-up and decoding tests.
2. Decode known RTTY test WAV files first.
3. After WAV decode is accepted, connect the same decoder to live WebSDR or QMX audio in a later task.
4. Decoder performance does not need to be contest-grade; simplicity and ADV portability matter more.
5. RTTY is an **external application**. Do not add it to the ADV compiled-in app registry. The eventual ADV deployment artifact is `rtty.elf`.

## Objective

Implement a portable streaming RTTY/ITA2 receive core plus a Linux MiniShell application `rtty` that decodes a PCM WAV file and writes decoded text to Console.

The Linux build may use MiniShell's normal runtime module form (`build-linux/runtime/apps/rtty.so`). That is only the Linux loader/container format. The application architecture must remain external and source-portable so a later task can package the same app as `/flash/apps/rtty.elf` or `/sd/apps/rtty.elf` without moving DSP logic into the ADV platform layer.

## Current context

MiniShell currently builds Linux applications as runtime-loaded modules through `add_minishell_app(...)`. ADV resolves applications in this order:

```text
compiled-in
/flash/apps/<app>.elf
/sd/apps/<app>.elf
```

RTTY must use the external slots when ADV work begins; it must not be registered in `platform/adv/adv_apps.c`.

Existing RTTY research is in private repository `wcheng95/rtty_decoder`:

- `71f1803e13d72f5655b2b641125fd6239c298741`: original QMX I/Q Python decoder.
- `9f50ca6f0387946f7656207767fad9d3ade6d5ab`: continuous-phase real-audio AFSK encoder, validated over the air.
- `eb4c762b72b8d1f30464d354713479474d44e605`: design analysis and future robust-decoder plan.

The current Python decoder is an offline SciPy proof of concept and is **not** source to port directly. It uses non-causal `decimate()` / `fftconvolve(..., same)` operations and QMX-specific complex-I/Q geometry.

For this task, use **12 kHz ordinary real PCM audio** as the decoder boundary. The same fixed-rate core will be used by generated WAV files now and by MiniShell Audio/QMX later. No resampler belongs inside the RTTY core.

The first receiver profile is:

```text
sample rate: 12000 Hz
baud:        45.45
shift:       170 Hz
audio window: 500..1500 Hz
sense:       normal LSB; MARK is the upper tone, SPACE = MARK - 170 Hz
ITA2:        5 data bits, LSB first
idle:        MARK
```

The decoder must automatically acquire a valid tone pair anywhere in the 500..1500 Hz audio window rather than assuming a fixed 2125/1955-Hz pair.

The existing `rtty_encode.py` remains a protocol/reference encoder, but T065 must generate or convert its canonical test vectors to 12 kHz. Its current framing emits at least 1.5 stop bits (in practice 2.0 bits); the decoder should tolerate this.

## Source of truth

MiniShell:

```text
AGENTS.md
docs/README.md
apps/README.md
include/minishell/api.h
CMakeLists.txt
```

RTTY research/reference:

```text
repository: wcheng95/rtty_decoder
encoder:    9f50ca6f0387946f7656207767fad9d3ade6d5ab
design:     eb4c762b72b8d1f30464d354713479474d44e605
```

The real-audio encoder in that repo is the canonical first WAV producer.

## Architectural constraints

- `apps/rtty/` owns RTTY parsing, demodulation, framing, and ITA2 decode.
- The pure decoder core must not call MiniShell APIs, POSIX, ALSA, ESP-IDF, FreeRTOS, or platform code.
- WAV/file adaptation may use only the MiniShell Filesystem API.
- User-visible decoded text, usage, and errors use `Console.write()`.
- No Display UI in this task.
- No Audio API in this task.
- No CAT/Serial API in this task.
- No public MiniShell API changes.
- Do not add `rtty` to the ADV compiled-in registry.
- Do not introduce SciPy, FFTW, liquid-dsp, Codec2/Horus, fldigi, minimodem, or another large dependency.
- No requirement for optimal weak-signal performance.
- The receive algorithm must be **causal/streaming**: bounded state and bounded memory, with no whole-file buffer.
- Prefer caller-owned fixed state. Do not require heap allocation in the pure decoder core.
- Floating-point DSP is acceptable for the first implementation.
- The decoder core boundary is fixed at 12 kHz S16 mono.
- WAV adaptation converts supported file PCM into that exact stream; T065 WAV files must be 12 kHz and other sample rates are rejected.
- Preserve non-integer 45.45-baud timing; do not round the protocol to 45 or 50 baud.
- The RTTY shift is fixed at 170 Hz for T065.
- The decoder must acquire a normal-LSB MARK/SPACE pair anywhere in 500..1500 Hz; both detected tones must remain inside the window.

## Decoder architecture for T065

Keep this deliberately small:

```text
12 kHz S16 mono PCM
    -> 500..1500 Hz tone-pair acquisition
    -> locked MARK / SPACE tone-energy detector
    -> hard/soft MARK-vs-SPACE decision
    -> start-edge synchronized fractional bit clock
    -> start + 5 data + stop validation
    -> ITA2 LTRS/FIGS state
    -> decoded characters
```

Use a small causal acquisition + tracking design:

1. While unlocked, use a lightweight Goertzel/correlation scan over the 500..1500 Hz window to find a plausible 170-Hz-separated normal-LSB pair.
2. Because idle is MARK, acquisition may use the strong upper MARK tone and infer SPACE = MARK - 170 Hz; a pair-energy score may also be used when both tones are present.
3. Once locked, use only the two selected tone detectors for ordinary streaming demodulation.
4. If the decoder remains unable to produce valid framing for a bounded interval, it may drop lock and reacquire.

This must stay small enough for later ADV use. Do not add an FFT dependency merely for acquisition. A waterfall, HMM/Viterbi, Gardner loop, AFC, and AGC are unnecessary in T065.

Timing requirements:

- Recognize idle MARK -> SPACE as a candidate start edge.
- Use a fractional sample/bit phase; do not assume an integer samples-per-bit value.
- Sample the five data bits LSB-first at their nominal centers.
- Require a valid MARK stop region before accepting a character.
- Re-acquire/reseed timing from character start edges rather than attempting a long free-running frame clock.

ITA2 requirements:

- LETTERS and FIGURES tables.
- LTRS and FIGS shift codes.
- CR/LF/space.
- Unknown/invalid symbols must not crash the app.

## WAV input contract

Initial invocation:

```text
rtty <path.wav>
```

Fixed T065 profile:

```text
sample rate  = 12000 Hz
baud         = 45.45
shift        = 170 Hz
search band  = 500..1500 Hz
sense        = normal LSB (MARK upper, SPACE lower)
```

Support uncompressed RIFF/WAVE PCM with:

- mono or stereo,
- 16-bit or 24-bit integer PCM,
- sample rate exactly 12000 Hz as declared by the WAV header,
- ordinary real audio,
- streaming reads through MiniShell Filesystem.

For stereo, downmix safely to one real sample (average L/R or use one channel; document the choice).

Reject unsupported encodings/formats with a concise Console error and nonzero exit status.

Do not add command-line tuning options yet. Tone location is acquired automatically inside the fixed 500..1500 Hz window; baud/shift/sense remain fixed for this first profile.

## Implementation scope

Expected structure is approximately:

```text
apps/rtty/
    README.md
    main/
        rtty_main.c
        rtty_wav.c
        rtty_wav.h
    src/
        rtty_core.c
        rtty_core.h
        rtty_ita2.c
        rtty_ita2.h
    tools/
        ... optional small deterministic WAV generator ...
```

Exact filenames may differ if a simpler layout is cleaner, but keep the pure decoder separate from WAV/MiniShell adaptation.

Update Linux `CMakeLists.txt` to build runtime app `rtty`.

A deterministic synthetic test generator is encouraged. It should use only ordinary development-host tooling and must not become a runtime dependency. A Python-stdlib generator is preferable to requiring NumPy/SciPy.

Update `apps/README.md` to list `rtty` as a current application once implementation exists and state that the ADV form is intended to be external `rtty.elf`.

## Non-goals

- Live QMX UAC receive.
- WebSDR capture/network integration.
- CAT control.
- TX/RTTY encoder inside MiniShell.
- ADV `rtty.elf` packaging/build in this task.
- ADV hardware execution.
- Display UI.
- Waterfall/scope/tuning indicator.
- Continuous AFC after tone acquisition.
- Automatic MARK/SPACE sense reversal; T065 uses normal LSB sense.
- Multiple baud rates, shifts, or sample rates.
- USOS.
- Squelch/AGC.
- Weak-signal optimization.
- Porting Codec2/Horus/minimodem/fldigi.
- Decoding `quick_brown_fox_lsb.wav` from the old Python decoder; that file is QMX complex-I/Q and is outside this real-audio T065 boundary.

## Acceptance criteria

- [ ] `apps/rtty/` exists and uses only the public MiniShell API at the application boundary.
- [ ] Pure RTTY core is platform-independent and causal/streaming.
- [ ] Core uses bounded memory and does not buffer an entire WAV.
- [ ] Core input contract is fixed 12 kHz S16 mono.
- [ ] 45.45 baud is represented fractionally rather than rounded to an integer baud.
- [ ] Receiver automatically acquires a 170-Hz tone pair whose tones lie within 500..1500 Hz.
- [ ] Correct ITA2 LETTERS/FIGURES shift behavior is covered by tests.
- [ ] Linux build produces `build-linux/runtime/apps/rtty.so`.
- [ ] `rtty <path.wav>` accepts supported real-audio PCM WAV files via MiniShell Filesystem.
- [ ] 12 kHz / 24-bit / stereo AFSK decodes a clean known message such as `THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG`.
- [ ] At least one 12 kHz / 16-bit WAV case is also tested.
- [ ] Clean synthetic vectors decode with the tone pair at more than one location in the 500..1500 Hz window (for example MARK/SPACE 1000/830 Hz and 1400/1230 Hz).
- [ ] A supported PCM WAV with a non-12-kHz sample rate is rejected clearly.
- [ ] Unsupported WAV format returns a concise error and nonzero result.
- [ ] Existing MiniShell tests remain green.
- [ ] No code is added to `platform/adv/adv_apps.c`.
- [ ] No public API changes.
- [ ] `apps/rtty/README.md` documents the current Linux WAV-only state and the later external ADV target name `rtty.elf`.

## Automated tests

Codex must add focused unit/integration coverage for at least:

- ITA2 LETTERS/FIGURES transitions.
- clean synthetic RTTY samples -> expected text through the pure core.
- fractional 45.45-baud timing.
- automatic acquisition of 170-Hz-spaced tones at multiple positions inside 500..1500 Hz.
- WAV parser: 12 kHz PCM16 mono.
- WAV parser: 12 kHz PCM24 stereo.
- rejection of a non-12-kHz WAV.
- malformed/unsupported WAV rejection.

Run:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
```

Also exercise the actual MiniShell runtime app with a generated test WAV and record the exact command/output in the implementation notes.

The test generator must make the test reproducible; do not depend on an internet download during tests.

## Manual / hardware validation

Linux manual validation after supervisor review:

1. Generate a clean 12-kHz real-audio RTTY WAV using the task's deterministic generator (the pinned encoder remains a protocol reference).
2. Place it in the Linux MiniShell-visible filesystem.
3. Run `rtty <path.wav>`.
4. Confirm the known message is printed correctly.

No ADV hardware validation is required for T065.

Live WebSDR/QMX receive is a later task after this WAV path is accepted.

## Codex implementation notes

Codex fills this section before handoff.

### Implementation summary

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and local test evidence.

## Architect test result

Record Linux WAV validation and final acceptance here.
