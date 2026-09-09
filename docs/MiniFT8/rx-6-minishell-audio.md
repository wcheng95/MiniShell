# RX-6 — MiniShell Audio Integration

Status: **COMPLETE / Linux CI validated**

RX-6 connects the cleaned MiniFT8 receive domain to the MiniShell public Audio API without changing the pure RX modules.

## Boundary

```text
Linux WAV provider
        |
        v
MiniShell Audio service
12 kHz / S16 / 2-channel
        |
        v
rx_audio_adapter
        |
        v
RxFrontend
        |
        | 6 kHz mono float
        v
RxSlotFramer
        |
        v
Ft8Engine
        |
        v
Ft8ProtocolSlot
        |
        v
RxResultBuilder
        |
        v
RxBatch
```

`rx_audio_adapter` belongs to MiniFT8. MiniShell owns the actual provider/device/backend audio resource. The adapter owns only MiniFT8's public stream handle and `open/start/read/stop/close` lifecycle.

No `rx_pipeline`, `rx_manager`, or other second application coordinator is introduced. `app_controller` remains the production coordinator; RX-6 uses a dedicated test probe to prove the integration boundary before RX-7 wires the same pieces into the real application/UI.

## Transport contract

RX-6 opens MiniShell RX Audio with the locked V1 transport:

```text
sample rate  12000 Hz
format       signed 16-bit PCM
channels     2
```

The endpoint is provider-defined. The Linux reference uses a logical MiniShell filesystem path. Future QMX or other live providers may use a different endpoint while preserving the same public Audio contract.

The adapter is streaming and allocator-free. It retains no full-slot raw PCM.

## Status and failure mapping

The adapter distinguishes:

```text
OK
END_OF_STREAM
INVALID
UNAVAILABLE
STATE
MINISHELL_ERROR
```

`MINI_ERR_END_OF_STREAM` is a normal distinct streaming result rather than a generic I/O failure. Other MiniShell results remain available through `rx_audio_adapter_last_result()` for diagnostics.

A MiniShell/provider failure therefore stops the MiniFT8 receive path at the adapter boundary; it does not require changes to or hidden recovery inside `RxFrontend`, `RxSlotFramer`, `Ft8Engine`, or `RxResultBuilder`.

## Unit proof

`tests/rx_audio_adapter_rx6_test.c` uses a fake public MiniShell Audio service and verifies:

```text
capability/interface validation
fixed 12 kHz/S16/2-channel open format
open/start/read lifecycle
END_OF_STREAM translation
cleanup-safe close
state validation
```

The test is part of the normal Linux CTest suite as `rx_audio_adapter_rx6_unit` and is also exercised by the dedicated RX-6 reference workflow.

## Full MiniShell reference proof

The dedicated RX-6 workflow creates a real 15-second 12 kHz/S16/stereo WAV fixture from the pinned MiniFT8-V2 CQ golden. Each 6 kHz golden sample is represented as two identical 12 kHz stereo frames and the remainder of the FT8 slot is zero-filled.

The test module is then launched **through MiniShell** and opens `/flash/rx6.wav` using the public Audio API. MiniShell's Linux WAV provider performs the actual file I/O.

The test reads in bounded 257-frame chunks and passes them through the unchanged RX-3/RX-4/RX-5 modules.

Result:

```text
M$> RX6 frames=180000 slot=12345 blocks=93 messages=1 text="CQ W1XYZ FN42"
ft8_rx_probe: PASS
```

The exact protocol payload remains:

```text
000000206016500A1988
```

This proves the desired substitution boundary:

> Replacing the Linux WAV provider with a QMX or other MiniShell Audio provider must not require changes inside `RxFrontend`, `RxSlotFramer`, `Ft8Engine`, or `RxResultBuilder`.

## Timing scope

RX-6 validates deterministic replay transport. The reference supplies `slot_id=12345` and `sample_offset=0` explicitly to `RxSlotFramer`; it does not add wall-clock acquisition to the Audio adapter.

That is intentional. The Audio adapter owns audio lifecycle, not UTC. Production coordination later supplies the framer's initial timing reference from the appropriate MiniShell time context; sample count remains authoritative after that reference.

## Not in RX-6

RX-6 deliberately does not:

```text
replace the prototype RX screen
add AutoSeq
add TX
add ADIF logging
add a QMX provider
add ADV audio backend code
change DSP/search/LDPC/SNR behavior
```

Those boundaries remain separate.

## Next

RX-7 connects real `RxBatch` results to the MiniFT8 RX screen using the Linux backend and ADV presentation first.
