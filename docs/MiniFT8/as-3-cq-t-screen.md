# MiniFT8-V3 AS-3: CQ Selection and Real T UIScreen

Status: **COMPLETE**

AS-3 connects the factual RX-selection boundary established in AS-1 to the pure fixed-size AutoSeq owner introduced in AS-2. It deliberately stops before automatic addressed-to-me processing and before any physical TX behavior.

## Ownership and data flow

```text
RxBatch / RxMessage
        |
        | visible RX line 1..6
        v
ui_shell
    resolves page-local line to absolute decoded-message index
        |
        v
APP_ACTION_SELECT_RX_MESSAGE
        |
        v
app_controller
    validates retained batch + absolute index
    classifies the selected factual RxMessage
    copies CQ facts into AutoSeqRxEvent
        |
        v
auto_seq_on_manual_rx()
        |
        v
AutoSeq active queue
        |
        v
auto_seq_snapshot_active()
        |
        v
AutoSeqQsoView[]
        |
        v
app_controller -> UiModel.tx_lines[]
        |
        v
ui_shell -> T UIScreen
```

`ui_shell` remains navigation/presentation code. It does not know `QsoContext`, inspect RX protocol fields, or make QSO-policy decisions.

`auto_seq` remains pure. It receives only its normalized event and exposes caller-owned QSO snapshots. It does not know `RxMessage`, `UiModel`, MiniShell, or the display.

`app_controller` remains the sole production coordinator between RX, AutoSeq, and UI.

## Selected CQ mapping

For AS-3, only a selected **resolved factual CQ** changes AutoSeq state.

The controller requires:

```text
RxMessage.is_cq == true
RxMessage.has_unresolved_hash == false
RxMessage.call_de is non-empty
```

It maps factual data as follows:

```text
RxBatch.slot_id       -> AutoSeqRxEvent.rx_slot_id
RxMessage.offset_hz   -> AutoSeqRxEvent.offset_hz
RxMessage.snr_db      -> AutoSeqRxEvent.snr_db
RxMessage.call_de     -> AutoSeqRxEvent.dxcall
valid 4-char extra    -> AutoSeqRxEvent.dxgrid
logical CQ            -> AUTO_SEQ_RX_FLAG_CQ
manual CQ start       -> AUTO_SEQ_MSG_TX1 normalized event kind
```

The controller does **not** parse `canonical_text` to recover QSO facts. Typed `RxMessage` fields remain authoritative.

Selecting a non-CQ is still a valid RX selection, preserving the AS-1 selection semantics, but it has no AutoSeq side effect in AS-3. Automatic `is_to_me` processing belongs to AS-4.

## T UIScreen projection

The T-screen model capacity now matches the complete bounded active AutoSeq queue:

```text
APP_MAX_TX_LINES = 30
AUTO_SEQ_MAX_QUEUE = 30
```

Only six rows are visible per ADV page; paging is unchanged.

Each active `AutoSeqQsoView` is projected into a compact line:

```text
<DXCALL:8> <STATE:4> <retry>/<limit>
```

Current state labels preserve the V2-style compact vocabulary:

```text
CALLING       CALL
REPLYING      RPLY
REPORT        RPRT
ROGER_REPORT  RRPT
ROGERS        RGRS
SIGNOFF       SOFF
```

The T screen consumes only these projected strings. It does not inspect AutoSeq storage.

## Real `kfs16b12k.wav` proof

Production input:

```text
tests/kfs16b12k.wav
profile       ADV
RX slot       12345
RX tuning     time_osr=2, freq_osr=2
Skip TX1      OFF
Max Retry     3
```

The normal MiniFT8 production path decodes 16 messages over three RX pages. Eight are factual CQs:

```text
CQ N4NJJ DM26
CQ AG6X DM12
CQ AE7KJ CN86
CQ W7RPS CN85
CQ N7REB CN74
CQ WN0KS EM19
CQ N5CH EM05
CQ KQ4PUG FM16
```

The integration test deliberately selects **all 16 decoded messages**. This proves that the controller, not the test harness, filters selection by factual CQ status. The resulting AutoSeq active queue contains exactly the eight CQs above.

Because Skip TX1 is OFF, all eight contexts begin in `REPLYING`, so their derived next message is TX1 and the T screen shows `RPLY 0/3`.

Observed T page 1:

```text
TX ... 1/2 ...
1 N4NJJ    RPLY 0/3
2 AG6X     RPLY 0/3
3 AE7KJ    RPLY 0/3
4 W7RPS    RPLY 0/3
5 N7REB    RPLY 0/3
6 WN0KS    RPLY 0/3
```

Observed T page 2:

```text
TX ... 2/2 ...
1 N5CH     RPLY 0/3
2 KQ4PUG   RPLY 0/3
```

All eight CQs were decoded in the same RX slot and therefore inherit the same opposite TX parity. AS-3 does not execute or simulate TX; the queue is only made visible.

## Tests

`ft8_ui_smoke` now independently verifies generic eight-entry T paging as six rows plus two rows, including wraparound.

`linux_ft8` exercises the complete production integration:

```text
MiniShell runtime
  -> ft8 app
  -> Linux WAV Audio provider
  -> production RX pipeline
  -> 16 decoded RxMessages
  -> actual RX UI paging and number-key actions
  -> app_controller CQ mapping
  -> real AutoSeq queue
  -> AutoSeqQsoView projection
  -> actual ADV T-screen paging
```

The first AS-3 test run exposed only a PTY test-harness chunk-boundary assumption. Its transcript already showed the correct six-plus-two queue. The harness was changed to wait for the final known row on each page before asserting; production code was not changed for that failure.

## Non-goals retained

AS-3 does not add:

- automatic processing of `RxMessage.is_to_me`;
- response handling for selected non-CQ messages;
- CQ beacon generation;
- FreeText;
- Field Day special sequencing;
- retry/tick execution through real slot events;
- `TxIntent` realization;
- TX Audio or Control use;
- logging side effects;
- physical transmission.

Those remain later AS stages. AS-4 is next and adds automatic addressed-to-me progression while preserving ordinary CQ as manual-only.
