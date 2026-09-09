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

Current development policy is **Linux backend first**, using the ADV presentation whenever UI is involved. Cardputer ADV supplies ADV as its composition default. Presentation is not persisted station configuration.

## Current RX architecture

RX-6 validates the receive path through the MiniShell public Audio API:

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
```

`app_controller` remains the sole production coordinator. There is no second RX manager or pipeline coordinator.

Ownership:

```text
MiniShell
    owns Audio provider/device/backend resource

rx_audio_adapter
    owns MiniFT8's public RX Audio stream handle and
    open/start/read/stop/close lifecycle

RxFrontend
    owns channel interpretation and 12 kHz -> 6 kHz adaptation

RxSlotFramer
    owns slot identity/sample counting and one bounded 960-sample accumulator

Ft8Engine
    owns FT8 DSP/protocol/hash state

RxResultBuilder
    owns factual RxBatch projection

app_controller
    owns application coordination/policy
```

Normal RX streams raw audio and does not retain a complete raw PCM slot.

## RX stage status

```text
RX-1A..1G  COMPLETE
RX-2        IMPLEMENTED / pc-1 manual test pending
RX-3        COMPLETE
RX-4        COMPLETE
RX-5        COMPLETE
RX-6        COMPLETE
RX-7        NEXT — real decoded RX screen
```

RX-2's pinned Linux reference is green, so its pending manual test does not block the structural sequence.

## RX-6 MiniShell Audio proof

The RX-6 integration test launches through the actual MiniShell Linux runtime and opens a real 15-second 12 kHz/S16/stereo WAV through the public Audio API.

The Audio provider is therefore outside MiniFT8:

```text
Linux WAV provider
    -> MiniShell Audio API
    -> rx_audio_adapter
    -> unchanged pure RX modules
```

Result:

```text
M$> RX6 frames=180000 slot=12345 blocks=93 messages=1 text="CQ W1XYZ FN42"
ft8_rx_probe: PASS
```

Exact golden identity remains:

```text
waterfall FNV-1a-64   18BE1E838FD9C6AF
payload                000000206016500A1988
canonical text         CQ W1XYZ FN42
```

Replacing the Linux WAV provider later with QMX or another MiniShell Audio provider must not require changes inside `RxFrontend`, `RxSlotFramer`, `Ft8Engine`, or `RxResultBuilder`.

Canonical RX-6 records:

```text
docs/MiniFT8/rx-6-minishell-audio.md
apps/ft8/src/rx_audio_adapter/README.md
```

## Timing

For live reception:

```text
UTC/time establishes initial slot_id + sample_offset
sample count owns progress afterward
```

`rx_audio_adapter` deliberately does not own UTC. The deterministic RX-6 replay test supplies a known initial slot reference directly. Production coordination supplies the timing reference when the real application is wired in RX-7.

For a 15-second FT8 slot at the engine boundary:

```text
90000 samples total
93 x 960 = 89280 samples delivered to Ft8Engine
720-sample slot-end remainder discarded
```

## Existing application services

The normal `ft8` application already uses MiniShell Display, Input, Filesystem, configuration, and lifecycle services. RX-6 proves MiniShell Audio separately before changing the real RX screen.

P1/P2/V1 real-ADV validation remains the pre-RX hardware baseline:

```text
heap free       about 282 KiB
largest block   about 228 KiB
```

Do not optimize RX RAM prematurely; measure after production integration.

## Next: RX-7

RX-7 will wire the validated receive composition into the normal `ft8` application and render real decoded `RxBatch` messages using the **Linux backend + ADV 20x7 presentation** first.

AutoSeq, TX, and ADIF remain separate major blocks after the decode-RX milestone.
