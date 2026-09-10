# MiniFT8-V3 Development

MiniFT8 is a portable MiniShell application with independent RX Audio, TX Audio, and Control resources.

Current development rule:

```text
backend       Linux
presentation  ADV when UI is involved
```

Stay on Linux until a genuine embedded-backend dependency must be exercised. MiniFT8-V2 remains the behavioral/golden reference; V2 structure is not copied wholesale.

## Current priority

The **decode-RX milestone is complete through RX-7**. The next major block is now **AutoSeq (AS)**.

AutoSeq is a boundary/ownership port first. Preserve the current MiniFT8-V2 AutoSeq behavior pinned at:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

Change data representation and ownership without redesigning scheduling/QSO behavior. `next_tx` is intentionally derived from QSO state rather than stored independently. See `as-plan.md` and `as-boundary-audit.md`.

Production RX tuning has intentionally advanced beyond the original RX-1C/V2-compatible monitor baseline. The current default is `time_osr=2, freq_osr=2`; `2x1` remains the low-memory/reference fallback. See `rx-tuning.md` for measurements and RAM policy.

Validated production RX path:

```text
MiniShell Audio
12 kHz / S16 / 2-channel
        |
        v
rx_audio_adapter
        |
        v
RxFrontend
        | 6 kHz mono float
        v
RxSlotFramer
        | exact 960-sample blocks
        v
Ft8Engine
        v
Ft8ProtocolSlot
        v
RxResultBuilder
        v
RxBatch
        v
app_controller
        v
UiModel
        v
ADV 20x7 presentation
```

`app_controller` remains the sole production coordinator. AutoSeq attaches after `RxResultBuilder` under `app_controller`; it does not become a second coordinator.

## Current stage status

```text
A0-A3       COMPLETE
P1          COMPLETE
P2          COMPLETE
V1          COMPLETE

RX-0        COMPLETE — V2 source/ownership review
RX-1A       COMPLETE — golden boundaries
RX-1B       COMPLETE — top-down RX ownership design
RX-1C       COMPLETE — monitor ownership/workspace
RX-1D       COMPLETE — candidate + LDPC + CRC
RX-1E       COMPLETE — explicit Ft8HashStore
RX-1F       COMPLETE — typed protocol codec
RX-1G       COMPLETE — pure Ft8Engine assembly
RX-2        IMPLEMENTED — host decoder; manual pc-1 validation pending
RX-3        COMPLETE — 12 kHz -> 6 kHz frontend
RX-4        COMPLETE — streaming slot framer
RX-5        COMPLETE — pure RX assembly -> RxBatch
RX-6        COMPLETE — MiniShell Audio + Linux WAV integration
RX-7        COMPLETE — decoded RX UI + ADV cross-build

AS-0        COMPLETE — plan, V2 reference freeze, boundary/ownership audit
AS-1        NEXT — complete station identity, factual SNR, RX selection boundary
AS-2        PLANNED — pure compact AutoSeq structural port
AS-3        PLANNED — CQ selection + real multi-QSO T screen
AS-4        PLANNED — automatic addressed-to-me progression
AS-5        PLANNED — retry/priority/inactive/reactivation/queue controls
AS-6        PLANNED — CQ/FreeText/Field Day/logging eligibility behavior
AS-7        PLANNED — slot/TxIntent lifecycle with simulated TX completion
AS-8        PLANNED — V2-equivalence closure on Linux + ADV
```

RX-2's pending manual pc-1 test does not block the structural sequence because its pinned Linux reference is green.

## Locked RX ownership

```text
MiniShell
    owns Audio provider/device/backend resource

rx_audio_adapter
    owns MiniFT8's public RX Audio stream handle lifecycle

RxFrontend
    owns 12 kHz S16 two-channel -> 6 kHz mono-float adaptation

RxSlotFramer
    owns slot identity, sample counting, and one bounded 960-sample accumulator

Ft8Engine
    owns FT8 monitor/waterfall/candidate/LDPC/CRC/hash/protocol state

RxResultBuilder
    owns factual application projection into RxBatch

app_controller
    remains sole application coordinator/policy owner and supplies initial timing
```

## Locked AutoSeq ownership

```text
RxResultBuilder
    produces factual RxBatch/RxMessage only
        |
        v
app_controller
    resolves user selection and ordered decode/slot/TX-completion events
        |
        v
auto_seq
    owns QSO queue, state progression, retries, priority and inactive/reactivation policy
        |
        +--> QsoView -> app_controller -> UiModel -> T UIScreen
        `--> TxIntent/policy events -> app_controller -> future TX/logging
```

`auto_seq` must not call MiniShell, `Ft8Engine`, `Ft8HashStore`, UI code, filesystem/logging code, Audio, Control, or platform APIs. It uses fixed-size C data and no AutoSeq heap allocation.

The current `qso_scheduler` is only a prototype settings holder and will be removed when `auto_seq` becomes the real owner in AS-2; do not keep two scheduler/QSO-policy owners.

## Locked data/timing contracts

MiniShell transport:

```text
sample rate  12000 Hz
format       signed 16-bit PCM
channels     2 ordered channels
```

Engine-native stream:

```text
sample rate  6000 Hz
format       mono float
block        960 samples
```

FT8 slot:

```text
15 s x 6000 = 90000 samples
93 x 960     = 89280 samples
remainder    =   720 samples
```

The 720-sample slot-end remainder is discarded. A first partial slot after stream start/discontinuity is also discarded. UTC/time supplies only the initial `slot_id + sample_offset`; sample count owns progression after that.

For AutoSeq/TX, preserve the V2 event rule: decode completion updates AutoSeq and produces a pending semantic intent; a later slot-boundary event decides execution; TX completion/tick advances retry/QSO scheduling. AutoSeq does not poll a clock or start TX itself.

## RX production proof

The real `tests/kfs16b12k.wav` fixture is now a production cross-platform anchor at 2x2:

```text
Linux  16 decoded messages
ADV    16 decoded messages
CQ      8 messages
```

ADV memory after the completed 2x2 WAV decode:

```text
heap free       111.3 KiB
largest block    53.0 KiB
app allocation  228.5 KiB
```

The eight CQs will be reused by AS-3 as the first real multi-QSO queue/T-screen fixture.

## RX-6 proof

```text
M$> RX6 frames=180000 slot=12345 blocks=93 messages=1 text="CQ W1XYZ FN42"
ft8_rx_probe: PASS
```

This proved that a future QMX or other MiniShell Audio provider can replace the Linux WAV provider without changing `RxFrontend`, `RxSlotFramer`, `Ft8Engine`, or `RxResultBuilder`.

## RX-7 proof

The normal `ft8` application consumes real `RxBatch` results and renders them on the RX screen.

```text
M$> ft8 --profile adv --rx /flash/rx7.wav --rx-slot 12345
RX 20 HH:MM:SS 1/1 <0-E>
1 CQ W1XYZ FN42
```

The RX-7 golden workflow passes through the real MiniShell runtime and production application. The same head passes Linux/reference CI and the ESP-IDF v5.5.1 ESP32-S3 Cardputer ADV firmware build.

## Locked structural rules

1. Stream raw audio; retain the waterfall; retain full-slot PCM only by explicit exception.
2. Preserve the proven 6 kHz FT8-engine boundary during cleanup.
3. Preserve V2 algorithms/behavior unless a separately measured change is intended.
4. Exact payload bytes are authoritative protocol-message identity.
5. Protocol type is first-class; typed fields are authoritative and canonical text is derived convenience.
6. `Ft8HashStore` is explicit per-engine state, not global, AutoSeq, or MiniShell state.
7. Station identity may be factual context but does not imply reply/TX policy.
8. FREE_TEXT may additionally be logical CQ only for `CQ <nnn|AAAA> <valid-callsign> [grid]`; protocol type remains FREE_TEXT.
9. App/module failure should remain local and must not destabilize MiniShell or unrelated applications.
10. `app_controller` remains the sole production coordinator.
11. Repeated bounded state such as AutoSeq flags, active-QSO metadata, hash metadata, and candidate flags should use narrow fields, masks, or bitsets when the RAM saving is material; compactness must not obscure correctness or timing-sensitive behavior.
12. AutoSeq must copy QSO-lifetime facts from `RxMessage`; it must never retain pointers into an `RxBatch` owned by RX state.
13. During AS-1..AS-8, V2 AutoSeq behavior is frozen as the oracle; improvements are deferred and introduced one measured change at a time after equivalence.

## Canonical records

RX:

```text
rx.md
rx-golden.md
rx-1b-design.md
rx-1c-monitor.md
rx-1d-decoder.md
rx-1e-hash-store.md
rx-1f-message-codec.md
rx-1g-engine.md
rx-2-host-decoder.md
rx-3-frontend.md
rx-4-slot-framer.md
rx-5-pure-assembly.md
rx-6-minishell-audio.md
rx-7-decoded-ui.md
rx-tuning.md
```

AutoSeq:

```text
as-plan.md
as-boundary-audit.md
```

## Next

Start **AS-1** with three bounded boundary-completion tasks:

```text
AS-1a  ConfigService owns station callsign/grid and app_controller injects callsign into RxResultBuilder
AS-1b  factual RX SNR is carried into RxMessage; AutoSeq never estimates it
AS-1c  RX 1..6 selection emits an absolute decoded-message AppAction resolved by app_controller
```

No QSO state-machine code should be added until these input boundaries are explicit.
