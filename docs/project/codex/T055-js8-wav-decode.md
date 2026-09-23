# T055 — Linux JS8 Normal WAV decoder

Status: READY

## Architect intent

Reach the first real JS8Chat receive milestone using the exact upstream JS8Call-improved Normal-mode test WAV already available locally at:

    ~/projects/js8chat/A_2_1.wav

Use the same flow that proved MiniFT8: host WAV first, live MiniShell/ADV later.

JS8Chat remains KISS and supports JS8 Normal / Mode A only.

## Objective

Implement a standalone Linux utility that performs:

    12 kHz mono S16 WAV
      -> normalize + simple 2:1 decimation
      -> 6 kHz / 960-sample blocks
      -> T054 monitor
      -> Normal Costas candidate search
      -> T054 LDPC/CRC payload decode
      -> unique 75-bit payload(s)
      -> minimal physical-frame view
      -> stdout

The utility target is apps/js8chat/tools/js8_decode.c.

T055 must prove the accepted T054 engine decodes a real upstream waveform. Do not expand into the full JS8 application/chat protocol.

## Accepted baseline

T052 CRC-12 + LDPC(174,87): COMPLETE.
T053 Normal 79-tone channel map: COMPLETE.
T054 monitor + payload decoder: COMPLETE at 0486cb3c474505eeb3601247abc5176c6f522338.

Read AGENTS.md, docs/js8/*, T052-T054, apps/js8chat/src/js8_engine/README.md, and the current MiniFT8 host decoder only as an architecture example.

## Exact fixture identity

Frozen upstream reference:

- repository: JS8Call-improved/JS8Call-improved
- tag: v3.0.3
- path: media/tests/A_2_1.wav
- Git blob SHA: d986a4e5a9cc654dffbfadae73ec35cc9cea1d83
- size: 360208 bytes

The upstream media/tests README defines names as {MODE}_{DEPTH}_{EXPECTED_DECODES}.wav, therefore A_2_1 means Normal mode, upstream depth 2, expected decode count 1.

Before using the local file, run:

    git hash-object ~/projects/js8chat/A_2_1.wav

It must equal d986a4e5a9cc654dffbfadae73ec35cc9cea1d83.

If it does not match, mark T055 BLOCKED and report the mismatch. Do not silently use a different fixture.

## Architectural constraints

- Normal/Mode A only.
- First run the accepted T054 DSP unchanged against the real fixture.
- Do not change search/sensitivity policy until failure evidence is collected.
- Any T054 policy change must be narrowly justified and recorded.
- No upstream desktop decoder architecture, whitening, subtraction, soft-combining, or generic submodes.
- No FT8 production-source changes.
- js8_engine remains pure, no-heap, platform-independent.
- Host utility may use normal Linux/POSIX file/allocation APIs.
- No UI, MiniShell app registration, live UAC, QMX CAT, conversations, JSC, or full JS8 application protocol.

## WAV input and frontend

Required WAV contract:

- RIFF/WAVE
- PCM format 1
- mono
- 12000 samples/s
- 16-bit little-endian

Parse RIFF chunks safely; do not assume a fixed 44-byte header.

Normalize S16 to float and use the same simple 2:1 phase-preserving decimation style as MiniFT8. Do not add an FIR/resampler unless the unchanged path is proven insufficient.

A 15-second file yields 90,000 samples at 6 kHz:

- 93 complete 960-sample blocks = 89,280 samples
- remainder = 720 samples

Process exactly the 93 complete blocks. Ignore the final incomplete 720-sample engine block; do not pad and do not create a 94th block.

## Candidate/decode flow

After capture:

1. obtain the T054 waterfall;
2. run the accepted T054 baseline first: capacity 50, min score 5, time search -10..+19, time_osr 2, freq_osr 2;
3. try candidates strongest-first;
4. retain only LDPC+CRC-valid payloads;
5. deduplicate by exact 75-bit payload;
6. print all unique valid payloads.

Hard fixture acceptance:

    unique valid payload count == 1

Candidate score/order are diagnostics, not identity.

If unchanged T054 does not decode one payload, record candidate count, top scores, time/frequency lattice, LDPC failures and CRC failures before changing anything.

## Minimal physical-frame helper

Add the smallest pure helper under js8_engine, for example js8_frame.[ch].

A 75-bit physical payload is:

- bits 0..71: 12 x 6-bit alphabet words
- bits 72..74: 3-bit frame type

Exact v3.0.3 alphabet:

    0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+

Expose an output equivalent to:

    char text12[13]
    uint8_t type

This is not the JS8 application-protocol parser. It only exposes the physical frame already present in the PHY.

Validate the helper against existing T052/T053 golden payloads.

## Utility output

Usage:

    ./build-linux/js8_decode ~/projects/js8chat/A_2_1.wav

For each unique valid decode print one stable line containing at least:

    payload=<75 bits> type=<0..7> frame="<12 chars>"

Score/time/frequency diagnostics may also be printed.

If frequency is printed, derive it as:

    200 + freq_offset*6.25 + freq_sub*(6.25/freq_osr)

For baseline freq_osr=2, freq_sub spacing is 3.125 Hz.

Do not label candidate time as exact UTC DT yet.

## External reference test

Do not commit the WAV into MiniShell.

Add an optional CMake cache path such as JS8_A2_1_REFERENCE_WAV.

When set, build/register a reference test invoking js8_decode and require exactly one unique valid decode.

Normal CTest must remain runnable without the external WAV.

Codex local validation must configure a separate reference build using $HOME/projects/js8chat/A_2_1.wav.

## First successful real-WAV evidence

Record in this task:

- fixture Git blob hash
- unique decode count
- exact 75-bit payload
- physical frame type
- 12-character physical frame
- candidate score
- candidate time lattice
- candidate frequency lattice
- derived audio frequency
- LDPC hard-error count

These become the first real-WAV regression evidence.

## Architecture enforcement

Extend JS8Chat rules with a tools module analogous to FT8:

    tools -> tools + js8_engine

Permit fopen only for the exact host utility path as a native exception.

Do not relax js8_engine no-heap or platform rules.

## Acceptance criteria

- [ ] local fixture blob matches pinned v3.0.3 blob
- [ ] js8_decode accepts the real 12 kHz mono S16 WAV
- [ ] frontend produces continuous 6 kHz samples
- [ ] exactly 93 full monitor blocks are processed
- [ ] unchanged T054 policy is tried first
- [ ] exactly one unique valid 75-bit payload is recovered
- [ ] payload is printed stably
- [ ] physical type and 12-character frame are printed
- [ ] first-run diagnostics are recorded
- [ ] external WAV is not committed
- [ ] normal tests require no external WAV
- [ ] optional pinned-WAV reference test passes
- [ ] js8_engine remains pure/no-heap
- [ ] FT8 production source unchanged
- [ ] Linux full CTest passes
- [ ] portable CTest passes
- [ ] JS8 boundary checks pass
- [ ] ASan/UBSan passes
- [ ] ADV build remains green
- [ ] git diff --check passes
- [ ] no unrelated cleanup

No RF/hardware validation is required. After supervisor review, the architect will manually run js8_decode on pc-1; that manual run is the milestone acceptance.

## Required local commands

Run at least:

    git hash-object ~/projects/js8chat/A_2_1.wav

    cmake -S . -B build-linux
    cmake --build build-linux -j"$(nproc)"
    PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

    cmake -S tests/unit -B /tmp/T055-build-unit
    cmake --build /tmp/T055-build-unit -j"$(nproc)"
    ctest --test-dir /tmp/T055-build-unit --output-on-failure

    PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
    PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat

    cmake -S . -B /tmp/T055-build-ref -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
    cmake --build /tmp/T055-build-ref -j"$(nproc)"
    ctest --test-dir /tmp/T055-build-ref -R "js8.*reference|js8.*A2.*1" --output-on-failure
    /tmp/T055-build-ref/js8_decode "$HOME/projects/js8chat/A_2_1.wav"

    source ~/projects/esp-idf/export.sh
    idf.py -C platform/adv build

    git diff --check
    git diff -- apps/ft8

The final FT8 diff must be empty.

Run ASan/UBSan on the pure JS8 tests and, if practical, the host reference decode using the same sanitizer policy as T054.

## Branch workflow

Use branch:

    codex/T055-js8-wav-decode

Codex must:

1. verify fixture identity before coding against it;
2. implement the physical-frame helper and Linux WAV decoder;
3. try unchanged T054 first;
4. collect evidence before any policy change;
5. record the exact successful real-WAV result;
6. add optional reference-test integration without checking in the WAV;
7. run all gates;
8. set Status to REVIEW;
9. fill implementation notes;
10. commit and push one reviewable commit;
11. return the SHA;
12. no PR and no Actions wait.

## Codex implementation notes

### Implementation summary

### Files changed

### Invariants preserved

### First real-WAV decode evidence

### Local tests run

### Manual validation still required

### Known limitations / risks

### Commit

## Supervisor review

## Architect test result

Pending manual run of js8_decode ~/projects/js8chat/A_2_1.wav after supervisor review.
