# MiniFT8-V3 AS-1 — AutoSeq Input Boundaries

## Purpose

AS-1 completes the factual application boundaries needed before the MiniFT8-V2 AutoSeq behavior is ported into the new `auto_seq` module.

AS-1 deliberately does **not** implement a QSO state machine, queue, TX intent, retry policy, or transmitter behavior.

The behavioral oracle for the later port remains:

```text
repository  wcheng95/Mini-FT8
commit      491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

The AS-1 rule is:

> AutoSeq will receive complete factual `RxMessage` inputs through `app_controller`; it must never reach backward into configuration files, decoder waterfall/bin geometry, hash state, UI state, or MiniShell services.

## 1. Station identity boundary

`ConfigService` now owns the FT8 station identity fields needed by RX classification and later AutoSeq:

```text
callsign
 grid
```

They are persisted in `/flash/ft8/station.txt` as:

```text
callsign=AG6AQ
grid=CM97
```

Values are normalized to uppercase when parsed. Existing station files without these keys remain valid; the fields default to empty rather than embedding a station identity in the application binary.

Ownership is:

```text
station.txt
    |
    v
storage_service
    |
    v
ConfigService                  owns parsed station identity
    |
    v
app_controller                 coordinates injection
    |
    +--> RxResultBuilder       receives local callsign for factual is_to_me classification
    `--> future auto_seq       receives station identity through an explicit AutoSeq config boundary
```

`RxResultBuilder` does not read `station.txt` and AutoSeq will not read it either.

## 2. Factual RX SNR boundary

MiniFT8-V2 AutoSeq latches the received signal report metadata when a decoded message is accepted into a QSO context. V3 therefore needs an SNR fact before AutoSeq begins.

SNR belongs to the RX/engine side because `Ft8Engine` owns the waterfall required to calculate it.

```text
Ft8Engine waterfall
    |
    | V2-compatible SNR calculation
    v
Ft8ProtocolMessage.snr_db
    |
    v
RxResultBuilder
    |
    v
RxMessage.snr_db
    |
    v
future auto_seq
```

AutoSeq must never inspect waterfall magnitudes or estimate SNR itself.

### V2-compatible definition

AS-1 preserves the pinned V2 algorithm rather than redesigning SNR reporting during the structural port:

1. Build a 256-bin histogram over the current waterfall magnitudes.
2. Use the 25th-percentile waterfall magnitude as the noise floor.
3. Locate the candidate magnitude using its time/frequency offset and OSR subindices.
4. Convert stored magnitude using the existing half-dB representation.
5. Compute candidate dB minus noise-floor dB.
6. Round to integer dB and clamp to `-30..99`.

This is factual decoder metadata. It is not a scheduling decision.

The histogram is temporary stack storage inside `Ft8Engine`; AS-1 introduces no persistent SNR heap allocation.

## 3. Factual RX audio-offset boundary

The V2 AutoSeq context also records the received station's audio offset so a reply can use the appropriate TX frequency policy later.

Before AS-1, `RxMessage` exposed decoder-native coordinates such as frequency bin and `freq_sub`. Requiring AutoSeq to convert those values would couple QSO policy to monitor geometry.

AS-1 therefore converts the decoder candidate coordinate while `Ft8Engine` still owns the monitor configuration:

```text
candidate.freq_offset / freq_sub
monitor min_bin / freq_osr / symbol period
    |
    v
Ft8Engine
    |
    v
Ft8ProtocolMessage.offset_hz
    |
    v
RxResultBuilder
    |
    v
RxMessage.offset_hz
    |
    v
future auto_seq
```

For the pinned 1500 Hz golden CQ fixture, the production RX assembly verifies:

```text
offset_hz = 1500
```

AutoSeq therefore receives ordinary Hz and never needs FFT/bin knowledge.

## 4. RX selection boundary

The RX UIScreen shows six decoded messages per page. Its visible line number is presentation state, not application identity.

AS-1 introduces:

```text
APP_ACTION_SELECT_RX_MESSAGE
```

`ui_shell` translates a visible selection into an **absolute decoded-message index**:

```text
absolute_index = visible_page * 6 + selected_line
```

Examples:

```text
page 1 line 6 -> index 5
page 2 line 1 -> index 6
page 2 line 2 -> index 7
```

The action contains only the index. `ui_shell` never sees an `RxMessage *` and never calls AutoSeq.

`app_controller` validates the index against the currently retained `RxBatch`.

## 5. RX message lifetime rule

`RxBatch.messages` points into RX-owned storage which is reused by future decode windows. No QSO-lifetime owner may retain a pointer into it.

AS-1 makes the selection lifetime explicit:

```text
AppRxState
    selected_rx_valid
    selected_rx_index
    selected_rx_generation
```

A new completed RX batch invalidates the previous selection.

The later AS-3 boundary will therefore be:

```text
UI selection
    -> absolute index
    -> app_controller validates current batch/generation
    -> app_controller resolves const RxMessage for this call only
    -> auto_seq COPIES QSO-lifetime facts into QsoContext
```

AutoSeq must not retain the `RxMessage *`.

## 6. Hash ownership remains unchanged

AS-1 does not change hash ownership:

```text
Ft8Engine
    owns Ft8HashStore
```

Resolved protocol facts flow forward in `Ft8ProtocolMessage` / `RxMessage`. AutoSeq does not own, query, or duplicate the decoder hash table.

## 7. Resulting AutoSeq input boundary

After AS-1, the information needed to create a normal V2-equivalent QSO context can arrive without crossing subsystem boundaries:

```text
RxBatch.slot_id
RxMessage.call_to
RxMessage.call_de
RxMessage.extra
RxMessage.is_cq
RxMessage.is_to_me
RxMessage.snr_db
RxMessage.offset_hz
RxMessage.protocol_type
RxMessage canonical/protocol facts

ConfigService.callsign
ConfigService.grid
ConfigService.skip_tx1
ConfigService.max_retry
```

The later `auto_seq` module will receive these through explicit calls from `app_controller`.

## 8. Tests

AS-1 adds or extends tests at the owning boundary:

```text
rx_result_builder_rx5_test
    verifies SNR/offset projection plus existing CQ/to-me classification

rx5_pure_assembly_reference
    verifies the real pinned CQ remains decoded and offset_hz == 1500
    verifies SNR remains in the V2-defined -30..99 range

ft8_ui_smoke
    verifies page-relative 1..6 keys become absolute RX indices
    including page 2 line 2 -> index 7

linux_ft8
    verifies callsign/grid parsing, uppercase normalization and persistence

linux_ft8_rx7
    selects the real decoded CQ through ui_shell -> AppAction -> app_controller
```

Linux and FT8 Reference CI pass on the AS-1 implementation head. ADV cross-build is the final embedded compilation gate.

## 9. Non-goals

AS-1 intentionally does not add:

```text
QsoContext
auto_seq queue
QSO state transitions
next-TX derivation
TxIntent
retry / inactive / reactivation behavior
T UIScreen queue projection
automatic is_to_me progression
TX encoder / Audio / Control
ADIF or Cabrillo I/O
```

Those begin with AS-2 and later stages.

## 10. AS-1 exit condition

AS-1 is complete when:

```text
station identity has one application owner
RX classification receives the configured local callsign
SNR is a factual RxMessage field
received audio offset is a factual RxMessage field
RX selection is an absolute application action
selection never creates cross-window RxMessage ownership
Linux/reference tests pass
ADV firmware cross-build passes
```

After that, AS-2 can introduce the pure compact `auto_seq` data structure and state-machine port without reaching across any RX, UI, storage, hash, or platform boundary.
