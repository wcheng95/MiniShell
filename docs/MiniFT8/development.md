# MiniFT8-V3 Development

MiniFT8 is a portable MiniShell FT8 application. MiniFT8-V2 is the behavioral reference for preserved behavior; V3 uses MiniShell APIs and V3 ownership boundaries rather than copying V2 structure wholesale.

Pinned V2 reference:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

## Current development rule

```text
backend       Linux first
presentation  ADV when UI behavior matters
```

Stay on Linux until work requires an embedded-only dependency. Preserve V2 behavior first, then introduce deliberate V3 differences explicitly.

## Current production baseline

```text
RX core / protocol decode          COMPLETE
MiniShell Audio RX                 COMPLETE
QMX live ALSA capture              COMPLETE
V2 12.64 s decode cadence          COMPLETE
continuous multi-slot live RX      COMPLETE
AutoSeq AS-0..AS-8                 COMPLETE
simulated TX lifecycle             COMPLETE
ADIF persistent logging            COMPLETE
Field Day Cabrillo logging         COMPLETE
physical QMX TX                    NEXT MAJOR BOUNDARY
```

Working live Linux/QMX command:

```text
M$> ft8 --profile adv --rx alsa:hw:2,0
```

## Validated production path

```text
QMX USB-UAC
48 kHz / S24_3LE / stereo
        |
        v
Linux ALSA capture worker
        |
        v
canonical MiniShell Audio ring
12 kHz / S16 / stereo
        |
        v
rx_audio_adapter
        |
        v
RxFrontend
6 kHz mono float
        |
        v
RxSlotFramer
960-sample blocks
UTC initial phase + sample-count progression
        |
        v
Ft8Engine
waterfall / candidate / LDPC / CRC / codec / hashes
        |
        v
RxResultBuilder
        |
        v
RxBatch
        |
        v
app_controller
        |
        +-> AutoSeq
        |      +-> QsoView -> UiModel -> T UIScreen
        |      `-> TxIntent / log eligibility
        |
        +-> log_service -> MiniShell FS/Time
        `-> simulated TX lifecycle
```

`app_controller` remains the sole production coordinator.

## Live RX lessons now locked as architecture

### 1. ALSA capture must explicitly start

Linux ALSA capture is opened/configured, prepared, then explicitly started before waiting/reading. A PREPARED stream is not treated as a running stream.

### 2. Capture must not stop during decode

FT8 decode is synchronous. The Linux provider therefore continuously drains ALSA on a worker thread into a canonical-audio ring buffer. MiniFT8 reads from that buffer.

This preserves the V2 property that transport acquisition continues while candidate/LDPC work runs.

### 3. Decode at 79 symbols, not the end of the 15 s slot

At 6 kHz:

```text
79 * 960 = 75840 samples = 12.64 s
```

V2-compatible live behavior:

```text
0.00 s     begin slot/waterfall
12.64 s    decode current waterfall
            reset waterfall count only
            preserve FFT history
15.00 s    discard incomplete 720-sample tail block
            begin next slot
```

The first partial slot after stream start/discontinuity is discarded. After timing is established, sample count owns progression.

## Stage status

```text
A0-A3       COMPLETE
P1-P2       COMPLETE
V1          COMPLETE

RX-0        COMPLETE — V2 review
RX-1A..1G   COMPLETE — monitor/decoder/hash/codec/engine
RX-2        IMPLEMENTED — host decoder
RX-3        COMPLETE — frontend
RX-4        COMPLETE — slot framer
RX-5        COMPLETE — pure RX assembly
RX-6        COMPLETE — MiniShell Audio + WAV
RX-7        COMPLETE — decoded application/UI path
RX-8        COMPLETE — live QMX ALSA + V2 timing + continuous capture

AS-0..AS-8  COMPLETE — compact V2-equivalent AutoSeq structural port
LOG-1       COMPLETE — V2 ADIF + Field Day Cabrillo through MiniShell APIs
```

## RX timing contract

Transport:

```text
12000 Hz
signed 16-bit PCM
2 channels
```

Engine:

```text
6000 Hz
mono float
960 samples/block
```

Slot:

```text
90000 total 6 kHz samples
75840 samples to V2 decode-ready point
93 complete 960-sample blocks in full slot
720 slot-end samples discarded
```

`Ft8Engine` does not own or read a clock.

## AutoSeq ownership

AutoSeq is pure policy/state:

```text
fixed 30-entry active/inactive queue
QSO progression
retry limits/counters
priority ordering
same-parity queue rotation
inactive parking/reactivation
CQ / FreeText / Field Day behavior
logging eligibility
```

It does not call MiniShell, filesystem, time, Audio, DSP, UI, or platform code. No AutoSeq heap allocation is permitted.

Normal next-TX semantics are derived from state:

```text
REPLYING       -> TX1
REPORT         -> TX2
ROGER_REPORT   -> TX3
ROGERS         -> TX4
SIGNOFF        -> TX5
```

## Logging contract

The controller coordinates logging at TX start. Pure AutoSeq owns eligibility/events and per-format ACK state; `log_service` owns serialization, date/frequency/path policy and copy-on-write persistence through injected MiniShell Filesystem and Time/Location APIs.

```text
AutoSeq
    -> AutoSeqLogEvent at eligible TX start
    -> app_controller passes station/QSO facts to log_service
    -> log_service commits files through MiniShell FS
    -> app_controller ACKs only successful writes
```

### ADIF

```text
/flash/ft8/YYYYMMDD.txt
```

V2 details retained:

```text
FT8 mode
UTC timestamp
selected FT8 dial frequency
station callsign
station grid truncated to 4 characters
unknown -99 reports omitted
```

V3 currently has no V2 comment/radio-macro configuration; the comment is therefore empty.

### Field Day Cabrillo

```text
/flash/ft8/fieldday.txt
```

The V2 ARRL Field Day header and `QSO:` line format are preserved. QSO lines are inserted before `END-OF-LOG:`.

### Optional RT log

V2's `RTYYMMDD.txt` RX/TX trace is controlled by `rxtx_log`. V3 does not yet expose that setting, so it remains intentionally unported rather than becoming silently always-on.

## UI contract

ADV remains the canonical compact presentation:

```text
20 x 7 text
```

Generic UIScreen rules:

1. Top level means entered a UIScreen but not one of its submenus.
2. Existing Back/ESC behavior remains unchanged.
3. Switching UIScreens always enters the destination at top level.
4. Page navigation wraps.
5. UIScreen selection and TX/RX state are independent.

See `ui.md` for the exact top-line format and screen definitions.

## Current source ownership

```text
apps/ft8/
├── main/
└── src/
    ├── app_controller/
    ├── auto_seq/
    ├── config_service/
    ├── presentation_profile/
    ├── storage_service/
    ├── tx_lifecycle/
    ├── ui_shell/
    ├── ft8_engine/
    ├── rx_audio_adapter/
    ├── rx_frontend/
    ├── rx_slot_framer/
    └── rx_result_builder/
```

The old `qso_scheduler` prototype has been removed; AutoSeq is the single QSO-state owner.

## Regression anchors

Important current proofs:

```text
known-good canonical WAV decodes through production app
QMX arecord -> SoX canonical WAV decodes
MiniShell audio_probe shows ~12 kHz live QMX canonical stream
live QMX MiniFT8 decodes consecutive slots
Linux and ADV presentation tests remain part of CTest/CI
```

The original hard structural RX anchors and detailed stage proofs remain documented in the RX stage files.

## Document hierarchy

Current truth:

```text
README.md       current MiniFT8 status/contracts
development.md  current engineering baseline and next boundary
ui.md           current UI contract
architecture.md current ownership/dependency architecture
```

Detailed `rx-*` and `as-*` documents are historical implementation records and regression rationale. When their old planning language conflicts with the current baseline, the four documents above take precedence.

## Next major boundary

Physical TX should reuse the already-stable semantic pipeline rather than bypassing it:

```text
AutoSeq TxIntent
        |
        v
app_controller
        |
        +-> MiniShell Control / CAT
        `-> MiniShell Audio TX
                |
                v
              QMX
```

Keep the current UTC slot/parity gate, logging trigger, retry progression, and V2 behavior while replacing simulated completion with real transmitter lifecycle evidence.