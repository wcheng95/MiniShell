# RX-7 — Decoded RX UI

Status: **COMPLETE / Linux reference + ADV build validated**

RX-7 moves the validated receive chain into the normal production `ft8` application and renders real decoded `RxBatch` results on the RX screen.

## Production boundary

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

`app_controller` remains MiniFT8's sole production coordinator. RX-7 does not add an `rx_pipeline`, `rx_manager`, background RX task, or second coordinator.

## Application integration

The production controller owns the receive composition and its bounded working state. It steps MiniShell Audio cooperatively in small chunks so decoding can progress while UI input remains responsive.

For live RX, the controller reads MiniShell UTC once to establish the initial FT8 `slot_id + sample_offset`; `RxSlotFramer` then advances by sample count. For deterministic replay, an explicit slot reference can be supplied.

The latest completed `RxBatch` is retained by the application and projected into `UiModel`. The old hard-coded demo RX lines are gone.

## RX presentation

ADV presentation implements the locked 20-character top row, for example:

```text
RX 20 14:32:08 1/2 8
```

The six content lines show real canonical decoded messages. Up/Down at UIScreen top level moves between pages with wraparound. Up to the FT8 candidate/message capacity of 50 decoded lines can be represented in the current model.

Switching UIScreens enters the destination at top level. Existing Back/ESC behavior remains unchanged.

## Production reference proof

The dedicated RX-7 workflow converts the pinned MiniFT8-V2 6 kHz golden CQ capture to the MiniShell Audio transport format, mounts it as a WAV endpoint, and launches the actual production `ft8` application through MiniShell:

```text
M$> ft8 --profile adv --rx /flash/rx7.wav --rx-slot 12345
RX 20 HH:MM:SS 1/1 <0-E>
1 CQ W1XYZ FN42
```

The exact protocol identity remains:

```text
payload         000000206016500A1988
canonical text  CQ W1XYZ FN42
```

This proves the path from public MiniShell Audio through the complete RX engine and into the normal RX UI.

## ADV build proof

RX-7 initially exposed that the ADV ESP-IDF component list had not yet included the new receive/engine sources and include directories. After the ADV build definition was brought in line with the production MiniFT8 source set, the Cardputer ADV firmware build passed under:

```text
ESP-IDF  v5.5.1
target   esp32s3
```

The ADV registry/unit job also passes.

This proves source/build portability to the ADV target. RX-7 does not add a live ADV Audio provider, so actual live ADV receive behavior remains outside this stage.

## Validation gate

On the final RX-7 code head before documentation closeout, the following CI families are green:

```text
Linux
RX-1C Reference
RX-1D Reference
RX-1G Reference
RX-2 Reference
RX-3 Reference
RX-4 Reference
RX-6 Reference
RX-7 Reference
ADV firmware-build
ADV registry-unit
```

## Not in RX-7

RX-7 deliberately does not add:

```text
AutoSeq
TX
ADIF logging
QMX live Audio/CAT provider work
ADV live Audio provider work
new DSP/search/LDPC/SNR algorithms
```

## Milestone result

The decode-RX milestone is complete. MiniFT8 now has a production, platform-independent receive path from MiniShell Audio through FT8 decoding to the real UI, with Linux serving as the reference target and ADV cross-build validation protecting the embedded port.

The next major block should be selected and designed separately rather than implicitly extending RX-7.
