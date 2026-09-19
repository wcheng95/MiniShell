# MiniFT8-V3 Development

MiniFT8 is a portable MiniShell FT8 application. MiniFT8-V2 is the behavioral reference for preserved behavior; V3 uses MiniShell APIs and V3 ownership boundaries rather than copying V2 structure wholesale.

Pinned V2 reference:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

## Current development rule

```text
production TX baseline   Linux/pc-1 + QMX accepted
portable Linux host      WinBook/TW700 + QMX RX/CAT/TX validated
embedded deployment      ADV live RX validated; physical TX remains future
```

The ADV RAM/USB-host feasibility risk is retired by T017. Linux/pc-1 + QMX now
has an accepted physical FT8 TX baseline through T022-T026: real CAT keying,
on-air decodability, RX recovery, RxTxLog, CQ/POTA beacon operation,
V2-compatible offset-source behavior, and a completed two-way QSO on
2026-09-18 UTC. WinBook/TW700 portability is validated with the pc-1-built
binaries: QMX ALSA RX/decode and QMX CDC CAT/TX both work. The login user must
have normal `dialout` access to the CDC tty. ALSA card and tty numbering may
change across boots; stable QMX endpoint discovery remains deferred.

## Current production baseline

```text
RX core / protocol decode          COMPLETE
MiniShell Audio RX                 COMPLETE
QMX live ALSA capture              COMPLETE
ADV QMX USB-host UAC live RX       COMPLETE — hardware validated
V2 12.64 s decode cadence          COMPLETE
continuous multi-slot live RX      COMPLETE
AutoSeq AS-0..AS-8                 COMPLETE
simulated TX lifecycle             COMPLETE
ADIF persistent logging            COMPLETE
Field Day Cabrillo logging         COMPLETE
physical QMX TX                    COMPLETE — pc-1/QMX hardware validated
CQ/POTA beacon controls             COMPLETE — T023 hardware validated
Random/Fixed/RX TX offset           COMPLETE — T024 hardware validated
WinBook/TW700 live RX               PASS — pc-1 binaries + QMX ALSA decode
WinBook/TW700 QMX CAT/TX            PASS — requires user membership in dialout
first real two-way QSO               COMPLETE — Linux/QMX, 2026-09-18 UTC
T026 RR73 responder compatibility    COMPLETE — temporary V2 keyword-before-grid rule
```

Working live Linux/QMX command:

```text
M$> ft8
```

T018 makes the Linux composition defaults `ADV` presentation plus
`alsa:hw:2,0` RX. Explicit presentation and RX overrides remain available and
have been validated on pc-1.

## Validated production path

```text
QMX USB-UAC
48 kHz / S24_3LE / stereo
        |
        +--> Linux ALSA capture worker
        |
        `--> ADV ESP-IDF USB-host UAC worker
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
        `-> Ft8TxPlan -> slot-anchored physical QMX CAT TX lifecycle
```

`app_controller` remains the sole production coordinator. The ADV path has now decoded real on-air FT8 on Cardputer ADV at 240 MHz using `time_osr=2, freq_osr=1`.

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
T017        COMPLETE — ADV QMX USB-host RX + lifecycle + post-FT8 usbmsc
T018        COMPLETE — Linux bare ft8 live-QMX/ADV defaults
T019        COMPLETE — Linux Serial/CDC + receive-safe QMX CAT sync
T020        COMPLETE — QMX CAT TX primitives; RF validated through T022-T024
T021        COMPLETE — pure FT8 TX encoder + immutable 79-tone plan
T022        COMPLETE — integrated physical QMX FT8 TX + RX recovery + RxTxLog
T023        COMPLETE — CQ/CQ POTA + beacon OFF/EVEN/ODD
T024        COMPLETE — Random/Fixed/RX TX-offset source
T026        COMPLETE — temporary GRID-coded RR73 -> TX4 compatibility fix

AS-0..AS-8  COMPLETE — compact V2-equivalent AutoSeq structural port
LOG-1       COMPLETE — V2 ADIF + Field Day Cabrillo through MiniShell APIs
```

## ADV RX hardware result

The main embedded-RX risk is retired: Cardputer ADV can host QMX over USB, convert
native UAC audio below the MiniShell API, and decode real FT8 messages through the
unchanged V3 RX pipeline.

Validated production profile:

```text
ESP32-S3 CPU   240 MHz
time_osr       2
freq_osr       1
```

Live memory at `freq_osr=1` is approximately 142.2 KiB free / 82.0 KiB largest
block with 129.2 KiB attributed to the FT8 application. A temporary
`freq_osr=2` test remained alive but increased application allocation to about
232.2 KiB, leaving 58.8 KiB free / 31.0 KiB largest. No messages were observed
during that short comparison, but decode duration/candidate load were not measured;
the extra CPU cost may be material on ESP32-S3. Therefore the result is inconclusive
for decode quality/performance and `freq_osr=1` remains the accepted ADV profile.

T017 hardware acceptance is complete: initial disconnected startup + late first
attach, consecutive live decode slots, repeated entry/exit, provider continuity/ring
statistics, and `usbmsc` after USB-host teardown all pass. Post-session QMX
unplug/replug recovery is not required because real QMX hardware can fail a second
enumeration, matching the practical V2 limitation.

### Deferred freq_osr=2 performance work

The `freq_osr=2` experiment established memory feasibility but did not measure
decoder execution time. It approximately doubles the monitor-frequency workspace
dimension and materially increases both RAM and compute. The architect estimates
ESP32-S3 decode may take roughly 3-4 seconds at that setting.

Any revisit should measure at minimum:

```text
decode wall time
candidate count / LDPC work
ring high-water while decode is synchronous
overflow/discontinuity count
heap free / largest block
number/quality of decoded signals versus freq_osr=1
```

This is intentionally outside T017.

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

## Linux CAT baseline

T019 proves the CAT ownership split on real pc-1/QMX hardware:

```text
app_controller
    -> MiniFT8 radio_control / radio_qmx
       owns QMX CAT semantics
    -> MiniShell Serial/CDC
       owns raw byte transport/lifecycle
    -> QMX USB CDC
```

Receive-safe startup commands:

```text
MD6;
FR0;
FT0;
FA%011u;
```

The selected-band dial frequency is now a single MiniFT8-owned source used by CAT
and logging. Real hardware validation confirms frequency/mode/VFO synchronization,
continued FT8 RX decode, no RF keying, and clean repeated CDC close/reopen.

No transmit CAT commands are part of T019.

## CAT TX primitive baseline

T020 adds the MiniFT8-owned QMX transmit control primitives:

```text
begin TX    MD6; TX;
tone        TA%04d.%02d;
end TX      RX;
```

The V2 floor/round/clamp tone formatting behavior is preserved, and cleanup
tracks uncertain TX attempts conservatively so Serial close first makes a
best-effort `RX;` restoration.

The standalone 1500 Hz RF tone test was intentionally skipped at T020. Its
hardware validation was subsequently supplied by T022-T024: real QMX keying,
per-symbol tone control, RX restoration, post-TX receive recovery and on-air
FT8 decodability all passed.

## FT8 TX encoder baseline

T021 completes the pure FT8 transmit-plan layer:

```text
AutoSeqTxIntent
    -> canonical FT8 TX text
    -> 77-bit payload
    -> CRC-14 + LDPC(174,91)
    -> 79 Gray/Costas tone indices
    -> Ft8TxPlan
```

The implementation is platform-free, heap-free, and contains no CAT/radio/clock
behavior. Exact payload and tone output is checked against 25 fixed vectors
generated independently from the pinned MiniFT8-V2 encoder source.

Current plan constants:

```text
symbols       79
symbol time   160 ms
tone spacing  6.25 Hz
tone range    0..7
```

Standard calls, current CQ variants, free text, and Field Day TX2/TX3 are
supported. Nonstandard/hashed-call TX remains intentionally unsupported for the
first-QSO path.

T022 completed physical scheduling/integration: slot-anchored CAT tone updates,
T020 RF validation, real QMX RX restoration, and V2-compatible RxTxLog. The
first completed Linux/QMX two-way QSO was subsequently captured in the production
RT trace.

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

### RxTxLog / RT trace

V2-compatible `RT[YYMMDD].txt` RX/TX trace is implemented in V3 and hardware validated.

It is part of the accepted T022 physical-TX baseline and provides RX/TX evidence for slot timing, transmitted text, reports and frequency offsets.

The accepted implementation preserves the V2-compatible line semantics:

```text
T [YYYYMMDD HHMMSS][freq_MHz] <text> <offset_hz>
R [YYYYMMDD HHMMSS][freq_MHz] <text> <snr_db> <offset_hz>
```

using daily `RT[YYMMDD].txt` storage. The generated RT trace becomes part of the
T022 hardware acceptance evidence.

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
live QMX MiniFT8 decodes consecutive slots on Linux
live Cardputer ADV/QMX USB-host MiniFT8 decodes real on-air messages at 240 MHz
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

## Deferred follow-up boundaries

The first complete Linux/QMX QSO and the physical transmitter lifecycle are done.
No new task is active after T026.

Deferred items:

```text
RR73 ambiguity
    T026 temporarily gives exact "RR73" terminal semantics precedence over
    GRID syntax, matching V2. A real locator RR73 is therefore ambiguous and
    needs a permanent design later.

Linux QMX discovery
    ALSA card numbers (for example hw:2,0 vs hw:1,0) and ttyACM numbers are
    enumeration details. A future MiniShell Linux provider should offer stable
    QMX-oriented endpoints rather than making MiniFT8 discover /dev or ALSA.

ADV physical TX
    Reuse the proven Linux semantic/CAT boundary when embedded TX becomes an
    active goal; do not redesign AutoSeq or TX encoding for ADV.
```

Until one of these is promoted to a bounded task, the accepted production
baseline is Linux/QMX physical RX/TX plus ADV/QMX live RX.