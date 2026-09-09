# MiniFT8-V3 Development

MiniFT8 runs as a MiniShell app with independent RX Audio, TX Audio, and Control resources.

Audio V1 transport for MiniFT8 is 12 kHz/S16/two-channel. MiniShell preserves channel order; MiniFT8 source profiles decide ordinary-audio versus I/Q meaning. `tests/kfs16b12k.wav` is the deterministic MiniShell Audio reference.

The MiniShell H1-H5 housekeeping audit and final boundary review are complete. No known MiniShell debt blocks MiniFT8 RX work.

## Current priority: RX-2 pure host FT8 decoder/use harness

RX-1A golden boundaries, RX-1B top-down ownership design, RX-1C monitor cleanup, RX-1D candidate/LDPC/CRC migration, RX-1E explicit callsign-hash ownership, RX-1F typed protocol codec, RX-1G pure `Ft8Engine` assembly/golden regression, and the P1/P2/V1 cross-backend/profile checkpoint are complete.

Validated platform/presentation matrix:

```text
Linux backend + DESKTOP presentation   P1/V1 PASS
Linux backend + ADV presentation       P1/V1 PASS
ADV backend   + ADV presentation       P2/V1 PASS
```

Canonical V1 record:

```text
v1-validation.md
```

The checkpoint verified application launch/exit, shared UI state transitions, configuration persistence, ADV 20x7 presentation behavior, repeated real-hardware `ft8` cycles, memory stability, and the absence of direct platform dependencies under `apps/ft8/`.

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

Cardputer ADV V1 uses static application composition: MiniShell and MiniFT8 are compiled into one ESP-IDF firmware image. Runtime ELF/application loading is deferred for later exploration; it is not required for the first ADV backend.

MiniFT8-V2 is reference material for proven behavior and algorithms. V2 structure is not copied wholesale into V3.

Canonical platform/profile plan:

```text
../project/adv-backend-plan.md
```

## Current milestone: decode RX

Canonical RX architecture and staged development are in `rx.md`.

Canonical completed design/implementation records:

```text
rx-1b-design.md
rx-1c-monitor.md
rx-1d-decoder.md
rx-1e-hash-store.md
rx-1f-message-codec.md
rx-1g-engine.md
```

The locked receive shape is:

```text
MiniShell Audio
12 kHz / S16 / 2-channel
    -> rx_audio_adapter
    -> rx_frontend
       6 kHz mono float
    -> rx_slot_framer
       exact 960-sample FT8 engine blocks
    -> Ft8Engine
       monitor/waterfall
       candidate search
       likelihood/LDPC/CRC
       Ft8HashStore
       typed protocol message codec
       exact-payload dedupe
    -> Ft8ProtocolSlot
    -> rx_result_builder
    -> RxBatch
    -> app_controller
    -> RX UI
```

`app_controller` remains the sole application coordinator. The RX modules are passive/stateful owners underneath it; no second RX manager/coordinator is introduced.

Normal raw audio remains streaming and bounded. The retained normal decode representation is the whole decode-window waterfall. Raw PCM slot retention/double buffering is optional research functionality, never a normal decoder requirement.

## Locked RX rules

1. **Stream raw audio; retain the waterfall; retain raw PCM only by explicit exception.**
2. **Preserve the proven 6 kHz FT8-engine boundary during ownership cleanup.** MiniShell remains 12 kHz/S16/two-channel; `rx_frontend` owns adaptation to 6 kHz mono float. Any future engine-rate change is a separate DSP experiment.
3. **V2 SNR is the structural-cleanup baseline.** Candidate sync score and SNR remain separate; SNR improvement is a later deliberate algorithm change.
4. **Protocol message type is first-class output.** Normal typed messages are not rendered and re-tokenized to recover structure.
5. **Free-text CQ exception.** A `FREE_TEXT` message may additionally be classified as logical CQ only when it matches `CQ <nnn|AAAA> <valid-callsign> [grid]`; protocol type remains `FREE_TEXT`.
6. **Station identity may be decoder context, not QSO policy.** Future deep search may use local callsign as an explicit search/prior hint. AutoSeq state, reply decisions, IgnoreList, TX stage, and UI policy remain outside `ft8_engine`.
7. **Hashed callsigns are explicit FT8-engine state.** `Ft8HashStore` persists across slots, is owned by one `Ft8Engine`, and is aged explicitly once per completed slot transition; it is not MiniShell state and has no global table.
8. **Exact payload bytes are protocol-message identity.** CRC/hash values may accelerate lookup/dedupe but are not collision-free identity.
9. **Ownership/interface cleanup is not an optimization project.** Preserve FFT, OSR, candidate search, LDPC, SNR, hash lookup, message unpacking, and other proven algorithm behavior unless a structural blocker forces a narrowly documented exception.
10. **Mathematical equivalence is not enough during DSP cleanup.** Preserve expression-level float behavior when the frozen golden proves that reassociation changes output.

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

A V2 host regression harness freezes three boundaries:

```text
PCM -> exact active waterfall bytes/hash
waterfall -> exact unique valid payload
fixed payload -> protocol type + canonical text
```

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

- type 0.6 `CONTESTING` is outside current V2 supported message scope; RX-1 does not add support merely because the enum names it;
- the generic telemetry decode buffer issue was fixed separately in MiniFT8-V2 with a direct regression test and no decoder-math change;
- the production V2 callsign hashtable is correct for 22-, 12-, and 10-bit lookups. The simplified `decode_helper.cpp` host-test map is not the production table.

### RX-1B — top-down RX module/interface design — complete

Canonical record:

```text
rx-1b-design.md
```

RX-1B fixed five application-level modules:

```text
rx_audio_adapter
rx_frontend
rx_slot_framer
ft8_engine
rx_result_builder
```

It also fixed:

```text
MiniShell 12 kHz/S16/2ch -> engine 6 kHz mono-float boundary
960-sample FT8 monitor block contract
single owner for every mutable RX resource/state
caller allocation / ft8_engine logical workspace ownership
raw ft8_lib types private to ft8_engine
initial UTC reference + sample-count slot progression
new decode window versus stream discontinuity semantics
error/status distinctions
unit-test ownership by module boundary
```

No production decoder source was migrated during RX-1B.

### RX-1C — clean monitor ownership/workspace/lifecycle — complete

Canonical record:

```text
rx-1c-monitor.md
```

RX-1C introduced the first cleaned `ft8_engine` implementation boundary:

```text
6 kHz mono float
    -> explicit Ft8Monitor
    -> compact waterfall
```

The monitor now has one explicit instance, caller-supplied/queryable workspace, no allocator dependency, no mutable DSP singleton, explicit failure/full status, and explicit begin-window versus reset-stream semantics.

On the Linux RX-1C reference build the baseline workspace query returns 105808 bytes. This is a host measurement rather than a hard-coded target requirement; 32-bit embedded alignment/FFT-plan size may differ slightly.

Tests prove two-instance independence, explicit invalid/workspace/full behavior, new-window history preservation, stream-reset history clearing, safe destruction, exact V2 dimensions, and the exact active waterfall fingerprint:

```text
18BE1E838FD9C6AF
```

The first refactor attempt failed the exact golden even though dimensions matched; restoring V2's exact Hann float multiplication grouping restored the fingerprint. No golden value was changed.

### RX-1D — candidate + likelihood/LDPC/CRC boundary — complete

Canonical record:

```text
rx-1d-decoder.md
```

RX-1D implements:

```text
Ft8WaterfallView
    -> FT8 Costas candidate search
    -> max-log likelihood extraction
    -> BP-LDPC
    -> CRC-14
    -> Ft8DecodedPayload
```

The cleaned boundary returns a validated exact 10-byte payload plus candidate/LDPC/CRC diagnostics; it does not depend on `ftx_message_t`, text rendering, or callsign hash state.

Pinned structural policy remains:

```text
candidate capacity      50
minimum sync score       5
max LDPC iterations     25
time search             -10..19 blocks
```

RX-1D also removes V2's pointer-before-array candidate addressing by resolving absolute waterfall blocks before constructing pointers. The LDPC implementation keeps one canonical parity-check topology and reconstructs the reverse three-check references deterministically in V2 row order rather than storing duplicated topology tables.

Unit and pinned-V2 reference tests pass. The exact RX-1A valid payload remains:

```text
000000206016500A1988
```

Candidate score/order remain diagnostics rather than permanent golden identity.

### RX-1E — explicit per-engine Ft8HashStore — complete

Canonical record:

```text
rx-1e-hash-store.md
```

RX-1E replaces V2's global/static callsign table with one explicit caller-owned `Ft8HashStore` instance suitable for one future `Ft8Engine` instance.

The pinned V2 production behavior is preserved:

```text
capacity                     128 entries
callsign                     11 chars + NUL
entry size                   16 bytes
stored hash                  full 22-bit hash
lookup                       22 / 12 / 10 bits
bucket                       (top10 * 23) % 128
age                          uint8, saturating
save/lookup                  refresh age to zero
trim-created holes           full-table lookup scan
full-table policy            trim to 78, then insert -> 79
```

The store has no time/platform dependency. `age_slot()` is an explicit lifecycle call.

Unit coverage proves independent instances, 22/12/10-bit lookup, same-full-hash replacement, age refresh, oldest-entry trimming, lookup through trim-created holes, the V2 full-table trim policy, and invalid-input handling.

Current Linux and prior-stage regressions pass.

### RX-1F — typed protocol message codec + Ft8ProtocolSlot — complete

Canonical record:

```text
rx-1f-message-codec.md
```

RX-1F migrates the supported V2 receive message codec into MiniFT8-owned typed results:

```text
Ft8DecodedPayload
    -> protocol type classification
    -> typed structured unpacking
    -> Ft8HashStore lookup/save
    -> canonical protocol text
    -> Ft8ProtocolMessage
    -> exact-payload dedupe
    -> Ft8ProtocolSlot
```

Protocol type and parse status are independent. Supported families are `STANDARD`, `NONSTD_CALL`, `FREE_TEXT`, `DXPEDITION`, `ARRL_FD`, and `TELEMETRY`; recognized-but-unimplemented families remain explicit `UNSUPPORTED` results. Type `0.6` remains `UNKNOWN` as in the pinned V2 baseline.

The five fixed RX-1A codec vectors reproduce their exact canonical text. Hash resolution uses the explicit RX-1E store directly for 22-, 12-, and 10-bit lookups. A hash miss remains a valid protocol parse rendered as `<...>` and is marked `has_unresolved_hash=true`.

`Ft8ProtocolSlot` uses caller-supplied message storage and exact 10-byte payload comparison for dedupe; it introduces no hidden allocator or slot-sized message buffer.

### RX-1G — pure cleaned Ft8Engine golden regression — complete

Canonical record:

```text
rx-1g-engine.md
```

RX-1G adds the explicit pure engine owner/lifecycle:

```text
Ft8Engine
    -> Ft8Monitor
    -> candidate search + LDPC/CRC
    -> Ft8HashStore
    -> typed message codec
    -> exact-payload dedupe
    -> Ft8ProtocolSlot
```

The engine is caller-owned, takes caller-supplied/queryable DSP workspace, and has no MiniShell or platform dependency. It owns one monitor, one candidate array, one persistent hash store, and current window/slot state.

Lifecycle is explicit:

```text
query requirements
init
begin_window(slot_id)
process exact 960-sample blocks
finalize_window -> Ft8ProtocolSlot
reset_stream on discontinuity
repeat
destroy
```

`reset_stream()` clears monitor continuity and cancels the active window while preserving callsign-hash knowledge. Beginning a new window after a completed window is the explicit owner of once-per-slot hash aging.

The dedicated pinned-V2 RX-1G CI golden passes:

```text
6 kHz WAV
    -> Ft8Engine
    -> message_count = 1
       payload = 000000206016500A1988
       type    = STANDARD
       text    = CQ W1XYZ FN42
```

The normal Linux suite and RX-1G lifecycle/unit tests pass on the code-bearing head.

## RX-2 — pure MiniShell-independent host decoder/use harness — NEXT

Goal:

> Put the completed `Ft8Engine` behind a small host-side use boundary that consumes engine-native 6 kHz PCM and returns typed protocol messages without MiniShell, UI, station-aware result building, or platform-specific dependencies.

RX-2 should make the cleaned engine straightforward to exercise as a reusable pure decoder before introducing the 12 kHz frontend and slot framer.

## Later RX stages

```text
RX-3  MiniFT8 RX frontend: 12k S16 2ch -> 6k mono float
RX-4  streaming slot framing with exact 960-sample engine blocks
RX-5  assemble pure RX -> Ft8ProtocolSlot -> RxBatch
RX-6  MiniShell WAV Audio integration
RX-7  real decoded RX screen
```

At RX-7, stop the milestone. AutoSeq, TX, and ADIF are separate major blocks and are not pulled into RX merely to demonstrate an end-to-end QSO.
