# MiniFT8-V3 AS-6 — CQ, FreeText, Field Day, and logging eligibility

Status: **IMPLEMENTED — awaiting final branch CI before merge**

AS-6 ports the remaining special AutoSeq semantics needed before a TX lifecycle exists. It intentionally does **not** add physical TX, Audio TX, CAT/Control, slot execution, or production `auto_seq_tick()` wiring.

The behavioral oracle remains MiniFT8-V2 at:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

V2 behavior is preserved except for the already-decided V3 Field Day rule described below.

## 1. Ownership

```text
Ft8ProtocolMessage
        |
        v
RxResultBuilder
  factual CQ / FD facts
        |
        v
app_controller
  event conversion / config injection
        |
        v
AutoSeq
  pure bounded policy state
        |
        +--> queue / QSO state
        +--> CQ / FreeText semantics
        +--> Field Day semantics
        `--> typed log eligibility
```

Hard rules remain:

```text
AutoSeq owns no heap
AutoSeq calls no MiniShell API
AutoSeq performs no filesystem/log I/O
AutoSeq performs no radio/audio TX
RxResultBuilder performs factual classification only
app_controller remains the production coordinator
```

## 2. CQ one-shot

CQ uses a short-lived `CALLING` queue entry, matching V2's queue model.

Properties:

```text
one CQ entry maximum in the active queue
CQ is lower priority than QSO work
one completed-TX tick removes the CQ entry
periodic beacon re-enqueue is not AS-6
```

CQ type is fixed-size configuration:

```text
CQ
SOTA
POTA
QRP
FD
FREETEXT
```

`cq_freetext` is a singleton AutoSeq configuration sidecar; it is not copied into every `QsoContext`.

## 3. Ad-hoc FreeText one-shot

Ad-hoc FreeText is also a one-shot `CALLING` entry but carries the FreeText flag used by the existing V2 priority relation.

Properties:

```text
only one pending FreeText entry
FreeText preempts active QSO work
if a queue exists, FreeText inherits queue-head TX parity
if the queue is empty, caller supplies fallback parity
one completed-TX tick removes the FreeText entry
```

The pending text is one fixed-size singleton sidecar in `AutoSeq`; there is no per-QSO string or heap allocation.

## 4. Field Day RX facts

`RxResultBuilder` now makes Field Day factual before policy sees the message.

CQ recognition:

```text
standard CQ FD form      -> is_cq=true, is_fd=true
FreeText CQ FD modifier  -> is_cq=true, is_fd=true
```

Structured ARRL Field Day exchange packets expose:

```text
is_fd=true
fd_exchange="<count><class> <section>"   e.g. "1B SCV"
without R prefix -> qso_kind=TX2
with R prefix    -> qso_kind=TX3
```

The exchange string is normalized factual data. AutoSeq does not parse rendered UI text.

## 5. Locked V3 Field Day exception

MiniFT8-V3 intentionally differs from older V2 fixtures when replying to **CQ FD**:

```text
ordinary CQ:
CQ -> TX1/grid -> ...

CQ FD:
CQ FD -> TX2/our FD exchange -> ...
```

Field Day does not use an RST/grid step. Therefore a manually selected CQ FD starts AutoSeq in `REPORT`, regardless of the generic `skip_tx1` setting.

The normal AutoSeq state machine is reused rather than creating a second Field Day scheduler:

```text
select CQ FD
    -> REPORT          derived TX2 = local FD exchange

receive R <exchange>
    -> ROGERS          derived TX4 = RR73

receive RR73
    -> SIGNOFF         derived TX5 = 73
```

For an addressed Field Day exchange after our CQ, an unprefixed exchange maps to TX2 semantics and `R <exchange>` maps to TX3 semantics.

## 6. Station configuration boundary

The V2 station-file behavior was used to separate persistent semantics from runtime beacon state.

Persisted in V3 `station.txt`:

```text
cq_type
cq_ft
free_text
fd_exchange
```

`app_controller_init()` synchronizes the persisted CQ type/text and FD exchange into the pure AutoSeq owner together with callsign/grid, Skip-TX1, and retry configuration.

Beacon mode is deliberately **not** persisted as an active runtime state. As in V2, a new application session starts with beacon OFF. OFF/EVEN/ODD scheduling belongs to AS-7.

## 7. Logging eligibility boundary

V2 logs at the start of a TX4/RR73 or TX5/73 emission. AS-6 preserves that timing semantically without doing I/O inside AutoSeq.

AutoSeq exposes:

```text
auto_seq_prepare_log_event()
auto_seq_ack_log_event()
```

`AutoSeqLogEvent` carries factual logging input and two independent eligibility flags:

```text
ADIF eligible
Field Day Cabrillo eligible
```

The caller performs the actual write later and acknowledges only successful writes.

Consequences:

```text
failed ADIF write      -> ADIF remains eligible
failed Cabrillo write  -> Cabrillo remains eligible
successful ADIF write  -> duplicate ADIF suppressed
successful Cabrillo    -> duplicate Cabrillo suppressed
```

ADIF and Cabrillo acknowledgement are independent.

## 8. Memory model

AS-6 keeps the bounded embedded design:

```text
QsoContext <= 64 bytes
AutoSeq    <= 2048 bytes
AUTO_SEQ_MAX_QUEUE = 30
```

No AS-6 feature adds dynamic allocation. CQ configuration, local FD exchange, and pending FreeText are singleton fixed-size storage in the AutoSeq owner, not per-QSO heap/string state.

## 9. Test coverage

The cumulative AutoSeq unit test covers:

```text
CQ one-shot lifecycle
CQ lower priority than QSO work
FreeText one-shot lifecycle
FreeText preemption
FreeText parity inheritance/fallback
CQ FD forced Skip-TX1 behavior
Field Day exchange retention
TX4/TX5 logging eligibility
failed-write retry eligibility
independent ADIF/Cabrillo acknowledgement
duplicate suppression after successful acknowledgement
```

`rx_result_builder_rx5_test` additionally covers:

```text
standard CQ FD factual classification
FreeText CQ FD factual classification
structured ARRL-FD TX2 exchange facts
structured ARRL-FD TX3/R exchange facts
```

`linux_ft8.py` round-trips the new semantic `station.txt` fields through the production ConfigService save path.

All previous RX, AutoSeq, UI, Linux, and ADV regressions remain in the same CI suite.

## 10. Deliberately deferred to AS-7

AS-6 does not implement:

```text
beacon OFF/EVEN/ODD runtime scheduler
periodic CQ re-enqueue
TxIntent construction/realization
slot-boundary execution policy
TX-start callback orchestration
actual ADIF/Cabrillo writes
simulated TX completion
a production caller of auto_seq_tick()
Audio TX
CAT/Control
physical RF transmission
```

AS-7 should consume the semantics established here rather than moving scheduling or platform work back into AutoSeq.
