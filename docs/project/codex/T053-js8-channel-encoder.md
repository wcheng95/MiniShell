# T053 — JS8 Normal 79-tone channel encoder

Status: READY

## Architect intent

Continue JS8Chat implementation stage by stage using the MiniFT8 engineering flow.

T052 established exact JS8Call-improved v3.0.3 CRC-12 and LDPC(174,87) behavior. T053 adds only the Normal-mode channel-symbol framing needed before receive DSP work.

JS8Chat is a practical field keyboard-to-keyboard mode. **JS8 Normal (Mode A) is the only supported JS8 mode.** Do not introduce generic submode abstractions.

## Objective

Implement the pure JS8 Normal channel encoder:

```text
75 payload bits
    -> CRC-12
    -> 87 information bits
    -> LDPC(174,87)
    -> 58 x 3-bit data tones
    -> insert 3 x 7-symbol Normal Costas sync
    -> exact 79 JS8 Normal tones
```

The output must match JS8Call-improved v3.0.3 bit-for-bit and tone-for-tone.

T053 stops before waveform/audio generation and before JS8 application/message packing.

## Accepted baseline

```text
T052  JS8 CRC-12 + LDPC(174,87) core   COMPLETE
commit 39023694a59ad06ef86051d98931d79703e67e3c
```

Current pure source:

```text
apps/js8chat/src/js8_engine/
    js8_crc.[ch]
    js8_ldpc.[ch]
    js8_ldpc_tables.h
```

## Source of truth

Read before editing:

```text
AGENTS.md
docs/js8/README.md
docs/js8/architecture.md
docs/js8/implementation-plan.md
docs/js8/js8-phy.md
docs/project/codex/T052-js8-crc-ldpc.md

apps/js8chat/src/js8_engine/js8_crc.[ch]
apps/js8chat/src/js8_engine/js8_ldpc.[ch]
tests/js8_vectors.md
tests/js8_golden_vectors.h
```

Frozen interoperability reference:

```text
repository: JS8Call-improved/JS8Call-improved
tag:        v3.0.3
files:
    JS8_Mode/JS8.cpp
    JS8_Mode/JS8.h
    JS8_Include/commons.h
```

The normative upstream encoder is `JS8::encode()`.

Normal mode constants:

```text
symbols             79
data symbols         58
sync symbols         21
tones                  8
symbol samples @12k 1920
symbol duration      160 ms
tone spacing          6.25 Hz
period               15 s
Costas type          ORIGINAL
```

Normal-mode original Costas sequence:

```text
4 2 5 6 1 3 0
```

For Normal mode the same original sequence is used at all three sync positions.

Sync positions:

```text
0..6
36..42
72..78
```

Data positions:

```text
7..35   = 29 parity 3-bit words
43..71  = 29 information 3-bit words
```

The v3.0.3 encoder groups each 87-bit half MSB-first into 29 consecutive 3-bit words and writes those values directly as tone indices 0..7.

**Do not apply the FT8 Gray map.**

JS8 Normal channel order is:

```text
Costas A
87 LDPC parity bits -> 29 direct 3-bit tones
Costas B
87 information bits -> 29 direct 3-bit tones
Costas C
```

## Architectural constraints

1. JS8 Normal / Mode A only.
2. Pure platform-independent C.
3. No heap allocation.
4. No mutable global state.
5. No Qt, Boost, FFTW, MiniShell API, POSIX, ESP-IDF, or NuttX dependency.
6. Reuse T052 CRC and LDPC code; do not duplicate those algorithms.
7. Do not change FT8 code or tables.
8. Do not create a generic FT8/JS8 channel encoder.
9. No waveform synthesis.
10. No application/message text codec.
11. Input/output arrays contain bounded caller-owned data.
12. Every produced tone must be 0..7.

## Implementation scope

Add a small pure channel module under:

```text
apps/js8chat/src/js8_engine/
```

For example:

```text
js8_channel.c
js8_channel.h
```

Suggested public constants:

```c
#define JS8_TONE_COUNT        79u
#define JS8_DATA_TONE_COUNT   58u
#define JS8_SYMBOL_PERIOD_MS 160u
#define JS8_TONE_SPACING_HZ    6.25f
```

A suitable API is:

```c
int js8_channel_encode(const uint8_t payload_bits[JS8_PAYLOAD_BITS],
                       uint8_t tones[JS8_TONE_COUNT]);
```

A diagnostic helper may also expose the immutable Normal Costas sequence or tone-frequency mapping if useful, but keep the API minimal.

If a tone-frequency helper is added, it must be pure:

```text
tone_hz = base_hz + tone_index * 6.25
```

No clock or TX ownership belongs here.

## Golden vectors

Extend the existing pinned v3.0.3 vector infrastructure.

For the same three T052 payload vectors, lock exact 79-tone arrays produced by the normative v3.0.3 algorithm.

The vector oracle must remain independent of the MiniShell implementation.

Preferred approach:

- extend `tests/js8_reference_oracle.py` so the standalone extracted v3.0.3 helper emits the exact 79 tones;
- keep the upstream source SHA-256 pin;
- regenerate checked-in expected vectors;
- normal CTest must still require no upstream checkout, Boost, network, or C++.

At least one test must explicitly prove all three Costas groups are:

```text
4 2 5 6 1 3 0
```

at positions:

```text
0..6
36..42
72..78
```

Also prove the 29 parity words come from codeword bits 0..86 and the 29 information words from bits 87..173, MSB-first, direct binary to tone index.

## Tests

Add deterministic tests proving:

- all three upstream-derived payloads produce exact 79-tone golden vectors;
- every tone is <= 7;
- exact tone count is 79;
- Costas groups match the original Normal sequence;
- data-tone positions exactly match direct 3-bit grouping of the T052 golden codewords;
- invalid/null arguments are rejected without output corruption;
- repeated calls are deterministic and retain no state;
- no FT8 Gray mapping is present.

If a tone-frequency helper exists, test base 1500 Hz:

```text
tone 0 -> 1500.00 Hz
tone 1 -> 1506.25 Hz
...
tone 7 -> 1543.75 Hz
```

Do not add audio samples or FFT tests in T053.

## Build / architecture enforcement

Extend the existing `js8_engine` build/test source list only as needed.

The existing JS8Chat architecture rule remains:

```text
src/js8_engine -> js8_engine only
no heap
```

Do not loosen it.

## Non-goals

Do not implement:

- WAV parsing or `A_2_1.wav` decode;
- waveform generation;
- monitor/waterfall;
- FFT;
- candidate search;
- likelihood extraction;
- receive timing search;
- message text packing;
- the 12-character JS8 text alphabet API as an application codec;
- standard/compound callsigns;
- directed messages;
- CQ/HB;
- Huffman/JSC;
- conversation state;
- UI;
- TX scheduler;
- MiniShell app registration;
- Audio/UAC;
- QMX CAT;
- ADV optimization;
- any JS8 mode except Normal.

## Acceptance criteria

- [ ] JS8 Normal channel encoder accepts exactly 75 payload bits.
- [ ] T052 CRC-12 and LDPC are reused.
- [ ] exact 79 tones match v3.0.3 for at least three pinned payloads.
- [ ] Costas sequence is exactly `4 2 5 6 1 3 0` at 0, 36, and 72.
- [ ] parity tones occupy 7..35.
- [ ] information tones occupy 43..71.
- [ ] 3-bit words are direct binary tone indices; no FT8 Gray map.
- [ ] all tones are 0..7.
- [ ] pure C, no heap, no mutable global state.
- [ ] no platform/MiniShell/FT8 dependency added.
- [ ] golden vectors regenerate from exact pinned v3.0.3 source.
- [ ] Linux full CTest passes.
- [ ] portable CTest passes.
- [ ] JS8 dependency/platform boundary checks pass.
- [ ] real ADV build remains green.
- [ ] sanitizer check passes for the pure JS8 test.
- [ ] `git diff --check` passes.
- [ ] no unrelated cleanup.

No manual/hardware validation is required.

## Automated tests

Run:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T053-build-unit
cmake --build /tmp/T053-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T053-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

python3 tests/js8_reference_oracle.py /tmp/T053-JS8.cpp > /tmp/T053-vectors.h
# compare the regenerated output with the checked-in vector file(s)

cc -std=c11 -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -Iapps/js8chat/src/js8_engine tests/js8_phy_test.c \
  apps/js8chat/src/js8_engine/js8_crc.c \
  apps/js8chat/src/js8_engine/js8_ldpc.c \
  apps/js8chat/src/js8_engine/js8_channel.c \
  -lm -o /tmp/T053-js8-sanitize
/tmp/T053-js8-sanitize

git diff --check
```

Adapt the oracle output filename/test command narrowly if the existing vector layout is cleaner another way.

## Branch workflow

Use:

```text
codex/T053-js8-channel-encoder
```

Codex:

1. read AGENTS.md, JS8 canonical docs, T052, and this task;
2. use exact v3.0.3 `JS8::encode()` behavior as oracle;
3. implement only the pure Normal-mode 79-tone channel encoder;
4. extend independent golden vectors;
5. run all required local gates;
6. set Status to REVIEW;
7. fill implementation notes;
8. commit and push one reviewable commit;
9. return commit SHA;
10. no PR;
11. no GitHub Actions wait.

## Codex implementation notes

### Implementation summary

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

## Architect test result

No manual/hardware validation is required for T053.
