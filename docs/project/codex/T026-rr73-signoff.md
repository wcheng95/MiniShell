# T026 — responder RR73 must advance to 73

Status: REVIEW

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

- [x] exact KF7SEY responder sequence reproduced as a failing test before fix;
- [x] root cause identified in handoff notes;
- [x] first RR73 advances next TX to TX5/73;
- [x] no subsequent R+report retry after accepted RR73;
- [x] physical plan text exactly `KF7SEY AG6AQ 73`;
- [x] RT T line also contains `KF7SEY AG6AQ 73`;
- [x] pure state table does not receive unnecessary redesign;
- [x] originator CQ/QSO path remains green;
- [x] no unrelated changes;
- [x] Linux/units/architecture/sanitizers/ADV all pass.

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

Before changing production code, extended `ft8_physical_tx_test.c` with the exact
KF7SEY responder exchange. Each input is encoded to payload bits, decoded by the
production protocol codec, assigned engine-style SNR/offset measurements, passed
through the real RxResultBuilder and controller batch/action paths, and checked
through the next AutoSeq intent and T021 plan. No TX4 event is injected directly
into AutoSeq.

The ordinary TOKEN-coded RR73 variant already passed. The GRID-coded RR73 variant
reproduced the reported canonical-text exchange and repeated R+00. Command:

```bash
cmake -S . -B build-linux
cmake --build build-linux --target ft8_physical_tx_unit -j8
build-linux/ft8_physical_tx_unit
```

Pre-fix exit 134 (assertion); captured in `/tmp/T026-before-test.log`:

```text
next TX: KF7SEY AG6AQ CM97 (kind 1)
next TX: KF7SEY AG6AQ R+00 (kind 3)
RR73 boundary: protocol=5 parse=0 unresolved=0 to=AG6AQ de=KF7SEY extra=RR73 extra_kind=4 to_me=1 qso_kind=1 projected=1 event_kind=1
after RR73: state=3 last_rx=1 next=KF7SEY AG6AQ R+00
Assertion app.auto_seq.queue[0].last_rx_kind==AUTO_SEQ_MSG_TX4 failed
```

Only the regression test was modified at this point. Enum meanings: protocol 5 is
STANDARD, parse 0 is OK, extra_kind 4 is GRID, qso/event kind 1 is TX1, and state 3
is ROGER_REPORT.

### Root cause

`RR73` also satisfies four-character locator syntax. A standard payload may decode
with `extra="RR73"` and `extra_kind=GRID`, yielding the same canonical text as a
terminal-token payload. RxResultBuilder previously classified that structured
field as TX1. The message passed is_to_me/hash/parse gates and reached AutoSeq, but
as the wrong event: ROGER_REPORT ignores TX1 and keeps retrying TX3/R+report.

The earliest incorrect QSO fact is therefore RxResultBuilder.qso_kind. Protocol
decode correctly retains the payload's field type; addressed projection faithfully
maps the incorrect TX1 fact. Active QSO lookup, sorting, batch application and
physical completion do not cause this reproduced failure. The TOKEN-coded RR73
control case passes the identical sequence before and after the fix.

Inspected pinned V2 `main/autoseq.cpp` at
`491e757ae6b1e4cfd2b9a6ba10f48b35643849e0`: `parse_rcvd_msg()` checks RR73/RRR
keywords before grid syntax, and ROGER_REPORT+TX4 selects SIGNOFF/TX5. The narrow
fix restores that keyword precedence for the ambiguous GRID-coded RR73 case.

The supplied RT trace contains canonical text and measurements, not raw payload
bits or extra_kind. Consequently, the exact wire representation from the real
KF7SEY reception cannot be established from that trace alone. The regression
proves a concrete defect producing the same visible exchange; the architect's
on-air repeat remains the confirmation for the reported occurrence.

### Implementation summary

In the standard GRID classification branch, recognize exact structured extra
`RR73` as RX_QSO_MSG_TX4 before applying ordinary grid recognition. Preserve the
protocol type, parse status, calls, extra, measurements and unresolved-hash flag.
No canonical-text fallback or protocol/state-machine redesign.

Post-fix regression evidence:

```text
RR73 boundary: protocol=5 parse=0 unresolved=0 to=AG6AQ de=KF7SEY extra=RR73 extra_kind=4 to_me=1 qso_kind=4 projected=1 event_kind=4
after RR73: state=5 last_rx=4 next=KF7SEY AG6AQ 73
next TX: KF7SEY AG6AQ 73 (kind 5)
```

Both semantic and physical variants run with TOKEN and GRID RR73. Repeated RR73
before TX5 retains signoff. Physical tests use the T022 executor, real encoder/
radio/logging with mocked MiniShell services, T024 Random offsets in range, and
verify the RT T line matches `KF7SEY AG6AQ 73` plus the plan base.

### Files changed

- `apps/ft8/src/rx_result_builder/rx_result_builder.c`: the only production logic
  change, exact RR73 precedence in the existing GRID classification branch.
- `tests/ft8_physical_tx_test.c`: decoded-payload responder regression, structured
  metadata/projection checks, physical TX5/RT assertions, complete originator
  path and resolved/unresolved nonstandard RR73 coverage.
- `tests/rx_result_builder_rx5_test.c`: GRID RR73/TX4, neighboring RR74/TX1,
  R-prefixed grid exclusion, and RRR/TX4 checks; existing assertions retained.
- `CMakeLists.txt`: register the existing RX-5 builder test source as a Linux
  CTest target so its expanded classification coverage is part of the local gate.
- This task packet: failing-before/passing-after evidence and REVIEW handoff.

### Behavior/invariants preserved

Pure AutoSeq state/retry/queue policy is untouched. The originator test proves
CQ -> received grid -> sent report -> received R+report -> sent RR73 -> received
73 completes normally. Existing pure addressed progression and T022/T023/T024
regressions remain enabled. Nonstandard RR73 still uses its structured terminal;
resolved destination hashes project TX4, while unresolved hashes remain gated.
Nonstandard TX support is unchanged.

No changes to FT8 protocol definitions/codec, DSP, report/SNR convention, offset
policy, CQ UI, aliases, CAT formatting, scheduling, RX lifecycle or log formats.
The received CQ SNR remains the factual 0 used to form R+00 before signoff.

### Tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: 58/58, including new builder target and all existing regressions/checker self-tests

cmake -S tests/unit -B /tmp/T026-build-unit
cmake --build /tmp/T026-build-unit -j8
ctest --test-dir /tmp/T026-build-unit --output-on-failure
# PASS: 15/15

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/serial_protocol_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
# All exit 0

cmake -S . -B /tmp/T026-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/T026-build-sanitize --target ft8_physical_tx_unit ft8_rx_result_builder_unit -j8
ctest --test-dir /tmp/T026-build-sanitize --output-on-failure -R 'ft8_physical_tx|ft8_rx_result_builder'
# PASS: 2/2 with ASan/UBSan/leak detection outside the known sandbox ptrace restriction

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# Real ESP32-S3 firmware PASS; minishell_adv.bin 0xbc400 bytes, 88% partition free

git diff --check
# PASS
```

### Hardware validation still required

After supervisor review, repeat the responder QSO and retain R/RR73 followed by
T/73 in the RT trace. A complete contact is preferred. No RF or hardware testing
was performed by Codex.

### Known limitations / risks

Raw payload evidence is unavailable for the supplied on-air trace, as detailed
above. GRID-coded literal RR73 now has the terminal precedence used by V2; ordinary
locators such as RR74 and R-prefixed grids retain their existing classifications.
No task-scope deviations.

### Commit

One implementation commit on `codex/T026-rr73-signoff`, titled
`T026: classify grid-coded RR73 as responder signoff`. This packet is included;
the full pushed SHA is returned in the handoff. No PR or Actions wait.

## Supervisor review

Review exact diff from task head to implementation.

## Architect test result
