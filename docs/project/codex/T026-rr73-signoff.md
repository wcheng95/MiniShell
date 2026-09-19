# T026 — responder RR73 must advance to 73

Status: READY

## Architect evidence

Real 40 m QMX trace, 2026-09-19 UTC:

```text
R [20260919 005012][7.074] CQ KF7SEY CN84 0 1928
...
T [20260919 005045][7.074] KF7SEY AG6AQ CM97 1619
R [20260919 005112][7.074] AG6AQ KF7SEY +02 1 1647
T [20260919 005115][7.074] KF7SEY AG6AQ R+00 2329
R [20260919 005142][7.074] AG6AQ KF7SEY RR73 2 1647
T [20260919 005145][7.074] KF7SEY AG6AQ R+00 1798
R [20260919 005212][7.074] AG6AQ KF7SEY RR73 5 1647
T [20260919 005215][7.074] KF7SEY AG6AQ R+00 1014
R [20260919 005242][7.074] AG6AQ KF7SEY RR73 6 1647
T [20260919 005245][7.074] KF7SEY AG6AQ R+00 624
```

Expected after the first received RR73:

```text
T [20260919 005145][7.074] KF7SEY AG6AQ 73 <offset>
```

The repeated `R+00` is wrong.

## Known-good state-table behavior

Pure AutoSeq already encodes:

```text
ROGER_REPORT + RX TX4/RR73
    -> SIGNOFF
    -> next TX = TX5 / 73
```

and the existing pure unit test covers this transition.

Pinned MiniFT8-V2 behavior is the same.

Therefore do **not** start by changing the pure state table blindly.

## Objective

Find and fix the integration bug that causes a live decoded addressed RR73 to fail
to advance the responder QSO from TX3/R+report to TX5/73.

The fix must preserve the already-working opposite/originator path:

```text
CQ AG6AQ ...
RX report
TX RR73
RX 73
```

## Required investigation order

Before changing production logic, create a failing regression that reproduces the
real responder sequence through the highest practical production boundary:

```text
decoded/typed RX message
 -> RxResultBuilder
 -> addressed_rx_to_event
 -> AppController
 -> AutoSeq
 -> next AutoSeqTxIntent
 -> Ft8TxPlan text
```

Do not accept a regression that injects `AUTO_SEQ_MSG_TX4` directly into
`auto_seq_on_addressed_rx()`; that path already passes and would miss the live bug.

The test must prove the structured metadata associated with:

```text
AG6AQ KF7SEY RR73
```

including:

- protocol type;
- parse status;
- unresolved-hash flag;
- call_to;
- call_de;
- extra/terminal;
- is_to_me;
- qso_kind.

Then prove whether that message reaches `auto_seq_on_addressed_rx()`.

## Exact responder regression

Use station:

```text
callsign=AG6AQ
grid=CM97
```

Model this sequence:

1. receive/select `CQ KF7SEY CN84`;
2. next TX must be `KF7SEY AG6AQ CM97` (TX1);
3. complete that TX semantically;
4. receive addressed `AG6AQ KF7SEY +02`;
5. next TX must be `KF7SEY AG6AQ R+00` (TX3; use the same factual SNR convention as production);
6. complete that TX semantically;
7. receive addressed `AG6AQ KF7SEY RR73`;
8. next TX must be TX5 and encode exactly:

```text
KF7SEY AG6AQ 73
```

It must **not** remain TX3 / `R+00`.

Repeat the RR73 once more before TX5 if useful; state must remain/signoff appropriately per V2 semantics, never regress to TX3.

## Full physical-path regression

Also add an integration variant using the T022 physical executor/mocked QMX CAT path
so the next physical plan after RR73 is:

```text
canonical_text = KF7SEY AG6AQ 73
message_kind   = TX5
```

and the RT T record contains the same 73 text.

Random offset may vary; assert consistency/range, not a fixed base.

## Candidate failure boundaries to inspect

Inspect, do not assume:

1. `Ft8ProtocolMessage` decode for RR73;
2. `RxResultBuilder::qso_kind`;
3. `is_to_me` matching;
4. `has_unresolved_hash` gate;
5. `addressed_rx_to_event()`;
6. active/inactive QSO lookup for KF7SEY;
7. queue sort/cleanup;
8. previous-slot batch application ordering;
9. any stale/snapshotted TX intent;
10. physical TX completion/tick ordering.

The RT log writes canonical text before AutoSeq projection, so a visible
`R ... RR73` line alone does **not** prove AutoSeq accepted the message.

## V2 reference

Repository:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

Relevant behavior:

```text
parse_rcvd_msg(): RR73/RRR -> TX4
ROGER_REPORT + TX4 -> SIGNOFF/TX5
format TX5 -> "<DXCALL> <MYCALL> 73"
```

Preserve this behavior.

## Non-goals

Do not change:

- FT8 protocol definitions;
- report semantics;
- retry policy unless the failing regression proves it is the cause;
- T024 Random/Fixed/RX offset behavior;
- CQ/POTA UI;
- alias work;
- CAT formatting;
- RX DSP thresholds;
- logging format.

Do not add text-based fallback parsing of `canonical_text` merely to hide broken
structured metadata unless the investigation proves the structured boundary cannot
represent valid RR73. Prefer fixing the earliest incorrect structured fact.

## Tests

Required:

- new responder RR73 regression through RxResultBuilder/AppController;
- physical mocked-QMX regression producing TX5/73;
- existing pure AutoSeq addressed progression remains unchanged;
- existing first-QSO/originator path remains green;
- RX result-builder RR73 classification coverage;
- standard and, if relevant, nonstandard/resolved-hash RR73 cases;
- Linux full CTest;
- portable units;
- architecture checks;
- sanitizers for changed pure/private code where practical;
- real ADV build;
- git diff --check.

## Acceptance criteria

- [ ] exact KF7SEY responder sequence reproduced as a failing test before fix;
- [ ] root cause identified in handoff notes;
- [ ] first RR73 advances next TX to TX5/73;
- [ ] no subsequent R+report retry after accepted RR73;
- [ ] physical plan text exactly `KF7SEY AG6AQ 73`;
- [ ] RT T line also contains `KF7SEY AG6AQ 73`;
- [ ] pure state table does not receive unnecessary redesign;
- [ ] originator CQ/QSO path remains green;
- [ ] no unrelated changes;
- [ ] Linux/units/architecture/sanitizers/ADV all pass.

## Manual validation

After review, architect repeats an on-air responder QSO.

PASS evidence should look like:

```text
R ... CQ <DX> <GRID>
T ... <DX> AG6AQ CM97
R ... AG6AQ <DX> +nn
T ... <DX> AG6AQ R+nn
R ... AG6AQ <DX> RR73
T ... <DX> AG6AQ 73
```

A completed contact is preferred but the decisive T026 check is the final
RR73 -> 73 transition.

## Branch workflow

Use:

```text
codex/T026-rr73-signoff
```

Codex:

1. reproduce before fixing;
2. identify root cause;
3. implement the smallest structural fix;
4. run all gates;
5. set Status to REVIEW;
6. record exact failing-before/fixed-after evidence;
7. commit/push one reviewable implementation commit;
8. return SHA;
9. no PR;
10. no RF testing by Codex.

## Codex implementation notes

### Failing regression before fix

### Root cause

### Implementation summary

### Files changed

### Behavior/invariants preserved

### Tests run

### Hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Review exact diff from task head to implementation.

## Architect test result
