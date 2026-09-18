# MiniFT8-V3 Development

MiniFT8 is a portable MiniShell FT8 application. MiniFT8-V2 is the behavioral reference for preserved behavior; V3 uses MiniShell APIs and V3 ownership boundaries rather than copying V2 structure wholesale.

Pinned V2 reference:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

## Current development rule

```text
completion / first QSO   Linux/QMX
embedded validation      ADV only when hardware-specific behavior matters
```

The ADV RAM/USB-host feasibility risk is retired by T017. MiniFT8 now returns to
Linux to finish physical TX and complete the first real MiniFT8-V3 QSO. Keep Linux
as the deterministic development/regression platform until a completed TX path is
ready to be carried back to ADV. Preserve V2 behavior first, then introduce
deliberate V3 differences explicitly.

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
physical QMX TX                    NEXT MAJOR BOUNDARY — Linux first QSO
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
        `-> simulated TX lifecycle
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

## Next major boundary

T017 is complete; no RX redesign is planned. The active goal is now the **first
MiniFT8-V3 QSO on Linux/QMX**. Physical TX should reuse the already-stable semantic
pipeline rather than bypassing it:

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

Keep the current UTC slot/parity gate, logging trigger, retry progression, and V2
behavior while replacing simulated completion with real transmitter lifecycle
evidence. Do not spend the next phase optimizing ADV-specific RAM or TX mechanics;
finish and validate the complete QSO path on Linux first, then port the proven TX
boundary back to ADV.