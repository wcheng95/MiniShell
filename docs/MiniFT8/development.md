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
portable AArch64 host    rpi3-2 + QMX RX/CAT/TX validated
embedded deployment      ADV QMX RX/TX operational and hardware accepted
```

The ADV RAM/USB-host feasibility risk is retired by T017. Linux/pc-1 + QMX now
has an accepted physical FT8 TX baseline through T022-T027: real CAT keying,
on-air decodability, RX recovery, RxTxLog, CQ/POTA beacon operation,
V2-compatible offset-source behavior, and a completed two-way QSO on
2026-09-18 UTC. WinBook/TW700 portability is validated with the pc-1-built
binaries: QMX ALSA RX/decode and QMX CDC CAT/TX both work. The login user must
have normal `dialout` access to the CDC tty. ALSA card and tty numbering may
change across boots; stable QMX endpoint discovery remains deferred.

`rpi3-2` provides the current Linux/AArch64 portability reference. It was built
natively on Raspberry Pi 3 AArch64 Linux and validated end-to-end with QMX:
MiniShell runtime, portable shared-object apps, MiniFT8 live ALSA RX/decode,
Serial/CDC CAT band control, and physical TX/RX all pass. Fresh-host prerequisites
were `cmake`, `build-essential`, and `libasound2-dev`; the ALSA development header
must be installed before the native build because Linux ALSA support is selected
at compile time. Preserve this bring-up recipe/evidence for future CardputerZero
work.

## V3.089 accepted baseline

T089 is complete. V3.089 adds the optional W/w RX top-row waterfall through the
generic MiniShell RGB565 raster capability while preserving the accepted V3.088
capture/decode/UAC/CAT/TX policy. ADV hardware validation confirms the final
dirty-row ownership model eliminates both UTC-second and RX-batch row-0 flashes.
Linux remains text-only. The history is optional/session-local, about 4.5 KiB,
and reuses existing monitor FFT output with no second FFT.

## Current production baseline

```text
RX core / protocol decode          COMPLETE
MiniShell Audio RX                 COMPLETE
QMX live ALSA capture              COMPLETE
ADV QMX USB-host UAC live RX       COMPLETE — hardware validated
V2 12.64 s decode cadence          COMPLETE
continuous multi-slot live RX      COMPLETE
AutoSeq AS-0..AS-8                 COMPLETE
simulation/test TX lifecycle        COMPLETE
ADIF persistent logging            COMPLETE
Field Day Cabrillo logging         COMPLETE
physical QMX TX                    COMPLETE — pc-1/QMX and ADV/QMX hardware validated
CQ/POTA beacon controls             COMPLETE — T023 hardware validated
Random/Fixed/RX TX offset           COMPLETE — T024 hardware validated
WinBook/TW700 live RX               PASS — pc-1 binaries + QMX ALSA decode
WinBook/TW700 QMX CAT/TX            PASS — requires user membership in dialout
rpi3-2 AArch64 QMX RX/CAT/TX        PASS — native build, live decode + physical TX
first real two-way QSO               COMPLETE — Linux/QMX, 2026-09-18 UTC
T026 RR73 responder compatibility    COMPLETE — temporary V2 keyword-before-grid rule
T027 non-standard/hash TX             COMPLETE — V2-compatible hashed QSO + type-4 CQ
T028 RX display/selection ordering     COMPLETE — live validated
T029 RX display lifetime                COMPLETE — live validated
T031 live QMX band CAT sync             COMPLETE — 1 s debounce, hardware validated
T032 V -> 3 current-day QSO view        COMPLETE — compact ADIF pages, hardware validated
T049 color status                        COMPLETE — ADV TX bar + RX row colors
T050 RX/TX bare page shortcuts           COMPLETE — `;` previous / `.` next
T083 CAT fault containment                COMPLETE — sustained ADV/QMX validation
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
UTC live scheduling + sample-count block framing
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

T088 restores the V2-style slot-local live window while preserving T081's useful
capture/decode lifecycle decoupling. At each UTC boundary the current slot resets
and owns the producer/framer/FFT/waterfall state. Exactly 79 x 960 samples are
accepted into the waterfall from UTC +0.00 through +12.64 s; decode is submitted
at +12.64 s. From +12.64 through the next UTC boundary, physical Audio acquisition
continues but MiniFT8 drains/discards those samples instead of extending the
waterfall.

ADV retains one core-1 decoder and one completed-result message buffer. Decode may
cross the following UTC boundary while the next slot begins capture independently.
If an old decode is still RUNNING at the following slot's own +12.64 s trigger,
core 0 requests cancellation and leaves engine mutation to the core-1 owner; the
new slot decode starts as soon as cancellation completes. An unconsumed stale
READY result is dropped. These are contained performance overruns, not fatal RX
invariants. Live discontinuities still do not move UTC capture/decode deadlines.

The former -1.60 s preroll/93-block live schedule is no longer used. Candidate
search still accepts V2-compatible negative time offsets by ignoring unavailable
pre-slot rows and scoring later Costas rows. The controller supplies UTC; the
engine remains clock-free. Physical TX pause/resume remains separate.

Bounded ADV diagnostics (`FT8_DECODE_DIAGNOSTICS`) identify `CAPTURE_RESET`,
`DECODE_START`, `DECODE_DONE`, `RESULT_PUBLISH`, and contained `OVERRUN`
events by slot.

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
T082        COMPLETE — RX page reset on new batch + configurable cqtypes list
T083        COMPLETE — CAT fault containment + sole-owner CDC transport
T024        COMPLETE — Random/Fixed/RX TX-offset source
T026        COMPLETE — temporary GRID-coded RR73 -> TX4 compatibility fix
T027        COMPLETE — V2-compatible non-standard/hash TX + type-4 plain CQ
T028        COMPLETE — reply-to-me/CQ/regular RX order + descending SNR
T029        COMPLETE — preserve RX rows through TX; clear at TX completion
T031        COMPLETE — live O -> 3 QMX band sync with 1 s final-selection debounce
T032        COMPLETE — V -> 3 current-day compact QSO view

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
75840 samples = 79 blocks in the live decode window
UTC +0.00 s  reset/begin slot-local capture
UTC +12.64 s submit decode and stop feeding this slot's waterfall
12.64..15.00 s continuously drain/discard Audio until the next UTC boundary
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

T031 extends that accepted boundary to runtime band changes: O -> 3 updates the
MiniFT8 band immediately, then an already-connected QMX synchronizes to the final
selection after a 1-second debounce. Rapid stepping coalesces intermediate bands;
hardware validation passed on 2026-09-20. The controller consumes slot progression
while retune is pending so physical TX cannot start on a stale frequency or as a
late catch-up after the CAT write.

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
behavior. Exact payload and tone output is checked against 36 fixed vectors
generated independently from the pinned MiniFT8-V2 encoder source; the original
25 T021 vectors remain unchanged.

Current plan constants:

```text
symbols       79
symbol time   160 ms
tone spacing  6.25 Hz
tone range    0..7
```

Standard calls, current CQ variants, free text, Field Day TX2/TX3, and T027
non-standard/hash TX are supported. Directed compound calls such as `W1AW/9`
remain STANDARD FT8 messages with V2-compatible 22-bit hash packing; plain CQ
from a non-standard local call uses type-4 and therefore omits the grid.
Modified non-standard local CQ forms (SOTA/POTA/QRP/FD) remain explicitly
unsupported rather than silently losing their modifier.

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

T086 restores V2 best-effort append/close durability for RT records: no explicit
per-record Filesystem sync is required or called. Recent buffered records may
be lost on process/power loss. ADIF and Cabrillo retain sync-before-rename
commits. T086 ADV/QMX hardware acceptance remains pending.

## RX display ordering

T028 makes RX ordering an app_controller-owned presentation/selection policy
without reordering the factual RxBatch.

```text
reply-to-me   strongest -> weakest
CQ            strongest -> weakest
regular       strongest -> weakest
```

Equal-SNR entries retain original decode order. UiModel uses the controller's
display-index map, and manual selection maps back to the original factual
RxMessage. Automatic addressed-message processing and RT logging continue in
raw batch order. Live validation passed. T049 adds presentation-only row coloring from the same
factual categories: reply-to-me red, CQ green, regular white.

T029 defines display lifetime independently from RX transport resets: the most
recent decoded/sorted RX rows remain visible and selectable through a following TX
slot and ordinary audio/framer reset events. Successful TX completion/RX resume
clears those prior-slot rows because the just-finished slot had no RX decode. A
new completed RX batch replaces the old display; an empty completed batch clears
it. Live Linux/QMX validation passed.

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
T027 non-standard/hash TX, T028 RX display ordering, T029 RX display lifetime, T031 live QMX band CAT synchronization, T032 V -> 3 current-day QSO display, and T081 RX lifecycle decoupling are complete.

Deferred items:

### Active-band field follow-up — V2 fidelity decisions

A busy-band ADV/QMX field test produced many healthy slots, including more than
13 decoded messages in a slot, but also intermittent slots with zero decoded
messages despite clearly active FT8 signals. Long decode execution extending into
the following slot is not itself considered a failure; the zero-decode slots are
the correctness symptom to investigate.

A pinned-V2 versus current-V3 audit confirmed that the core FT8 decode mathematics
are already substantially V2-equivalent: 6 kHz engine rate, 200-2900 Hz analysis,
time_osr=2, freq_osr=1, 50 candidates, minimum sync score 5, strongest-first
candidate ordering, BP-LDPC, and 25 maximum LDPC iterations. The remaining
differences are primarily runtime RX/decode scheduling and surrounding I/O policy.

The architect has approved the following future fix/improvement work:

1. **Restore V2 decoder scheduling — APPROVED.**
   T087 keeps the ADV decoder on core 1 with its static 8192-byte stack and
   restores FreeRTOS priority 4. Active work loops without deliberate yielding;
   the idle worker retains a one-tick delay. Stop/abort and diagnostics remain
   unchanged. Decoder math, candidate limits/order, capture/FFT timing and
   V3/MiniShell ownership boundaries are preserved. ADV/QMX timing and normal
   RX acceptance remain pending.

2. **Restore V2 RxTxLog durability policy — APPROVED.**
   V2 RX/TX trace append uses normal append/close without per-record flash sync.
   T086 restores open/write/close without an explicit per-record sync in V3.
   This removes the extra sync from optional traffic logging during RX work.
   This decision applies to RxTxLog; it does not
   weaken the separate ADIF/Cabrillo QSO-commit durability contract.

3. **Restore V2 block normalization — POSTPONED.**
   V2 scales each 960-sample FFT input block toward a bounded nominal level before
   monitor processing. V3 does not currently do this. Do not implement it yet;
   retain it as a V2-fidelity option after the RX/FFT path is reviewed.

These decisions are now promoted into bounded follow-up work:

```text
T086  TESTING — V2 RxTxLog append/close policy; ADV acceptance pending
T087  TESTING — V2 ADV priority/tight scheduling; hardware acceptance pending
T088  COMPLETE — V3.088 decode baseline; field test passed with no zero-decode slots
T089  COMPLETE — V3.089 accepted baseline; ADV waterfall hardware accepted
```

The architect promoted T088 directly so the complete repair can be field-tested
as one V3.088 binary. Zero-decode on an active band remains the primary acceptance
blocker; T086/T087/T088 should be evaluated together in that field test.

V3.089 is the accepted MiniFT8 baseline. It preserves the V3.088 ADV/QMX
decode behavior that passed bench and field validation, including the 2026-10-16
active-band test with no zero-decode slots, and adds the hardware-accepted T089
RX waterfall. Future work returns to code reading and small, measured incremental
improvements rather than another RX repair campaign.

#### RX/FFT direction agreed after the V2 audit

The preferred direction restores the simple V2 slot-local capture window while
keeping V3's lifecycle decoupling:

```text
UTC +0.00 s
    new slot owns capture
    reset slot-local audio/framer/FFT/waterfall state

0.00 .. 12.64 s
    consume 79 x 960 samples at 6 kHz
    capture + FFT/waterfall have priority over optional application work

UTC +12.64 s
    waterfall is decode-ready
    submit decode on core 1

12.64 .. 15.00 s
    decoder normally finishes inside the remaining 2.36 s
    transport acquisition may continue/drain independently

next UTC boundary
    next slot takes capture ownership immediately
    reset slot-local capture/waterfall state regardless of prior decode state
```

Do not restore the V3 -1.60 s preroll/continuous-waterfall schedule unless later
evidence requires it. Candidate search may retain V2-compatible negative time
offset handling with unavailable pre-slot rows, as V2 did.

Physical QMX UAC acquisition remains continuous below the MiniShell Audio API.
"Reset at the slot boundary" means reset/discard the slot-local consumer state;
do not tear down or physically restart USB/UAC. Prefer continuously draining or
explicitly discarding non-capture-period audio so stale ring backlog cannot be
mistaken for the next slot's first samples.

A late previous decode is subordinate to the new slot. Crossing the UTC boundary
must not delay or invalidate new-slot capture. If a prior decode is still RUNNING
or has an unconsumed result at the following slot's decode trigger, treat it as a
stale performance overrun to cancel/drop in favor of the current slot rather than
terminating the FT8 application. The exact cancellation/data-handoff mechanism is
still an implementation detail; do not add extra waterfall buffering unless
measurement shows it is necessary.

### Other deferred non-critical blocking/performance leads

The post-T083 audit also identified the following future diagnostic/performance
leads:

- ADIF/Cabrillo commits and current-day QSO view reads are synchronous filesystem
  work on the FT8 task.
- ADV diagnostic output is serialized through a shared output lock/UART path used
  by both core-0 FT8 and the core-1 decode worker.
- Some failure-cleanup joins retain unbounded post-timeout suspension waits.

Prefer local best-effort recovery for discontinuities/skips and preserve strict
blocking only for unknown RF/TX state or genuine internal invariants.


```text
RR73 ambiguity
    T026 temporarily gives exact "RR73" terminal semantics precedence over
    GRID syntax, matching V2. A real locator RR73 is therefore ambiguous and
    needs a permanent design later.

Linux QMX discovery
    ALSA card numbers (for example hw:2,0 vs hw:1,0) and ttyACM numbers are
    enumeration details. A future MiniShell Linux provider should offer stable
    QMX-oriented endpoints rather than making MiniFT8 discover /dev or ALSA.

ADV QMX USB-host reuse
    Physical TX is now operational on ADV. The remaining portability issue is
    USB session lifetime: exiting the final QMX user tears down the host and
    class drivers, and a second fresh QMX enumeration can fail until QMX is
    power-cycled. A future resident/reusable QMX session should preserve the
    existing AutoSeq/TX encoding boundary.

Shelved n28a priority / speculative decode experiment
    This is not current V3 work. Revisit only if field use demonstrates a
    decode problem that justifies the added complexity.

    The existing sync search still produces the normal 50 physical candidates.
    Do not use signal strength as an LDPC scheduling priority. Instead, exploit
    the systematic LDPC(174,91) layout: score the preserved n28a field of each
    candidate against the known CQ encoding and the local mycall encoding.

    A possible two-round policy is:

        search -> 50 candidates
        n28a semantic scoring -> highest 19 -> round 1
        remaining 31 -> earliest time_sub first -> round 2

    Round 1 first uses the ordinary LDPC/CRC/message path. Only when ordinary
    LDPC fails and the mycall n28a score is at or above an experimental
    threshold would a constrained/speculative reply-to-me decode be attempted.
    The initial threshold discussed for an experiment is 18/28. The score only
    nominates a recovery attempt; normal LDPC/CRC/protocol/QSO validity remains
    authoritative. CQ participates in round-1 priority but does not trigger
    speculative decoding.

    If this experiment is ever promoted, keep the first implementation small:
    CQ + mycall only; defer mycall_hash22/compound-call handling; keep the 19/31
    split fixed; make only the speculative mycall threshold runtime-tunable in
    /flash/ft8/setting.txt; and expose only a V-screen speculative_success
    counter for initial evaluation. No message marker or RxTxLog format change
    is required.

    Tuning should be empirical. Start at 18/28; if useful field testing produces
    no speculative_success, lower the threshold gradually until a genuine
    recovery appears or the idea proves unhelpful. A success means ordinary
    LDPC failed but the speculative path recovered a valid reply. If no
    measurable recovery benefit appears, abandon the experiment rather than
    preserve its complexity.
```

Until one of these is promoted to a bounded task, the accepted production
baseline is Linux/QMX physical RX/TX plus ADV/QMX operational RX/TX.