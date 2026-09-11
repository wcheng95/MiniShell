# MiniFT8-V3 FQ-0 Boundary and Ownership Freeze

Status: **FQ-0 implementation contract**

FQ-0 freezes the first-QSO module boundary before real timing, live UAC, RxTxLog, serial/CAT, or RF execution is added.

The purpose is not to add features. It is to make ownership and permitted communication explicit enough that later work cannot accidentally create cross-talk.

## Core rule

`app_controller` is the sole MiniFT8 production coordinator.

Peer MiniFT8 modules do not call each other for policy or configuration. They expose narrow interfaces and receive the facts/configuration they need from `app_controller` or from their owning adapter boundary.

A helper that is structurally inside one service is not a peer service. For example, a future QMX CAT protocol helper may live under `radio_service`; that does not make it an independent coordinator.

## Normative production call graph

```text
ft8_main
    |
    +-> ft8_ui_adapter ------------------------> MiniShell Display/Input
    |
    `-> app_controller
            |
            +-> MiniShell Time/Location
            +-> MiniShell Memory
            |
            +-> config_service
            +-> storage_service ---------------> MiniShell Filesystem
            |
            +-> rx_audio_adapter --------------> MiniShell Audio
            +-> rx_frontend
            +-> rx_slot_framer
            |       `-> controller-owned callback
            |               +-> ft8_engine
            |               `-> rx_result_builder
            |
            +-> auto_seq
            +-> tx_lifecycle
            |
            `-> radio_service                  [FQ-5]
                    |
                    +-> QMX CAT protocol helper
                    `-> serial adapter --------> MiniShell serial byte stream [FQ-4]
```

The graph is intentionally **not**:

```text
TxLifecycle -> radio_service
AutoSeq -> radio_service
AutoSeq -> storage_service
radio_service -> config_service
rx_frontend -> Ft8Engine
RxSlotFramer -> Ft8Engine
```

Those would be peer-module cross-talk.

`RxSlotFramer` emits generic events through a caller-supplied callback. The callback is owned by `app_controller`; therefore the framer does not know or call `Ft8Engine` directly.

## Ownership table

| Owner | Owns | May depend on | Must not own/know |
| --- | --- | --- | --- |
| `ft8_main` | application lifetime and top-level loop | public controller/UI-adapter interfaces | FT8 policy, DSP, QMX protocol, platform device APIs |
| `ft8_ui_adapter` | MiniShell Display/Input adaptation | MiniShell Display/Input, `ui_shell` types | FT8 policy, radio/audio/storage behavior |
| `app_controller` | production sequencing and policy coordination | narrow MiniFT8 modules plus MiniShell Time/Location and Memory | Linux/ADV device APIs |
| `config_service` | parsed persistent station/config values | pure C data | filesystem I/O, MiniShell APIs, radio/audio/UI side effects |
| `storage_service` | MiniFT8 filesystem adapter | MiniShell Filesystem API only | config policy, logging policy, radio/audio/UI |
| `rx_audio_adapter` | MiniFT8 RX stream-handle lifecycle | MiniShell Audio API only | DSP, slot policy, AutoSeq, storage |
| `rx_frontend` | 12 kHz S16/2-channel -> 6 kHz mono-float adaptation | pure C data | Audio API, clock, decoder, AutoSeq |
| `rx_slot_framer` | sample-count slot progression after anchor | pure C data and caller callback | UTC source, decoder, AutoSeq, Audio API |
| `ft8_engine` | FT8 DSP/hash/protocol decode state | its internal engine helpers | MiniShell, UI, AutoSeq, storage, radio |
| `rx_result_builder` | factual decoded-message projection/classification | typed FT8 protocol output | AutoSeq policy, UI policy, MiniShell |
| `auto_seq` | QSO queue/state/retry/priority policy | caller-supplied factual events/config | MiniShell, clock, storage, radio, Audio, UI |
| `tx_lifecycle` | slot/parity execution eligibility | caller-supplied slot position | AutoSeq, radio, CAT, Audio, storage, MiniShell clock |
| `radio_service` | application-level radio execution | caller-supplied TX request; narrow radio helper/serial adapter | AutoSeq internals, UI, config service, Linux tty APIs |
| QMX CAT helper | QMX command semantics/formatting | radio-service-owned transport seam | Linux tty/USB implementation, AutoSeq, UI, config |
| serial adapter | MiniFT8 serial stream-handle lifecycle | MiniShell serial byte-stream API only | QMX command semantics, AutoSeq, UI, config |

## No-cross-talk rules

### Configuration

`config_service` never pushes configuration into another module.

```text
config_service
      ^
      |
app_controller
      |
      +-> auto_seq_set_...()
      +-> future radio_service configuration
      `-> future logging configuration
```

This is the same rule used for the live GPS grid today: `app_controller` reads MiniShell Time/Location and explicitly updates AutoSeq station identity. AutoSeq does not read Time/Location or configuration itself.

### RX

The transport/DSP path is coordinated, not chained by hidden ownership:

```text
app_controller
    read       -> rx_audio_adapter
    adapt      -> rx_frontend
    frame      -> rx_slot_framer
    callback   -> ft8_engine
    project    -> rx_result_builder
    policy     -> auto_seq
```

Data may flow from one stage to the next, but the stages do not discover or call their peers.

### TX

The TX path must keep **eligibility**, **policy**, and **execution** separate:

```text
                    app_controller
                    /      |       \
                   /       |        \
          tx_lifecycle   auto_seq   radio_service
          when allowed   what TX    execute TX
```

The controller obtains an `AutoSeqTxIntent`, asks `TxLifecycle` whether the real UTC slot is executable, then asks `radio_service` to execute the already-decided request.

`TxLifecycle` never calls `radio_service`.

`AutoSeq` never calls `radio_service`.

`radio_service` never reaches back into AutoSeq state.

### Logging

RxTxLog policy belongs above storage:

```text
app_controller / future log formatter
        |
        `-> storage_service -> MiniShell Filesystem
```

`storage_service` only performs filesystem operations. It does not decide what an RX/TX record means, when a record should be written, or how timestamps/frequencies are compressed.

### Platform boundary

MiniFT8 application code never uses Linux, ESP-IDF, NuttX, board, ALSA, tty, USB-host, I2S, GPIO, or other platform APIs directly.

Only MiniShell providers own those details.

The portable application sees MiniShell service contracts.

## Narrow MiniShell API exposure

Prefer passing the narrowest MiniShell service pointer into an adapter rather than the whole `mini_api_t` table:

```text
storage_service       <- mini_fs_api_t
rx_audio_adapter      <- mini_audio_api_t
future serial adapter <- mini_serial_api_t
```

`app_controller` may retain `mini_api_t` because it is the production coordinator and currently owns Time/Location and Memory use.

Pure policy/DSP modules must not receive `mini_api_t` or any MiniShell service pointer.

## Existing-tree audit

The current tree already follows the important FQ-0 rules:

- `auto_seq` is pure and heap-free;
- `tx_lifecycle` is pure and consumes caller-supplied slot position;
- `rx_audio_adapter` receives only `mini_audio_api_t`;
- `storage_service` receives only `mini_fs_api_t`;
- `RxSlotFramer` knows only a generic emit callback, not `Ft8Engine`;
- `app_controller` owns the RX pipeline sequencing and the AutoSeq handoff;
- live GPS location is read by `app_controller` and pushed into AutoSeq rather than AutoSeq reading Time/Location;
- `ft8_ui_adapter` is the Display/Input boundary and does not own application policy;
- the existing platform-boundary test rejects Linux/ESP-IDF/board leakage from MiniFT8.

One planning-diagram ambiguity is corrected by this document: `TxLifecycle` and future `radio_service` are siblings under `app_controller`, not a call chain.

## Mechanical guards

FQ-0 extends the existing `tests/ft8_platform_boundary.py` test so architecture drift fails CI.

The guard should enforce at least:

1. pure modules cannot include `minishell/api.h`;
2. `app_controller_internal.h` cannot be included outside `src/app_controller`;
3. local MiniFT8 module includes must follow the allowed dependency graph;
4. existing platform-header/token bans remain active;
5. AutoSeq remains heap-free.

This test is intentionally structural. It does not attempt to prove all runtime behavior, but it makes the most likely accidental cross-talk visible immediately in CI.

## FQ-0 completion criteria

FQ-0 is complete when:

- this ownership/call graph is accepted as the first-QSO contract;
- the first-QSO overview uses the same sibling relationship for `TxLifecycle` and `radio_service`;
- CI mechanically guards platform leakage and module dependency direction;
- no existing MiniFT8 source violates the frozen graph;
- no feature behavior changes are introduced by FQ-0.

After that, FQ-1 may add the shared real 15-second timing boundary without reopening module ownership.
