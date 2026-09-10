# MiniFT8-V3 AS-5: Queue Lifecycle Controls

Status: **COMPLETE**

AS-5 exposes the existing MiniFT8-V2 AutoSeq queue lifecycle through the V3 application boundary without changing AutoSeq policy. The pure `auto_seq` owner remains authoritative for retry state, priority, active/inactive storage, late-message reactivation, eviction, and same-parity rotation.

## Scope

AS-5 adds application/UI access to the two V2 queue controls that are meaningful before TX execution exists:

```text
T visible line 1..6 -> drop that absolute active QSO index
T Enter             -> rotate the front same-parity run
T Up/Down           -> page navigation unchanged
```

The V3 T screen retains the AS-3 six-QSO-per-page layout. This intentionally adapts V2's old five-row control layout while preserving the underlying operations.

No physical TX, TX audio, CAT/control, CQ/FreeText/Field Day special behavior, or logging side effect is added here.

## Ownership

```text
ui_shell
    maps T-screen navigation/control to AppAction
        |
        v
app_controller
    validates/applies command
    supplies monotonic timestamp for parking
        |
        v
auto_seq
    owns queue mutation and QSO metadata
```

Hard rules preserved:

- UI never receives `QsoContext *`;
- UI actions use absolute active-queue indices;
- `app_controller` remains the sole coordinator;
- `auto_seq` remains independent of MiniShell/UI/platform APIs;
- dropped QSO metadata is copied into AutoSeq's inactive zone, never retained through an RX/UI pointer;
- stale/out-of-range drops and inapplicable rotations are non-fatal no-ops.

## Drop behavior

`APP_ACTION_DROP_TX_QSO` calls `auto_seq_drop_index()` with a caller-supplied monotonic timestamp.

V2 behavior is preserved:

```text
normal active QSO -> move to inactive zone
CQ/CALLING        -> remove directly
```

Inactive storage is not displayed on T. Its purpose is to preserve QSO-lifetime metadata for a later addressed retry.

The timestamp comes from MiniShell `Time/Location.monotonic_us()` through `app_controller`; AutoSeq itself does not read a clock.

## Reactivation behavior

Automatic addressed-message handling from AS-4 already calls `auto_seq_on_addressed_rx()`. If the sender matches an inactive context, the pure core:

1. removes that context from the inactive zone;
2. appends it to the active zone;
3. resets retry counter and inactive timestamp;
4. re-anchors TX parity from the new RX slot;
5. applies the addressed RX message;
6. sorts/cleans the active queue.

AS-5 adds an integrated production proof of this path rather than changing the core implementation.

## Same-parity rotation

`APP_ACTION_ROTATE_TX_QUEUE` calls `auto_seq_rotate_same_parity()`.

The operation preserves V2 semantics: inspect the queue head's TX parity, find the contiguous front run with the same parity, and move the head to the end of that run. If fewer than two eligible entries exist, the command is a non-fatal no-op.

The `tests/kfs16b12k.wav` CQ fixture is ideal here because all eight selected CQs came from one RX slot and therefore all eight have the same opposite TX parity.

Initial queue:

```text
N4NJJ
AG6X
AE7KJ
W7RPS
N7REB
WN0KS
N5CH
KQ4PUG
```

After one rotate:

```text
AG6X
AE7KJ
W7RPS
N7REB
WN0KS
N5CH
KQ4PUG
N4NJJ
```

The production Linux test then drops `AG6X` from page 1 and drops `N4NJJ` as the sole row on page 2. This proves the page-local key is translated into the correct absolute active-queue index and that T paging collapses from 2 pages to 1 when six active entries remain.

## Retry and priority staging

AS-2 already contains deterministic pure-core coverage for:

- retry counting and exhaustion;
- no-exchange eviction versus exchanged-QSO parking;
- state-priority ordering;
- same-state retry count as a sort key;
- inactive reactivation;
- same-parity rotation;
- explicit drop behavior;
- full 30-entry capacity and oldest-inactive eviction;
- unknown mid-QSO/signoff reincarnation guards.

AS-5 deliberately does **not** call `auto_seq_tick()` from production application code. In V2, tick is semantically a post-TX-completion operation. V3 does not yet have a TX lifecycle, so fabricating a production tick source here would violate the staged ownership design.

The production boundary `TX completed -> auto_seq_tick()` belongs to AS-7, when TxIntent and simulated TX completion are introduced.

One V2 nuance remains intentionally unchanged: incrementing the head retry count inside `auto_seq_tick()` does not immediately call `sort_and_clean()`. Retry count affects ordering the next time sorting occurs through the normal event flow. AS-5 does not alter this behavior.

## Integrated reactivation proof

The FT8 Reference integration harness constructs two consecutive real RX slots from pinned V2 WAV goldens:

```text
slot N    W1ABC K9XYZ -12
slot N+1  W1ABC K9XYZ RR73
```

With local station `W1ABC`:

```text
slot N decode
 -> automatic AS-4 QSO creation
 -> K9XYZ RRPT 0/3

T line 1 drop
 -> K9XYZ leaves active queue
 -> metadata parked inactive

slot N+1 RR73
 -> automatic addressed-message lookup
 -> inactive K9XYZ reactivated
 -> existing metadata preserved
 -> state advances to SOFF
 -> exactly one K9XYZ active row
```

No physical or simulated TX occurs during this proof.

## Tests

AS-5 extends:

```text
tests/ft8_ui_smoke.c
    T line -> absolute APP_ACTION_DROP_TX_QSO
    page-2 absolute index
    Enter -> APP_ACTION_ROTATE_TX_QUEUE

tests/linux_ft8.py
    real kfs 16/8 decode/selection
    eight-entry same-parity rotation
    page-1 drop
    page-2 absolute drop
    2-page -> 1-page collapse

tests/linux_ft8_rx7.py
    real addressed TX2 -> active RRPT
    T drop -> inactive parking
    next-slot addressed RR73 -> inactive reactivation -> SOFF
```

The existing AS-2 pure unit suite remains the policy oracle; AS-5 does not duplicate those state-machine tests at the UI layer.

## Result

AS-5 completes the pre-TX queue-lifecycle application boundary:

```text
RX/manual events -> AutoSeq active queue
                    |
                    +-> T screen projection
                    +-> user drop -> inactive metadata
                    +-> user rotate -> same-parity fairness control
                    `-> later addressed RX -> inactive reactivation
```

The next stage is **AS-6: CQ/Beacon, FreeText, Field Day and logging-eligibility behavior**. TxIntent/slot execution remains AS-7.
