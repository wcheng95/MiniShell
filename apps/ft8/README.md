# MiniFT8

MiniFT8-V3 is a portable MiniShell FT8 application. Its MiniShell runtime name is `ft8`.

Canonical application documentation lives under:

```text
docs/MiniFT8/
```

The application depends on MiniShell services rather than Linux, NuttX, ESP-IDF, board APIs, or test mocks. Other protocols such as FT4, CW, RTTY, and JS8 are separate future MiniShell applications rather than modes inside `ft8`.

## Presentation profiles

```text
DESKTOP   30 x 8
ADV       20 x 7
```

Linux:

```text
M$> ft8 --profile desktop
M$> ft8 --profile adv
```

Current development policy is **Linux backend first**, using ADV presentation whenever UI is involved. Cardputer ADV supplies ADV as its composition default.

## Production RX architecture

RX-7 completes the decode-RX path through the normal application:

```text
MiniShell Audio
12 kHz / S16 / 2-channel
    -> rx_audio_adapter
    -> RxFrontend
       6 kHz mono float
    -> RxSlotFramer
       sample-count slot framing
       exact 960-sample engine blocks
    -> Ft8Engine
       monitor/waterfall
       candidate search
       likelihood/LDPC/CRC
       Ft8HashStore
       typed protocol codec
       exact-payload dedupe
    -> Ft8ProtocolSlot
    -> RxResultBuilder
    -> RxBatch
    -> app_controller
    -> UiModel
    -> UI presentation
```

`app_controller` remains the sole production coordinator. There is no second RX manager or pipeline coordinator.

Normal RX streams raw audio and does not retain a complete raw PCM slot.

## RX stage status

```text
RX-1A..1G  COMPLETE
RX-2        IMPLEMENTED / pc-1 manual test pending
RX-3        COMPLETE
RX-4        COMPLETE
RX-5        COMPLETE
RX-6        COMPLETE
RX-7        COMPLETE — real decoded RX screen + ADV build
```

RX-2's pinned Linux reference is green, so its pending manual test does not block the structural sequence.

## Timing

For live reception:

```text
UTC/time establishes initial slot_id + sample_offset
sample count owns progress afterward
```

Production `app_controller` obtains the initial MiniShell time reference. `rx_audio_adapter` does not own UTC; `Ft8Engine` never reads a clock.

For a 15-second FT8 slot at the engine boundary:

```text
90000 samples total
93 x 960 = 89280 samples delivered to Ft8Engine
720-sample slot-end remainder discarded
```

## RX-7 reference proof

The RX-7 integration test launches the actual production `ft8` application through MiniShell, opens a real 12 kHz/S16/stereo WAV through the public Audio API, and verifies the pinned V2 CQ appears on the ADV RX screen.

```text
M$> ft8 --profile adv --rx /flash/rx7.wav --rx-slot 12345
RX 20 HH:MM:SS 1/1 <0-E>
1 CQ W1XYZ FN42
```

Exact golden identity remains:

```text
waterfall FNV-1a-64   18BE1E838FD9C6AF
payload                000000206016500A1988
canonical text         CQ W1XYZ FN42
```

The RX-7 head also cross-compiles successfully for Cardputer ADV with ESP-IDF v5.5.1 / ESP32-S3. This is a build-portability proof; a live ADV Audio provider is not part of RX-7.

Replacing the Linux WAV provider later with QMX or another MiniShell Audio provider must not require changes inside `RxFrontend`, `RxSlotFramer`, `Ft8Engine`, or `RxResultBuilder`.

## UI behavior established in RX-7

ADV uses the locked 20-character status line. Real RX messages are available from the latest `RxBatch`, six lines per page. Top-level Up/Down paging wraps, and switching UIScreens returns to the destination screen's top level.

Detailed per-UIScreen actions remain governed by `docs/MiniFT8/ui.md` and are separate from the decode-RX milestone.

## Canonical RX records

```text
docs/MiniFT8/rx.md
docs/MiniFT8/rx-6-minishell-audio.md
docs/MiniFT8/rx-7-decoded-ui.md
```

## After RX-7

Decode RX stops here. AutoSeq, TX, ADIF, live radio/provider integration, and remaining UI behavior stay separate major blocks.
