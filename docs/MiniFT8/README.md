# MiniFT8-V3

MiniFT8-V3 is a substantial portable application hosted by MiniShell. This directory is the canonical documentation for active MiniFT8-V3 development.

The previous standalone MiniFT8-V3 repository has been superseded. Active development and authoritative architecture now live with MiniShell so application requirements and MiniShell API evolution can be developed and tested together.

## Current boundary

```text
MiniFT8-V3 project
    |
ft8 runtime application
    |
MiniShell API
    |
MiniShell services/runtime
    |
Linux / future NuttX / thick embedded backend / mocks
```

MiniFT8 contains no Linux, ncurses, ESP-IDF, NuttX, USB/UART/I2S, or board-specific path in its application core.

Code-facing names use lowercase; prose continues to use the project names MiniShell and MiniFT8.

## Protocol application boundary

MiniFT8 is now the project/design name while the MiniShell runtime application is simply:

```text
ft8
```

The runtime application is **FT8-only**. Protocol switching is handled by MiniShell application switching rather than an internal mode selector:

```text
ft8      current
ft4      future
cw       future
rtty     future
js8      future
```

Future protocol applications are created only when their implementation begins. No placeholder apps are maintained merely to reserve names.

## Major MiniFT8 domain blocks

With platform/storage responsibilities moved below MiniShell, the remaining major FT8 radio-domain blocks are intentionally small in number:

```text
RX
AutoSeq
TX
ADIF log
```

They are coordinated through `app_controller`; they do not call one another behind it.

## Current priority

RX-0 architecture/source review, RX-1A golden-boundary freeze, RX-1B top-down ownership design, RX-1C monitor cleanup, RX-1D candidate/LDPC/CRC cleanup, RX-1E explicit callsign-hash ownership, RX-1F typed protocol codec, RX-1G pure `Ft8Engine` assembly/golden regression, and the P1/P2/V1 platform/presentation checkpoint are complete.

Validated matrix:

```text
Linux backend + DESKTOP presentation   PASS
Linux backend + ADV presentation       PASS
ADV backend   + ADV presentation       PASS
```

Real ADV P2/V1 testing established the current pre-RX memory baseline:

```text
heap free       ~282 KiB
largest block   ~228 KiB
```

The next stage is:

```text
RX-2  pure MiniShell-independent host decoder/use harness
```

RX-2 will put the completed engine behind a small host-side 6 kHz PCM use boundary before the 12 kHz frontend and slot framer are introduced.

Canonical current records/plans:

```text
rx.md
rx-1b-design.md
rx-1c-monitor.md
rx-1d-decoder.md
rx-1e-hash-store.md
rx-1f-message-codec.md
rx-1g-engine.md
development.md
```

## Current integrated baseline

MiniShell-native MiniFT8 currently contains:

```text
text UI
DESKTOP and ADV presentation profiles
configuration
prototype scheduler settings
station.txt persistence
MiniShell Display/Input/Filesystem integration
MiniShell Audio API + deterministic WAV RX provider
clean explicit Ft8Engine lifecycle
clean FT8 monitor core
clean FT8 candidate + likelihood/LDPC/CRC core
per-engine Ft8HashStore
typed RX protocol message codec
caller-storage Ft8ProtocolSlot with exact-payload dedupe
```

Application I/O is modeled as three independent resources:

```text
RX Audio Path
TX Audio Path
Control Path
```

MiniFT8 requests Audio V1 transport as:

```text
12000 Hz
signed 16-bit PCM
2 channels
```

MiniShell preserves channel ordering but does not assign channel meaning. Ordinary audio versus I/Q is a MiniFT8 source/profile contract.

During the RX ownership-cleanup stages, `rx_frontend` adapts the MiniShell transport to the locked FT8-engine boundary:

```text
6000 Hz
mono float
960 samples per FT8 monitor block
```

This preserves proven MiniFT8-V2 decoder behavior. Changing the engine sample rate, FFT/OSR, filters, candidate policy, LDPC behavior, or SNR estimator is explicitly deferred to later algorithm work.

Examples of future source semantics:

```text
RX = QMX-AUDIO
    channel 0/1 = ordinary audio channels
    MiniFT8 selects/downmixes as needed

RX = QMX-IQ
    channel 0 = I
    channel 1 = Q
    MiniFT8 uses the I/Q DSP path
```

Hardware-native transport conversion stays below MiniShell; protocol-specific DSP, channel interpretation, and TX waveform synthesis stay inside MiniFT8.

Control remains a planned independent MiniShell service. Its eventual generic operations must not expose FT8 symbols or device-specific CAT syntax.

## RX ownership shape

RX-1B fixed five application-level modules beneath `app_controller`:

```text
app_controller
    |
    +-- rx_audio_adapter
    +-- rx_frontend
    +-- rx_slot_framer
    +-- ft8_engine
    `-- rx_result_builder
```

`app_controller` remains the only coordinator. `ft8_engine` owns the logical use of monitor/waterfall/candidate/decode/hash/protocol state but has no MiniShell, UI, storage, AutoSeq, TX, or platform dependency.

RX-1G now implements that ownership as one explicit pure engine:

```text
6 kHz mono float
    -> Ft8Engine
       -> Ft8Monitor
          compact waterfall
       -> FT8 candidate search
       -> likelihood extraction
       -> BP-LDPC
       -> CRC-14
       -> validated 10-byte payload
       -> typed protocol codec
          <-> Ft8HashStore
       -> exact-payload dedupe
    -> Ft8ProtocolSlot
```

The engine is caller-owned and uses caller-supplied/queryable DSP workspace. It owns the candidate array, persistent callsign-hash knowledge, and current slot/window state. `reset_stream()` clears monitor continuity while preserving protocol knowledge. Beginning a new completed-slot window owns the once-per-slot hash aging step.

Protocol type and parse status are separate; typed fields are authoritative and canonical text is derived convenience.

## RX memory rule

Normal RX is streaming:

```text
bounded PCM buffers
    -> bounded FFT workspace
    -> whole decode-window waterfall
    -> decode
```

A whole raw-audio slot is not required. Optional research modes may retain or double-buffer raw PCM so alternate algorithms can consume the same samples, but Cardputer-class operation must remain possible without that memory cost.

Locked rule:

> Stream raw audio; retain the waterfall; retain raw PCM only by explicit exception.

RX-1C made the monitor workspace measurable. On the Ubuntu x86-64 reference build the V2-compatible 6 kHz/time_osr=2/freq_osr=1 monitor requests:

```text
105808 bytes total
```

including an 80538-byte waterfall, FFT plan, window/history, and FFT scratch. This is queried rather than hard-coded; the exact 32-bit embedded total may differ slightly due to pointer/alignment size.

RX-1E preserves the V2 compact callsign-table entry:

```text
128 entries x 16 bytes = 2048 bytes
```

plus the small explicit store count/alignment field.

RX-1F deliberately does not hide a slot-sized allocation: `Ft8ProtocolSlot` uses caller-supplied `Ft8ProtocolMessage[]` storage with explicit capacity.

RX-1G exposes engine memory classes through `Ft8EngineRequirements`: monitor workspace/alignment plus fixed engine-instance, candidate-array, and hash-store storage. No new allocator-owned buffer was introduced.

## RX golden proofs

RX-1C reproduces the pinned RX-1A monitor boundary exactly:

```text
blocks       85
active bytes 73610
FNV-1a-64    18BE1E838FD9C6AF
```

RX-1D runs that same waterfall through the migrated candidate/LDPC/CRC path using the frozen `50 / 5 / 25` policy and reproduces exactly one unique valid payload:

```text
000000206016500A1988
```

Candidate score/order are diagnostics rather than permanent golden identity.

RX-1E adds deterministic unit coverage for the V2 production hash-table behavior: independent instances, 22/12/10-bit lookup, age refresh, trim-hole probing, same-full-hash replacement, and the 128 -> 78 -> 79 full-table policy.

RX-1F reproduces the five frozen RX-1A codec vectors exactly, including:

```text
CQ W1XYZ FN42
W6ABC AG6AQ R 1B SCV
K1ABC RR73; W9XYZ <KH1/KH7Z> -08
CQ PJ4/KA1ABC
CQ POTA W1XYZ
```

RX-1G closes the structural core with one pure end-to-end golden:

```text
pinned 6 kHz PCM
    -> Ft8Engine
    -> message_count = 1
       payload = 000000206016500A1988
       type    = STANDARD
       parse   = OK
       text    = CQ W1XYZ FN42
```

The dedicated `RX-1G Reference` workflow and the normal Linux suite pass on the RX-1G code-bearing head.

A first RX-1C refactor attempt changed the waterfall despite mathematically equivalent Hann-window multiplication. The hard golden caught it; restoring V2's exact float operation grouping restored byte identity. This remains a standing caution for later DSP cleanup.

## Run

Build MiniShell normally, then:

```text
M$> ft8                    # DESKTOP default
M$> ft8 --profile desktop
M$> ft8 --profile adv
```

`q` exits MiniFT8 and returns to:

```text
M$>
```

Presentation selection is launch policy. It is not persisted and is not inferred from `platform=linux` or `platform=adv`.

The current configuration file is:

```text
/flash/ft8/station.txt
```

Configuration saves use:

```text
write /flash/ft8/station.txt.tmp
sync + close
rename -> /flash/ft8/station.txt
```

through the MiniShell Filesystem API.

The current FT8-only configuration uses scalar protocol-local values such as:

```text
profile=
band=
skip_tx1=
max_retry=
```

There is no persisted protocol `mode=` field and no `presentation=` field inside `station.txt`.

The O-screen `Profile: Default` setting is a station/operating profile and is distinct from the ADV/DESKTOP presentation.

## Documentation

- `architecture.md` — ownership, dependency direction, Audio and RX/TX/Control boundaries.
- `rx.md` — canonical decode-RX pipeline, RAM rules, V2 classification, golden-reference policy, and staged RX development plan.
- `rx-1b-design.md` — locked RX-1B physical module boundaries, ownership, 6 kHz engine contract, lifecycle, workspace, error, and unit-test responsibilities.
- `rx-1c-monitor.md` — completed RX-1C monitor implementation, explicit workspace/lifecycle, memory measurement, golden proof, and float-regression lesson.
- `rx-1d-decoder.md` — completed RX-1D candidate search, likelihood/LDPC/CRC migration, cleaned payload boundary, and pinned payload proof.
- `rx-1e-hash-store.md` — completed RX-1E explicit callsign-hash ownership, V2 22/12/10-bit lookup semantics, aging, trim-hole behavior, compact storage, and unit proof.
- `rx-1f-message-codec.md` — completed RX-1F typed protocol representation, supported message unpacking, explicit hash-store integration, canonical text, and `Ft8ProtocolSlot` dedupe/storage contract.
- `rx-1g-engine.md` — completed RX-1G pure engine owner/lifecycle, memory classes, hash-aging ownership, stream-reset semantics, and 6 kHz PCM-to-typed-slot golden proof.
- `rx-decoder-contract.md` — RX-0B review of V2 `decode_helper.cpp` and the extracted decoder contract.
- `rx-v2-production-review.md` — RX-0B review of production `decode_monitor_results()`, with mixed V2 responsibilities assigned to V3 owners.
- `rx-monitor-review.md` — RX-0B review of `monitor.h/c`, DSP/workspace ownership, reset semantics, RAM requirements, and monitor-level golden strategy.
- `rx-decode-review.md` — RX-0B review of `decode.h/c`, candidate search, likelihood/LDPC/CRC boundaries, status cleanup, and deep-search extension points.
- `rx-message-review.md` — RX-0B review of `message.h/c`, typed protocol results, callsign-hash ownership, special-message handling, and codec gaps.
- `rx-golden.md` — RX-1A pinned V2 golden boundaries: reference WAVs, exact Linux waterfall fingerprints, payload/codec vectors, and known V2 gaps that are not golden targets.
- `v1-validation.md` — P1/P2/V1 cross-backend/profile validation record.
- `ui.md` — presentation geometry, UI model, and controls.
- `development.md` — current development gate and next task.

## Source

Current implemented application source includes:

```text
apps/ft8/
├── main/                  MiniShell application edge/adapters
├── include/ft8/           shared application types
└── src/
    ├── app_controller/
    ├── config_service/
    ├── ft8_engine/
    │   ├── README.md
    │   ├── ft8_engine.[ch]
    │   ├── ft8_monitor.[ch]
    │   ├── ft8_decoder.[ch]
    │   ├── ft8_ldpc.[ch]
    │   ├── ft8_crc.[ch]
    │   ├── ft8_hash_store.[ch]
    │   ├── ft8_message_codec.[ch]
    │   `-- vendor/kissfft/
    ├── presentation_profile/
    ├── qso_scheduler/
    ├── storage_service/
    `-- ui_shell/
```

RX-1B reserves the other ownership modules as implementation begins:

```text
    ├── rx_audio_adapter/
    ├── rx_frontend/
    ├── rx_slot_framer/
    `-- rx_result_builder/
```

Do not create placeholder modules merely to mirror the design document. Add each source module when its implementation/test stage begins.

As functionality grows, new modules should be introduced only when their ownership and interfaces are clear. A logical owner may use several small private implementation files; one-owner does not mean one giant source file.
