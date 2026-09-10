# MiniFT8-V3 AutoSeq Boundary and Ownership Audit

Status: **COMPLETE — ready to begin AS-1**

Audit base:

```text
MiniShell main  165de0f21b217f2e96e1a4e7ee8ae283d73fa65f
V2 AutoSeq      491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

Purpose: verify that the current MiniFT8-V3 code has a clean place for AutoSeq, identify boundary gaps before implementation, and prevent the V2 structural coupling from being copied into V3.

## 1. Overall result

The current codebase is structurally ready for AutoSeq.

No pre-AS architectural refactor is required. The existing RX pipeline already terminates at the correct factual boundary:

```text
Ft8Engine
    -> Ft8ProtocolSlot
    -> RxResultBuilder
    -> RxBatch/RxMessage
    -> app_controller
```

AutoSeq should attach **after `RxResultBuilder` and under `app_controller`**.

Two factual/context inputs are incomplete and must be finished in AS-1 before V2-equivalent QSO contexts are created:

```text
1. station callsign/grid ownership and injection
2. factual RX SNR in RxMessage
```

The UI also needs one new action boundary for selecting an absolute RX-message index. These are localized boundary-completion tasks, not architectural blockers.

## 2. Audit matrix

| Area | Status | Finding | AS action |
| --- | --- | --- | --- |
| `app_controller` | GREEN | Already sole production coordinator; correct attachment point for AutoSeq | Keep owner; add explicit AutoSeq calls/events only here |
| `Ft8Engine` | GREEN | Owns DSP, protocol decode, candidates and `Ft8HashStore` | AutoSeq must not reach into engine/hash state |
| `Ft8HashStore` | GREEN | Explicit per-engine fixed-capacity store; compact hash+age representation | Leave unchanged during AS |
| `RxResultBuilder` | GREEN/YELLOW | Correct factual policy-free boundary; already exposes CQ/to-me/calls/extra/candidate facts | Wire local station callsign; add factual SNR field |
| `RxBatch` lifetime | GREEN | Retained by current RX state until replaced/destroyed | AutoSeq may read during call but must copy QSO-lifetime facts; never retain pointers |
| `ConfigService` | YELLOW | Owns persisted profile/band/Skip-TX1/retry, but not callsign/grid yet | Extend station identity ownership in AS-1 |
| `qso_scheduler` | REPLACE | Current module only mirrors Skip-TX1/max-retry settings; it does not own a real QSO scheduler | Replace with `auto_seq`; do not keep both |
| `UiModel` | GREEN/YELLOW | Good projection boundary; RX/TX are strings only, which keeps UI decoupled | Add queue projection from QsoView; no AutoSeq types in `ui_shell` |
| `ui_shell` | YELLOW | RX paging works, but selecting 1..6 on RX emits no AppAction | Emit absolute RX index only; app_controller resolves it |
| T UIScreen | YELLOW | Paging infrastructure already works; content is prototype `TX queue empty` | Project AutoSeq QsoView into T lines |
| `ft8_main` | GREEN | Owns lifecycle/loop only; no QSO policy | Keep unchanged except normal action/event plumbing |
| MiniShell APIs | GREEN | RX uses public Memory/Audio/FS/Display/Input/Time boundaries correctly | AutoSeq itself uses none of them |
| Logging | FUTURE/YELLOW | V2 AutoSeq invokes ADIF/Cabrillo callbacks directly | Preserve eligibility timing but emit typed event; logging owner performs I/O |
| TX realization | FUTURE/GREEN boundary | Not implemented, which is useful for safe AutoSeq bring-up | AutoSeq emits TxIntent only; no Audio/Control in AS core |
| Architecture docs | DOC DEBT | `architecture.md` section 11 still describes RX as future work | Refresh after/with first AS integration; no code blocker |

## 3. Correct AutoSeq insertion boundary

The audited V3 flow should become:

```text
MiniShell Audio
    -> rx_audio_adapter
    -> RxFrontend
    -> RxSlotFramer
    -> Ft8Engine
    -> Ft8ProtocolSlot
    -> RxResultBuilder
    -> RxBatch
          |
          v
    app_controller
       /       \
      v         v
  auto_seq    UiModel
      |
      v
 QsoView/TxIntent
      |
      +--> UiModel/T screen
      `--> future TX/logging
```

Do not insert AutoSeq inside `Ft8Engine`, `RxResultBuilder`, `ui_shell`, or a MiniShell backend.

## 4. `app_controller` audit

Current state is good:

- it owns `ConfigService`, current scheduler prototype, `StorageService`, and RX state;
- it receives completed `RxBatch` through its own RX event path;
- it builds `UiModel`;
- it applies `AppAction`;
- `ft8_main` does not directly coordinate DSP or scheduler state.

Decision for AS:

```text
AppController
    owns one AutoSeq instance
    invokes it synchronously
    owns event ordering
    maps persistent config -> AutoSeq config
    resolves UI-selected RxMessage
    projects AutoSeq views -> UiModel
    receives AutoSeq events/TxIntent
```

Preferred storage is an embedded fixed-size `AutoSeq` instance rather than an independent heap-owned singleton. Because `AppController` is currently a stack-local object in the foreground app task, AS-2 must measure `sizeof(AutoSeq)` and task stack high-water before locking this choice. Expected queue size is small enough that embedded ownership should remain practical.

Do not use module-static mutable AutoSeq state.

## 5. `qso_scheduler` audit

Current V3 `qso_scheduler` contains only:

```text
skip_tx1
max_retry
```

and simple getters/setters. It currently owns no QSO queue, state machine, timing, TX selection, or retry lifecycle.

Keeping it alongside a real `auto_seq` module would create ambiguous ownership:

```text
ConfigService -> qso_scheduler settings
ConfigService -> auto_seq settings
```

Decision:

> `qso_scheduler` is a prototype placeholder and is removed when the pure `auto_seq` owner is introduced in AS-2.

Persistent values remain owned by `ConfigService`; AutoSeq receives runtime copies/configuration through `app_controller`.

## 6. RX factual boundary audit

`RxMessage` is already the correct input type family for AutoSeq because it contains:

```text
exact payload
protocol type / parse status
unresolved-hash fact
is_cq
is_to_me
canonical text
call_to
call_de
extra
candidate timing/frequency coordinates
LDPC/CRC diagnostics
```

This is substantially cleaner than feeding AutoSeq a display/UI structure.

### Gap A: local station identity

`RxResultBuilder` already supports `config.local_callsign` and uses it to classify `is_to_me`, but `app_controller` currently initializes it from `rx_result_builder_default_config()` without supplying a station callsign. `ConfigService` does not yet parse/store callsign or grid.

Therefore current production RX can classify CQ correctly but cannot yet make configured-station `is_to_me` authoritative.

Required AS-1 ownership:

```text
station.txt
    -> StorageService
    -> ConfigService owns callsign/grid
    -> app_controller
       -> RxResultBuilderConfig.local_callsign
       -> AutoSeq station config
```

Neither `RxResultBuilder` nor AutoSeq reads the file directly.

### Gap B: RX SNR

V2 AutoSeq latches the measured RX SNR when a CQ/QSO is accepted because that value later becomes the report we send and part of logging metadata. Current V3 `RxMessage` has candidate coordinates but no factual SNR.

Required AS-1 rule:

> SNR estimation belongs to RX/decoder factual processing, not AutoSeq.

AutoSeq receives an already-computed SNR value. Do not approximate SNR inside AutoSeq and do not substitute candidate sync score as SNR without an explicit validated rule.

## 7. Hash ownership audit

Current ownership is correct:

```text
Ft8Engine
    contains Ft8HashStore
```

The hash store is fixed at 128 entries and already packs age with the 22-bit canonical hash in one `uint32_t` per entry plus callsign storage.

Decision:

- AutoSeq does not own a second callsign hash table;
- AutoSeq does not resolve hashes;
- AutoSeq consumes the resolved/unresolved result already present in `RxMessage`;
- unresolved-hash behavior remains a protocol/RX fact feeding V2-equivalent policy.

This avoids duplicated RAM and split truth.

## 8. UI/action audit

Current RX rendering pages six messages at a time, but `activate_line()` has no RX action. A displayed line number alone is page-relative, while AutoSeq must act on the exact retained `RxMessage`.

Required action boundary:

```text
APP_ACTION_SELECT_RX_MESSAGE
    value.index = visible_page * 6 + selected_line
```

Ownership:

```text
ui_shell
    computes absolute visible index
    emits AppAction only

app_controller
    validates index against current RxBatch
    reads the RxMessage
    passes it to auto_seq
```

Do not put `RxMessage *`, `QsoContext *`, or AutoSeq APIs in `UiShell`/`AppAction`.

The current generic T paging code already uses `model->tx_count`, so an eight-entry queue naturally gives two ADV pages without a new paging mechanism.

## 9. AutoSeq internal ownership

`auto_seq` will own:

```text
active QSO contexts
inactive QSO contexts
queue ordering
state progression
retry counters
same-parity rotation
inactive reactivation/expiry semantics
CQ/FreeText one-shot state
V2 scheduling priority policy
V2 logging-eligibility policy
```

It will not own:

```text
station.txt persistence
MiniShell Time service
slot boundary detection
RX decoding
hash resolution
UI selection/navigation
FT8 bit packing/LDPC/tones/audio
radio/CAT/control
ADIF/Cabrillo file I/O
```

`app_controller` supplies time/event facts such as slot identity or TX completion explicitly when required.

## 10. Queue/data-structure audit

The current V2 reference uses `AUTOSEQ_MAX_QUEUE = 30` with one array split into active and inactive zones. Preserve that behavior and capacity first.

V2 uses `std::string`, broad `int`s, many booleans, and redundant `next_tx`. V3 may change representation because the user-approved goal of this port is boundary/ownership cleanup plus compact data.

Approved representation rules:

- fixed-size C arrays for callsign/grid/exchange fields;
- narrow signed types for SNR where range allows;
- narrow counters where limits allow;
- explicit flag masks/bitsets for repeated booleans;
- no C implementation-defined bit-field layout;
- no per-context heap allocation;
- no stored `next_tx`; derive it from state;
- preserve `inactive_since_ms` semantics initially unless equivalence proves a safer compact replacement;
- do not remove other fields merely because they look redundant before V2 behavior tests prove it.

Add `_Static_assert` checks for context/queue size after final AS-2 layout.

## 11. Lifetime audit

`RxBatch` belongs to `AppRxState`. Its message storage is embedded in that RX state and is overwritten/reused by later decode windows.

Therefore:

```text
allowed:
    auto_seq_select(&seq, &rx_message, slot_id)
    -> copy callsign/grid/SNR/offset/etc. into QsoContext

forbidden:
    QsoContext.rx_message = &rx_message
```

AutoSeq QSO state must survive RX stream closure and future batch replacement without depending on RX allocation lifetime.

Likewise, QsoView returned to UI should be a snapshot/caller-owned output, not a pointer that lets UI mutate queue state.

## 12. Timing/event ownership audit

Current RX timing is clean:

```text
MiniShell UTC establishes initial slot reference
RxSlotFramer owns sample-count progression
Ft8Engine is clock-free
```

For AutoSeq/TX, preserve V2 behavior while keeping owners explicit:

```text
app_controller owns slot-boundary event ordering
auto_seq owns QSO policy in response to events
future TX module owns waveform/physical TX execution
```

AutoSeq does not poll a clock and does not start TX itself.

This directly supports the V2 golden ordering:

```text
decode complete
-> AutoSeq update
-> pending TxIntent
-> later slot boundary
-> future TX
-> TX completion/tick
```

## 13. Logging ownership audit

V2 AutoSeq directly owns callbacks for ADIF/Cabrillo side effects. That is structural coupling, not behavior we need to preserve literally.

V3 decision:

- AutoSeq preserves the exact state/transition where logging becomes eligible;
- AutoSeq emits a typed `QSO_LOG_READY`-style policy event/snapshot;
- `app_controller` routes that to the logging/storage owner;
- AutoSeq never opens/writes files.

This changes ownership without changing observable sequencing semantics.

Logging implementation itself remains outside the initial AS stages.

## 14. T UIScreen ownership audit

The T screen should be a projection of AutoSeq state, not the queue owner.

```text
auto_seq -> QsoView[]
app_controller -> UiModel.tx_lines[] / tx_count
ui_shell -> paged rendering
```

The first real integrated test should intentionally fill the queue from the eight CQs in `tests/kfs16b12k.wav`:

```text
16 decoded
8 CQ
8 manual selections
8 active QSO contexts
T page 1: entries 1..6
T page 2: entries 7..8
```

This tests RX selection, index mapping, context copying, queue insertion, view projection, and paging while physical TX remains impossible.

## 15. Memory audit

Current ADV 2x2 RX measurement after the deterministic WAV decode:

```text
heap free       111.3 KiB
largest block    53.0 KiB
app allocation  228.5 KiB
```

That makes AutoSeq a good candidate for fixed embedded storage rather than fragmented heap allocation.

AS memory rules:

```text
no AutoSeq heap allocation
measure sizeof(QsoContext)
measure sizeof(AutoSeq)
measure foreground task stack high-water after integration
prefer narrow repeated state and explicit masks
never trade correctness for a few incidental bytes
```

A 30-entry compact queue should be small relative to the FT8 2x2 DSP workspace, but measurement is authoritative.

## 16. Documentation audit

`docs/MiniFT8/development.md` correctly says RX-7 is complete and AutoSeq is separate, but `docs/MiniFT8/architecture.md` still contains an older "current implementation boundary" section describing RX consumer/`ft8_engine` as future work.

This is documentation debt only. The AS plan and this audit are the current AutoSeq design authority. Refresh `architecture.md` when AS-1/AS-2 establishes the final AutoSeq source/type names, avoiding another speculative rewrite before implementation.

## 17. Readiness decision

The codebase is ready to start **AS-1**.

Locked decisions before implementation:

```text
[done] V2 AutoSeq behavior pinned as oracle
[done] port changes boundary/ownership/data representation first
[done] app_controller remains sole coordinator
[done] auto_seq sits after RxResultBuilder
[done] AutoSeq owns one compact 30-entry active/inactive queue
[done] next_tx derived from state
[done] AutoSeq performs no MiniShell/UI/DSP/hash/file/radio calls
[done] RxBatch pointers are not retained
[done] T UIScreen is a read-only projection of QSO queue state
[done] eight real CQs become the first multi-QSO integration fixture
```

Required first-stage work already scoped, not unresolved:

```text
AS-1a  ConfigService station callsign/grid ownership + builder injection
AS-1b  factual RX SNR carried into RxMessage
AS-1c  absolute RX-message selection AppAction
```

After those three boundary completions, AS-2 can port the V2 AutoSeq core without reaching across any ownership boundary.
