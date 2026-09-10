# MiniFT8-V3 AS-4: Automatic Addressed-to-Me Progression

Status: **COMPLETE**

AS-4 connects completed factual RX batches to the existing pure AutoSeq state owner. It preserves the MiniFT8-V2 rule that ordinary CQs are manual-only while decoded ordinary QSO messages addressed to the configured local station are processed automatically.

## Ownership and event flow

```text
Ft8Engine
    -> Ft8ProtocolSlot
    -> RxResultBuilder
         classifies factual ordinary QSO stage
         TX1 / TX2 / TX3 / TX4 / TX5
         report_db when applicable
    -> RxBatch
    -> app_controller
         detects completed batch once
         walks messages in decode order
         maps eligible is_to_me message to AutoSeqRxEvent
    -> auto_seq_on_addressed_rx()
         active lookup / progression
         inactive lookup / reactivation
         unknown-mid-QSO guards
         priority sorting
    -> QsoView
    -> T UIScreen
```

The RX callback remains an RX-assembly boundary. It does not call AutoSeq. `app_controller_step_rx()` performs the AutoSeq feed after a new `RxBatch` generation is complete.

`auto_seq.c` is unchanged by AS-4. State progression, active/inactive matching, reactivation, and reincarnation guards remain owned by the AS-2 pure core.

## Factual RX stage

`RxMessage` now carries two compact factual fields:

```text
RxQsoMessageKind qso_kind
int8_t            report_db
```

`qso_kind` is RX classification, not a TX decision. For ordinary standard FT8 messages:

```text
4-character grid  -> TX1
report             -> TX2
R+report           -> TX3
RRR / RR73         -> TX4
73                 -> TX5
```

The classification uses typed protocol fields (`extra_kind` and terminal values), not `canonical_text` parsing in `app_controller`.

The V2 report range remains `-30..+30`. `report_db` is `-99` when no report is present.

A V2 edge is deliberately preserved: an R-prefixed grid such as `R FN42` is not classified as an ordinary TX1 grid.

## Automatic eligibility

AS-4 automatically feeds a decoded message only when all of these are true:

```text
is_to_me == true
parse_status == FT8_PROTOCOL_PARSE_OK
has_unresolved_hash == false
call_de is non-empty
qso_kind is TX1..TX5
```

The controller copies the factual fields into an `AutoSeqRxEvent` and sets `AUTO_SEQ_RX_FLAG_TO_ME`.

Ordinary CQ behavior remains unchanged:

```text
ordinary CQ       -> no automatic AutoSeq action
selected CQ       -> AS-3 manual insertion
addressed message -> AS-4 automatic processing
```

ARRL Field Day and DXpedition messages are intentionally not flattened into the ordinary TX1..TX5 mapping here. Their special semantics remain later work.

## V2 queue behavior preserved

The existing AS-2 core supplies the pinned V2 behavior:

- matching active DX context is updated rather than duplicated;
- matching inactive context is reactivated and updated;
- current RX slot re-anchors the opposite TX parity;
- unknown TX1/grid and TX2/report may start a fresh context;
- unknown TX3/R+report is ignored;
- unknown TX4/RRR/RR73 and TX5/73 are ignored;
- when all 30 entries are active and no inactive entry can be evicted, the new decode is dropped non-fatally.

No physical TX occurs in AS-4.

## Production golden proof

The FT8 Reference workflow reuses pinned MiniFT8-V2 WAV files and converts them through the same 12 kHz stereo MiniShell Audio path used by production MiniFT8.

Configured local station:

```text
callsign  W1ABC
grid      FN42
Skip TX1  OFF
Max Retry 3
```

### Fresh TX1/grid

Input:

```text
W1ABC K9XYZ FN42
```

No RX line is selected. The completed batch automatically creates one K9XYZ context:

```text
K9XYZ    RPRT 0/3
```

This is V2 behavior: receiving TX1 as a fresh addressed contact advances the new context to `REPORT`, whose derived next transmission is TX2.

### Fresh TX2/report

Input:

```text
W1ABC K9XYZ -12
```

Again with no manual selection, the fresh context becomes:

```text
K9XYZ    RRPT 0/3
```

The received report is factual TX2, so V2 progression reaches `ROGER_REPORT`, whose derived next transmission is TX3.

### Existing-context two-slot progression

One WAV contains two consecutive complete FT8 slots for the same DX:

```text
slot N    W1ABC K9XYZ FN42
slot N+1  W1ABC K9XYZ -12
```

The controller processes both completed batches in order. The T screen after slot N+1 contains exactly one K9XYZ entry:

```text
K9XYZ    RRPT 0/3
```

This proves that the second addressed decode advances the existing context rather than appending a duplicate.

### Unknown late RR73 guard

Input:

```text
W1ABC K9XYZ RR73
```

With no active or inactive K9XYZ context, AutoSeq ignores the message and the T screen remains empty. This preserves the V2 reincarnation guard.

## Tests

`rx_result_builder_rx5_test` pins factual TX1..TX5 classification and report extraction.

`linux_ft8_rx7.py` remains the production MiniShell/FT8 integration harness and now additionally proves:

```text
fresh addressed TX1 -> automatic RPRT
fresh addressed TX2 -> automatic RRPT
two slots, same DX   -> one existing context advances
unknown RR73         -> no context created
```

The earlier RX-7 / AS-3 manual-CQ case remains in the same harness as a regression check.

## Non-goals

AS-4 does not add:

- TX slot execution or `TxIntent` realization;
- retry ticks driven by actual TX completion;
- T-screen queue-control actions;
- CQ/beacon generation;
- FreeText behavior;
- Field Day special sequencing;
- DXpedition sequencing;
- ADIF/Cabrillo side effects;
- Audio TX, CAT/control, or physical transmission.

AS-5 is next and exercises the already-ported V2 retry, priority, inactive/reactivation, and queue-control behavior through the V3 application boundaries without redesigning those policies.
