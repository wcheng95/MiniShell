# MiniFT8-V3 AS-7 — Slot and simulated TX lifecycle

Status: **COMPLETE**

AS-7 introduces the application TX lifecycle boundary without adding Audio TX, CAT/Control, waveform generation, or RF transmission.

The behavioral oracle remains MiniFT8-V2 at:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

## 1. Ownership

```text
AutoSeq
  queue/state policy
  no clock
      |
      v
AutoSeqTxIntent
  semantic caller-owned snapshot
      |
      v
app_controller / TxLifecycle
  UTC slot observation
  parity gate
  beacon runtime policy
  simulated TX start/completion
      |
      +--> capture AutoSeqLogEvent at TX start
      `--> auto_seq_tick() after completion
```

Hard rules:

```text
AutoSeq does not read MiniShell UTC
AutoSeq does not key Audio/CAT/RF
TxLifecycle does not inspect QSO state
ft8_main does not own scheduling policy
one eligible slot boundary can cause at most one completion/tick
```

## 2. Semantic TX intent

`auto_seq_prepare_tx_intent()` projects the current queue head into a fixed-size `AutoSeqTxIntent`.

It carries only semantic facts needed by a future encoder/transmitter:

```text
QSO / CQ / FreeText intent type
TX1..TX6 semantic message kind where applicable
CQ type
station callsign/grid
DX callsign/grid
report value
Field Day exchange/flag
audio offset Hz
TX parity
retry counter/limit
```

It does not contain an encoded FT8 waveform, CAT command, device handle, MiniShell API pointer, or heap-owned string.

CQ FreeText and ad-hoc FreeText use the fixed singleton text already owned by AutoSeq. A Field Day reply intent carries the configured local exchange and preserves the AS-6 rule that selecting CQ FD starts with TX2 semantics rather than grid/TX1.

## 3. Slot-boundary lifecycle

`TxLifecycle` is a small controller-side time gate. Production derives FT8 slot position from MiniShell UTC:

```text
slot_id      = floor(unix_seconds / 15)
slot parity  = slot_id & 1
ms_into_slot = position inside the 15 s slot
```

The first observation only anchors the lifecycle. It must never cause a TX simply because MiniFT8 started in the middle of a slot.

A scheduling boundary is emitted only when:

```text
new slot == previous slot + 1
and
new slot is observed within the first 1000 ms
```

This prevents catch-up transmissions after suspend, missed slots, or forward/backward UTC corrections. Those events re-anchor the lifecycle instead.

The one-second value is an AS-7 simulation/polling guard, not a promise for future physical-RF start timing. A real transmitter backend may require a tighter pre-armed boundary while preserving the same no-catch-up invariant.

## 4. Parity and simulated completion

At an eligible boundary, `app_controller`:

```text
1. snapshots the current AutoSeq TX intent
2. optionally creates a beacon CQ only if no intent exists
3. checks intent TX parity against the current slot
4. records the TX-start snapshot
5. captures any TX4/TX5 logging eligibility
6. completes the simulated transmission synchronously
7. calls auto_seq_tick() exactly once
```

Wrong-parity boundaries do not consume a retry. Re-observing the same slot cannot transmit twice.

The simulated completion is intentionally synchronous in AS-7. A future real TX backend will replace that one step with an asynchronous start/completion path while keeping the controller boundary and post-completion tick rule.

## 5. Beacon runtime policy

Beacon state belongs to the controller lifecycle and is not persisted:

```text
OFF
EVEN
ODD
```

Behavior preserves the V2 model:

```text
no pending intent + matching beacon parity
    -> enqueue one CQ
    -> transmit simulated CQ
    -> tick removes one-shot CQ

next matching cycle
    -> enqueue a fresh CQ
```

Any active QSO or pending FreeText intent preempts beacon CQ generation. Changing beacon mode removes a stale queued CQ so a parity change cannot leave an obsolete one-shot request behind.

AS-7 exposes the controller beacon API and tests it. The O-screen editing UI remains a UI task; AS-7 does not expand the still-open O-screen design merely to exercise the lifecycle.

## 6. Logging boundary

AS-6 established typed ADIF/Cabrillo eligibility. AS-7 now places that event at the real lifecycle point corresponding to V2 `on_tx_starting()`:

```text
TX4/TX5 intent is about to start
    -> auto_seq_prepare_log_event()
    -> controller captures AutoSeqLogEvent
    -> simulated TX completes
    -> auto_seq_tick()
```

AS-7 does **not** acknowledge the event automatically and does not pretend a file write succeeded.

Actual ADIF/Cabrillo serialization is intentionally left to a dedicated logging owner because it also needs file naming, frequency/band facts, UTC formatting, Cabrillo header/end-marker handling, and write-failure policy. Pulling those responsibilities into the TX lifecycle would recreate V2 coupling.

Until that owner exists, the AutoSeq logging flag remains eligible, which is the correct failure/retry behavior.

## 7. Deterministic fixture isolation

`--rx-slot` supplies a synthetic RX slot ID for deterministic WAV decode tests. AS-7 deliberately disables wall-clock TX stepping in that mode:

```text
ft8 --rx ... --rx-slot N
    -> synthetic RX timing only
    -> no wall-clock simulated TX
```

This prevents a CI run that happens to cross a real 15-second UTC boundary from consuming or reordering the deterministic AS-3/AS-5 queue fixture.

Live operation, including `--rx` without `--rx-slot`, uses the normal MiniShell UTC TX lifecycle.

## 8. Test coverage

`ft8_tx_lifecycle_as7_test` covers:

```text
first observation anchors without TX
adjacent slot boundary detection
duplicate-slot suppression
wrong-parity suppression
missed-slot no-catch-up
backward UTC correction re-anchor
late boundary suppression
QSO TX1 intent projection
Field Day TX2 intent projection
CQ/SOTA intent projection
CQ FreeText intent projection
ad-hoc FreeText intent projection
retry tick only after simulated completion
retry exhaustion through controller lifecycle
TX4 logging eligibility captured before tick
beacon one-shot re-enqueue
QSO preemption of beacon
beacon mode change removes stale CQ
```

The existing Linux integration suite also remains green with deterministic `--rx-slot` isolation. The branch is required to pass the Linux, FT8 Reference, and Cardputer ADV CI gates on the exact final head before merge.

## 9. Deliberately deferred

AS-7 does not implement:

```text
FT8 TX message encoding / waveform generation
MiniShell Audio TX
CAT / Control
radio-specific TX sequencing
physical RF transmission
persistent ADIF writer
persistent Cabrillo writer
O-screen beacon/CQ editing UI
```

Those are downstream consumers of the lifecycle boundary, not reasons to move time/platform ownership back into AutoSeq.

## 10. Completion boundary

AS-7 is complete when the following are true:

```text
semantic TxIntent projection is typed and platform-independent
UTC slot/parity execution is controller-owned
first/missed/duplicate/wrong-parity slot edges cannot consume TX state
simulated completion is the sole production source of auto_seq_tick()
beacon CQ is regenerated only when idle and on configured parity
logging eligibility is captured at TX start without fake acknowledgement
--rx-slot deterministic tests cannot be mutated by wall-clock TX
Linux + FT8 Reference + ADV CI pass on the exact final branch head
```

Actual transmitter and persistent logging implementations remain downstream work; they do not block AS-7 closure.
