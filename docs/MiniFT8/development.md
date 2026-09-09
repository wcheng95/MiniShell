# MiniFT8-V3 Development

MiniFT8 runs as a MiniShell app with independent RX Audio, TX Audio, and Control resources.

Audio V1 transport for MiniFT8 is 12 kHz/S16/two-channel. MiniShell preserves channel order; MiniFT8 source profiles decide ordinary-audio versus I/Q meaning. `tests/kfs16b12k.wav` is the deterministic MiniShell Audio reference.

The MiniShell H1-H5 housekeeping audit and final boundary review are complete. No known MiniShell debt blocks MiniFT8 RX work.

## Current priority: V1 cross-backend/profile validation

RX-1A is complete and remains the frozen decoder/golden baseline. RX-1B is intentionally **paused** while MiniShell and MiniFT8 complete the two-backend/two-presentation validation checkpoint.

Current validation matrix:

```text
Linux backend + DESKTOP presentation   P1 PASS
Linux backend + ADV presentation       P1 PASS
ADV backend   + ADV presentation       P2 PASS
```

P1 and P2 are complete.

P1 introduced application-level presentation profiles without duplicating MiniFT8 logic:

```text
DESKTOP   30 x 8, contextual footer
ADV       20 x 7, six main lines, no footer
```

Linux launches both explicitly:

```text
M$> ft8 --profile desktop
M$> ft8 --profile adv
```

Presentation is launch policy. It is not inferred from backend identity and is not persisted in `station.txt`. The O-screen `Profile` value remains a separate station/operating-profile concept.

P2 packages the same `ft8` application sources into the Cardputer ADV static registry and selects the ADV presentation at the ADV composition edge. Real hardware validation passed for app discovery, launch/exit, ADV 20x7 presentation, configuration persistence across launches, and repeated `ft8` cycles.

P2 also exposed a runtime-stack ownership problem: substantial foreground applications should not borrow ESP-IDF's `app_main` stack. ADV foreground applications now execute synchronously on a MiniShell-managed 16 KiB application task while the resident shell remains on its own task. The portable `free` utility is packaged on ADV to observe application memory headroom. The validated pre/post-`ft8` baseline was approximately:

```text
heap free       282 KiB
largest block   228 KiB
app allocations 0 after exit
```

Repeated `ft8` launch/exit cycles left the memory baseline effectively unchanged.

V1 now compares Linux+ADV against ADV+ADV at the application-visible level and closes the cross-backend/profile checkpoint.

Cardputer ADV V1 uses static application composition: MiniShell and MiniFT8 are compiled into one ESP-IDF firmware image. Runtime ELF/application loading is deferred for later exploration; it is not required for the first ADV backend.

MiniFT8-V2 is reference material for proven Cardputer hardware behavior only. V2 is not modified or refactored as part of this work.

Canonical plan:

```text
../project/adv-backend-plan.md
```

RX-1B resumes after V1 passes.

## Deferred milestone: decode RX

Canonical RX architecture and staged development are in `rx.md`.

```text
MiniShell Audio
    -> rx_audio_adapter
    -> rx_frontend
    -> rx_slot_framer
    -> ft8_monitor
    -> candidate finder
    -> candidate decoder
    -> protocol message codec
    -> Ft8ProtocolSlot
    -> rx_result_builder
    -> RxBatch
    -> app_controller
    -> RX UI
```

Normal raw audio remains streaming and bounded. The retained normal decode representation is the whole decode-window waterfall. Raw PCM slot retention/double buffering is optional research functionality, never a normal decoder requirement.

## Locked RX rules

1. **Stream raw audio; retain the waterfall; retain raw PCM only by explicit exception.**
2. **V2 SNR is the structural-cleanup baseline.** Candidate sync score and SNR remain separate; SNR improvement is a later deliberate algorithm change.
3. **Protocol message type is first-class output.** Normal typed messages are not rendered and re-tokenized to recover structure.
4. **Free-text CQ exception.** A `FREE_TEXT` message may additionally be classified as logical CQ only when it matches `CQ <nnn|AAAA> <valid-callsign> [grid]`; protocol type remains `FREE_TEXT`.
5. **Station identity may be decoder context, not QSO policy.** Future deep search may use local callsign as an explicit search/prior hint. AutoSeq state, reply decisions, IgnoreList, TX stage, and UI policy remain outside `ft8_engine`.
6. **Hashed callsigns are explicit FT8-engine state.** `Ft8HashStore` persists across slots and is aged explicitly; it is not MiniShell state and must not require a global table.
7. **Exact payload bytes are protocol-message identity.** CRC/hash values may accelerate lookup/dedupe but are not collision-free identity.

## RX-0 — architecture and V2 source review — complete

Completed reviews:

```text
V2 tests/tx_e2e/decode_helper.cpp
V2 production decode_monitor_results()
V2 monitor.h / monitor.c
V2 decode.h / decode.c
V2 message.h / message.c
```

Canonical review artifacts:

```text
rx-decoder-contract.md
rx-v2-production-review.md
rx-monitor-review.md
rx-decode-review.md
rx-message-review.md
```

Key conclusions:

```text
monitor mathematics      KEEP
monitor ownership        CLEAN substantially
candidate/LDPC/CRC math  KEEP
message protocol math    KEEP
message representation   CLEAN substantially
hash ownership           CLEAN substantially
platform/device code     stays outside ft8_engine
```

No deliberate DSP/algorithm improvements are mixed into structural cleanup.

## RX-1 — clean FT8 decode core

### RX-1A — freeze golden boundary evidence — complete

Canonical record: `rx-golden.md`.

MiniFT8-V2 algorithm baseline is pinned at:

```text
5bd3ef98f72388a850bebad04bd7300b90edb63c
```

A V2 host regression harness now freezes three boundaries:

```text
PCM -> exact active waterfall bytes/hash
waterfall -> exact unique valid payload
fixed payload -> protocol type + canonical text
```

Reference tests live in the V2 repository:

```text
tests/tx_e2e/test_rx1a_boundaries.cpp
tests/tx_e2e/rx1a_reference_dump.cpp
.github/workflows/rx1a-reference.yml
```

The RX-1A workflow passes the existing `golden_rx` test and the new boundary regression on Ubuntu 24.04 x86-64 with GCC/G++ 13.3.0.

Hard structural anchors include:

```text
FT8 active-waterfall FNV-1a-64  18BE1E838FD9C6AF
FT4 active-waterfall FNV-1a-64  CBDD2509276E5030
unique CQ payload                000000206016500A1988
```

Fixed codec vectors cover:

```text
STANDARD CQ
ARRL Field Day
DXpedition
non-standard-call CQ
FREE_TEXT CQ-shaped message
```

RX-1A also clarified three V2 points:

- type 0.6 `CONTESTING` is simply outside current V2 supported message scope; RX-1 does not add support merely because the enum names it;
- the generic telemetry decode buffer was too small for 18 hex characters + NUL; this was fixed separately in MiniFT8-V2 PR #44 with a direct regression test and no decoder-math change;
- the **production V2 callsign hashtable is correct** for 22-, 12-, and 10-bit lookups. The simplified `decode_helper.cpp` host-test map is not the production table and should not be used to judge V2 hashtable correctness.

These distinctions remain explicit so structural refactoring neither invents new protocol scope nor “fixes” behavior that was already correct.

### RX-1B — top-down RX module/interface design — PAUSED

No decoder source migration begins until RX-1B is complete. RX-1B resumes only after the ADV backend/profile validation checkpoint passes.

Design order when resumed:

```text
RX goal
  -> top-level responsibilities
  -> module boundaries
  -> single ownership of mutable state/resources
  -> data contracts
  -> lifecycle/state transitions
  -> memory/workspace ownership
  -> dependency direction
  -> error/status contracts
  -> unit-test boundaries
  -> only then source migration
```

RX-1B must answer at least:

- Which logical RX blocks become independently testable modules versus private files inside one owner?
- What exact MiniFT8-owned types cross each boundary?
- Which raw `ft8_lib` types remain private inside `ft8_engine`?
- Who owns `Ft8Monitor`, waterfall storage, candidate storage, decode scratch, and `Ft8HashStore`?
- How are `begin_new_decode_window` and `reset_stream` represented?
- What is the explicit decode profile/search context, including future deep-search hints?
- What are the typed protocol-message variants and parse-status semantics?
- What is the exact `Ft8ProtocolSlot -> rx_result_builder -> RxBatch` contract?
- Which unit test is responsible for each boundary and invariant?

The likely architecture remains conceptually:

```text
RX
|
+-- rx_audio_adapter
+-- rx_frontend
+-- rx_slot_framer
+-- ft8_engine
|     +-- monitor/waterfall
|     +-- candidate search
|     +-- candidate decode / LDPC / CRC
|     +-- protocol message codec
|     `-- hash store
`-- rx_result_builder
```

but RX-1B, not the V2 directory layout, decides the final module/file structure.

### RX-1C and later — implementation only after RX-1B

The implementation sequence will be finalized by RX-1B. Current expected direction is:

```text
RX-1C  clean monitor ownership/workspace/lifecycle
RX-1D  bring candidate + likelihood + LDPC + CRC core across
RX-1E  implement explicit Ft8HashStore
RX-1F  implement typed protocol message codec
RX-1G  pure cleaned decoder golden regression
```

These labels may change if RX-1B finds a cleaner dependency order.

## Later RX stages

After the cleaned FT8 core is stable:

```text
RX-2  pure MiniShell-independent host FT8 decoder
RX-3  MiniFT8 RX frontend: 12k S16 2ch -> engine-native stream
RX-4  streaming slot framing
RX-5  assemble pure RX -> Ft8ProtocolSlot -> RxBatch
RX-6  MiniShell WAV Audio integration
RX-7  real decoded RX screen
```

At RX-7, stop the milestone. AutoSeq, TX, and ADIF are separate major blocks and are not pulled into RX merely to demonstrate an end-to-end QSO.
