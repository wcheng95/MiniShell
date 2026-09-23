# T052 — JS8 Normal CRC-12 and LDPC(174,87) core

Status: COMPLETE

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

- [x] pure JS8 CRC-12 implementation matches v3.0.3;
- [x] exact 75 -> 87 bit formation matches v3.0.3;
- [x] exact LDPC(174,87) encoder matches v3.0.3;
- [x] codeword ordering is parity[87] followed by information[87];
- [x] BP decoder matches v3.0.3 parity-check behavior;
- [x] noiseless golden codewords decode to exact information words;
- [x] recovered information words pass CRC;
- [x] corrupted CRC is rejected;
- [x] at least three fixed upstream-derived golden vectors are checked in;
- [x] no heap allocation;
- [x] no mutable global state;
- [x] no Qt/Boost/FFTW/platform dependency;
- [x] only JS8 Normal assumptions are represented;
- [x] no changes to FT8 behavior;
- [x] Linux full CTest passes;
- [x] portable unit tests pass where applicable;
- [x] architecture/platform boundary checks remain green;
- [x] real ADV build remains green;
- [x] `git diff --check` passes;
- [x] no unrelated cleanup.

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

The generic app boundary scripts do not yet define `js8chat`. Add the minimal `APP_RULES["js8chat"]` entry required for this task:

```text
enforced root:  src
module owner:   js8_engine -> src/js8_engine
allowed deps:   js8_engine -> js8_engine only
no-heap module: js8_engine
```

Do not add permissions for MiniShell API, platform code, other apps, or FT8 internals. Later JS8Chat tasks can extend the rule as new owned modules are introduced.

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

### Implementation summary

Implemented the pure Normal-mode CRC-12 and systematic LDPC(174,87) encoder
and BP decoder. The CRC performs augmented polynomial division over the exact
88-bit upstream buffer (75 payload bits plus 13 zeros), then XORs with 42.
The decoder preserves upstream adjacency order, sign convention, 30-update
limit, initial syndrome check and stalled-syndrome early exit. Check-message
and tanh scratch are fused without changing message evaluation order.

Three checked-in vectors were generated independently by compiling extracted
v3.0.3 C++ parity/CRC/BP definitions with Boost, without linking MiniShell.
The oracle also confirms exact recovery with three weak wrong-sign decisions.
Source hash, regeneration command and table digest are in `tests/js8_vectors.md`.

### Files changed

- `apps/js8chat/src/js8_engine/js8_crc.[ch]`: independent CRC append/check.
- `apps/js8chat/src/js8_engine/js8_ldpc.[ch]`: encode/decode and explicit contracts.
- `apps/js8chat/src/js8_engine/js8_ldpc_tables.h`: pinned generator and BP tables.
- `tests/js8_phy_test.c`, `tests/js8_golden_vectors.h`: fixed vectors, noiseless
  and three-error recovery, CRC rejection, invalid arguments, output canaries,
  repeatability, nonconvergence and independent CRC/LDPC gates.
- `tests/js8_ldpc_tables_test.py`: dimensions, upstream-derived SHA-256,
  reciprocal 522-edge graph and all 87 generator columns against every check.
- `tests/js8_reference_oracle.py`, `tests/js8_vectors.md`: offline reproducible
  oracle and provenance; not used by the normal test suite.
- `tests/js8_tests.cmake`, `CMakeLists.txt`, `tests/unit/CMakeLists.txt`:
  pure library and shared tests. The standalone portable harness lacked
  `enable_testing()` and registration of its existing service-test executable;
  enabled both so the required portable CTest command actually runs tests.
- `tests/architecture_rules.py`: js8_engine-only dependency/no-heap rule.
- This task packet: gate evidence and REVIEW handoff.

### Invariants preserved

MSB-first 0/1 arrays; 75 payload + 12 CRC bits; parity[87] before info[87];
positive LLR means 1. CRC and LDPC validation remain independent. All pointers
are required; invalid bits/nonfinite input LLRs return -1 without output writes.
Nonconvergence returns 1, zero information, last codeword decisions and -1 hard
errors. Success returns 0 with the upstream hard-error count.

No heap, mutable global state, MiniShell/platform APIs, FT8 changes, application
registration, audio, tones, message codec or other submodes. Fixed scratch is
local to each decode call. No architectural or behavioral scope deviations.

### Local tests run

All commands run from the MiniShell repository unless noted. Final results:

```sh
git status --short
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: 91/91, including all existing architecture/platform checks.

cmake -S tests/unit -B /tmp/T052-build-unit
cmake --build /tmp/T052-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T052-build-unit --output-on-failure
# PASS: 15/15.

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat
# Both PASS.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: minishell_adv.bin 0x151790 bytes; app partition 78% free.

python3 tests/js8_reference_oracle.py /tmp/T052-JS8.cpp > /tmp/T052-vectors.h
cmp tests/js8_golden_vectors.h /tmp/T052-vectors.h
# PASS: exact regeneration; upstream noiseless and three-error BP checks pass.

cc -std=c11 -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -Iapps/js8chat/src/js8_engine tests/js8_phy_test.c \
  apps/js8chat/src/js8_engine/js8_crc.c \
  apps/js8chat/src/js8_engine/js8_ldpc.c -lm -o /tmp/T052-js8-sanitize
/tmp/T052-js8-sanitize
# PASS outside sandbox; ASan/UBSan report no errors.

git diff --check
# PASS.
```

The initial table-integrity test failed because its new initializer parser
omitted the closing brace of the one-dimensional length array. Fixed the
parser; all table checks and both full CTest suites then passed. No product
test was weakened. The first sanitizer invocation hit LeakSanitizer's ptrace
restriction inside the sandbox; the approved outside-sandbox rerun passed.

### Manual/hardware validation still required

None for T052. No flashing or RF tests performed. The WAV fixture was not used.
The ADV build checks the existing firmware; JS8 is not registered or linked into
that firmware at this stage, as specified.

### Known limitations / risks

BP convergence/sensitivity beyond these deterministic vectors is not claimed.
The upstream un-clipped tanh/atanh arithmetic is retained; extreme finite input
LLRs can saturate internal messages, as documented in the vector README.
All decoder work is bounded; there is no sensitivity/performance study.

Pre-existing untracked `build-keyer.sh`, `rebuild-all.sh`,
`platform/adv/elf_apps/keyer/` and Python cache directories were left untouched
and excluded from the commit.

### Commit

The reviewed implementation commit is:

`39023694a59ad06ef86051d98931d79703e67e3c`

No PR or GitHub Actions wait.

## Supervisor review

Reviewed commit `39023694a59ad06ef86051d98931d79703e67e3c` against T052 and the pinned JS8Call-improved v3.0.3 source.

Result: **PASS**.

Review findings:

- Diff is one bounded implementation commit, one commit ahead of the T052 baseline, with no FT8 product-source changes.
- CRC implementation reproduces the upstream augmented CRC-12 over 75 payload bits plus 13 zero bits, polynomial `0xC06`, followed by XOR 42. Independent supervisor spot-check reproduced golden CRC values 42, 3508, and 1822.
- LDPC codeword ordering is exactly parity[87] followed by information[87].
- BP decoder preserves upstream initial syndrome check, positive-LLR-is-1 convention, check/variable update order, 30-update limit, hard-error count, and stalled-syndrome early exit.
- Generator/check tables are pinned and structurally validated. Tests verify all 522 reciprocal graph edges and every one-hot information column satisfies all 87 parity checks (`H * G^T == 0`).
- Golden vectors are generated by an offline oracle that hashes and extracts the exact v3.0.3 CRC/parity/BP definitions; normal tests do not depend on upstream, Boost, network access, or C++.
- JS8 source remains pure C, no heap, no mutable decoder singleton, and no MiniShell/platform/FT8 dependency.
- The new `js8chat` architecture rule is minimal: `src/js8_engine` may depend only on itself and is marked no-heap.
- The portable-test CMake change registers the already-existing `minishell_unit_tests` executable and enables testing so the required portable CTest command actually exercises it. This is test-harness correction only, not product behavior.
- Reported gates are consistent with the diff: Linux 91/91, portable 15/15, JS8 boundary checks, ADV build, ASan/UBSan, oracle regeneration, and diff check all pass.
- No manual/hardware validation is required for T052.

Main was fast-forwarded to the reviewed implementation commit.

## Architect test result

No manual/hardware test is required for T052. Software review accepted; task complete.
