# T052 — JS8 Normal CRC-12 and LDPC(174,87) core

Status: READY

## Architect intent

Begin JS8Chat implementation using the same staged MiniShell engineering flow used for MiniFT8.

JS8Chat is a compile-in MiniShell application. The implementation architecture is the existing MiniFT8/Ft8Engine pattern; JS8Call-improved v3.0.3 is the frozen interoperability oracle.

The product philosophy is KISS: JS8Chat exists for practical keyboard-to-keyboard HF communication in the field. **JS8 Normal (Mode A) is the only JS8 speed/mode supported.** Do not add generic multi-submode abstractions.

The architect has an upstream Normal-mode test fixture at:

```text
~/projects/js8chat/A_2_1.wav
```

That WAV is reserved for the later Linux decode milestone. T052 deliberately stops before audio/DSP.

## Objective

Implement the first pure JS8 PHY building block:

```text
75 payload bits
    -> JS8 CRC-12
    -> 87 information bits
    -> LDPC(174,87) systematic codeword
    -> belief-propagation decode
    -> recovered 87 information bits
    -> CRC validation
```

The implementation must match JS8Call-improved v3.0.3 bit-for-bit.

This task establishes golden-vector parity before any waterfall, tone, WAV, UI, JSC, or application-protocol work.

## Current context

Canonical implementation roadmap:

```text
docs/js8/implementation-plan.md

M1A  CRC-12 + LDPC(174,87) golden vectors
M1B  frame/tone encoder vectors
M1C  MiniFT8-style monitor/candidate decoder
M1D  exact 75-bit payload from WAV
M1E  Linux js8_decode utility
```

Current MiniShell already contains:

```text
apps/js8chat/
    README.md
    resources/
    tools/
```

No JS8 application source has been implemented yet.

Do not implement this task in the temporary/local `~/projects/js8chat` repository. The canonical implementation repository is:

```text
wcheng95/MiniShell
```

The local old repo is only a source of external test assets such as `A_2_1.wav`.

## Source of truth

Read before editing:

```text
AGENTS.md
docs/js8/README.md
docs/js8/architecture.md
docs/js8/implementation-plan.md
docs/js8/js8-phy.md

apps/ft8/src/ft8_engine/README.md
apps/ft8/src/ft8_engine/ft8_crc.[ch]
apps/ft8/src/ft8_engine/ft8_ldpc.[ch]
```

Frozen interoperability reference:

```text
repository: JS8Call-improved/JS8Call-improved
tag:        v3.0.3
file:       JS8_Mode/JS8.cpp
```

Normative v3.0.3 implementation details in `JS8_Mode/JS8.cpp`:

```text
N  = 174
K  = 87
M  = 87
KK = 87

BP_MAX_ROWS       = 7
BP_MAX_CHECKS     = 3
BP_MAX_ITERATIONS = 30
```

CRC behavior:

```cpp
boost::augmented_crc<12, 0xc06>(range.data(), range.size()) ^ 42
```

The encoder computes CRC over an 11-byte MSB-first buffer in which:

```text
bits 0..74   = JS8 payload
bits 75..87  = zero while CRC is computed
```

Then the 12 CRC bits occupy:

```text
bits 75..86
bit  87      = unused/padding zero in the 11-byte storage
```

The resulting 87 information bits are the actual JS8 LDPC information word.

LDPC codeword ordering in v3.0.3 is:

```text
codeword[0..86]    = 87 parity bits
codeword[87..173]  = 87 information bits
```

This ordering is important. The BP decoder explicitly recovers the information word from the last 87 codeword bits.

The parity generator is the 87x87 matrix beginning at the upstream `constexpr auto parity` definition.

The BP decoder parity-check adjacency is the upstream `Mn` / `Nm` data used by `bpdecode174()`.

Do not substitute FT8's LDPC(174,91), CRC-14, generator matrix, or codeword ordering.

## Architectural constraints

1. **Normal mode only.**
   - No Fast/B.
   - No JS8 40/C.
   - No Slow/E.
   - No JS8 60/I.
   - No generic submode parameterization.

2. Pure platform-independent C.
   - No Qt.
   - No Boost dependency in MiniShell code.
   - No FFTW.
   - No MiniShell API.
   - No POSIX/Linux API.
   - No ESP-IDF/NuttX API.

3. No heap allocation.

4. No mutable global decoder state.

5. Fixed tables may be `static const` read-only data.

6. Keep the API small and explicit. This is a JS8 PHY primitive, not an application/message codec.

7. Do not modify FT8 behavior or tables.

8. Do not refactor FT8 into a generic FTx/JS8 engine during this task.

9. Preserve MSB-first bit ordering exactly as v3.0.3.

10. Prefer fixed-size integer/float arrays with compile-time dimensions.

## Implementation scope

Create a JS8 engine PHY directory, for example:

```text
apps/js8chat/src/js8_engine/
    js8_crc.c
    js8_crc.h
    js8_ldpc.c
    js8_ldpc.h
```

Exact naming may differ slightly, but keep the ownership explicit under `apps/js8chat/src/js8_engine/`.

Suggested constants:

```c
#define JS8_PAYLOAD_BITS   75u
#define JS8_CRC_BITS       12u
#define JS8_INFO_BITS      87u
#define JS8_CODEWORD_BITS 174u
```

A suitable low-level API may expose operations equivalent to:

```c
/* payload_bits and info_bits contain values 0/1, MSB-first protocol order. */
int js8_crc12_append(const uint8_t payload_bits[JS8_PAYLOAD_BITS],
                     uint8_t info_bits[JS8_INFO_BITS]);

int js8_crc12_check(const uint8_t info_bits[JS8_INFO_BITS]);

int js8_ldpc_encode(const uint8_t info_bits[JS8_INFO_BITS],
                    uint8_t codeword[JS8_CODEWORD_BITS]);

int js8_ldpc_decode(const float llr[JS8_CODEWORD_BITS],
                    uint8_t info_bits[JS8_INFO_BITS],
                    uint8_t codeword[JS8_CODEWORD_BITS],
                    int *hard_errors);
```

The exact signatures can differ if there is a clear reason, but preserve these boundaries:

- CRC can be tested independently;
- LDPC encode can be tested independently;
- LDPC BP decode can be tested independently;
- bit ordering is explicit;
- caller owns all storage;
- no heap.

For LLR sign convention, match upstream `bpdecode174()`:

```text
positive LLR -> bit 1
negative LLR -> bit 0
```

Document that convention in the header/test.

## Golden vectors

T052 must check fixed reference vectors into MiniShell tests.

Do not require the upstream repository at test runtime.

Generate the reference vectors once from **exact JS8Call-improved v3.0.3 behavior** and record provenance in the test source or a small vector README.

At minimum include three deterministic 75-bit payload patterns:

```text
1. all zeros
2. a nontrivial fixed alternating/patterned payload
3. a second nontrivial fixed pseudo-random/static payload
```

For each vector lock:

```text
75 input payload bits
12 expected CRC bits / CRC numeric value
87 expected information bits
174 expected codeword bits
```

The golden values must come from the pinned upstream algorithm, not from the new MiniShell implementation itself.

If a small one-off oracle helper is used to extract vectors from v3.0.3, keep it outside production code. It does not need to be committed unless it adds durable value.

## Decoder tests

For every golden codeword:

1. convert codeword bits into strong noiseless LLRs, e.g. fixed magnitude with upstream sign convention;
2. run the BP decoder;
3. require success;
4. require exact 87-bit information recovery;
5. require CRC pass;
6. require exact 75-bit payload recovery.

Also test:

- one deliberately corrupted CRC bit causes CRC validation failure;
- invalid/null arguments are rejected where the API defines validation;
- output remains bounded and deterministic.

Optional but useful within scope:

- flip a small number of codeword hard decisions while retaining stronger correct LLR evidence and prove BP recovery, if this remains a deterministic unit test.

Do not turn T052 into a sensitivity/performance study.

## Table validation

Add structural/static tests sufficient to catch accidental table corruption.

At minimum prove:

```text
LDPC generator dimensions = 87 x 87
BP codeword bits          = 174
BP checks                 = 87
max checks per bit        = 3
max neighbors per check   = 7
```

If tables are represented in compressed form, add an integrity test or fixed digest/count that makes unintended changes obvious.

## Build integration

Add only the build/test integration required for the pure JS8 PHY module.

The module does not need to be registered as a runnable MiniShell app yet.

Do not add UI, command dispatch, Audio, Time, Radio, storage, or platform bindings.

## Non-goals

Do **not** implement in T052:

- `A_2_1.wav` decoding;
- WAV parsing;
- waterfall/FFT;
- candidate search;
- Costas search;
- 79-tone construction;
- symbol Gray mapping;
- waveform generation;
- frame/message text packing;
- callsign packing;
- compound callsigns;
- directed messages;
- heartbeat/CQ;
- Huffman;
- JSC;
- conversation state;
- UI;
- TX scheduler;
- MiniShell app registration;
- live Audio/UAC;
- QMX CAT;
- ADV optimization;
- other JS8 speeds.

The architect's `~/projects/js8chat/A_2_1.wav` fixture is explicitly reserved for M1D/M1E.

## Acceptance criteria

Software gate:

- [ ] pure JS8 CRC-12 implementation matches v3.0.3;
- [ ] exact 75 -> 87 bit formation matches v3.0.3;
- [ ] exact LDPC(174,87) encoder matches v3.0.3;
- [ ] codeword ordering is parity[87] followed by information[87];
- [ ] BP decoder matches v3.0.3 parity-check behavior;
- [ ] noiseless golden codewords decode to exact information words;
- [ ] recovered information words pass CRC;
- [ ] corrupted CRC is rejected;
- [ ] at least three fixed upstream-derived golden vectors are checked in;
- [ ] no heap allocation;
- [ ] no mutable global state;
- [ ] no Qt/Boost/FFTW/platform dependency;
- [ ] only JS8 Normal assumptions are represented;
- [ ] no changes to FT8 behavior;
- [ ] Linux full CTest passes;
- [ ] portable unit tests pass where applicable;
- [ ] architecture/platform boundary checks remain green;
- [ ] real ADV build remains green;
- [ ] `git diff --check` passes;
- [ ] no unrelated cleanup.

There is no RF/manual hardware validation for T052.

## Automated tests

Run before handoff:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T052-build-unit
cmake --build /tmp/T052-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T052-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

If the generic app boundary scripts do not yet recognize `js8chat`, make only the minimal test-discovery change needed to cover the new pure source tree. Do not weaken the boundary rules.

## Manual / hardware validation

None.

The WAV fixture is intentionally not used yet.

## Branch workflow

Use:

```text
codex/T052-js8-crc-ldpc
```

Codex:

1. read `AGENTS.md`, canonical JS8 docs, and this task;
2. inspect the current FT8 CRC/LDPC style for architecture only;
3. inspect exact JS8Call-improved v3.0.3 `JS8_Mode/JS8.cpp` behavior;
4. derive and record fixed v3.0.3 golden vectors;
5. implement only the pure JS8 CRC/LDPC core;
6. add deterministic tests;
7. run all required local gates;
8. set this task Status to `REVIEW`;
9. fill the implementation notes below;
10. commit and push one reviewable commit;
11. return the commit SHA;
12. no PR;
13. no GitHub Actions wait.

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

No manual/hardware test is required for T052.
