# MiniFT8-V3

MiniFT8-V3 is the portable FT8 application hosted by MiniShell. Its runtime application name is:

```text
ft8
```

MiniFT8-V2 remains the behavioral reference for preserved FT8/QSO behavior:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

V3 keeps V2 behavior where practical, but all platform access goes through the MiniShell public API.

## Current status

Both production receive paths are now proven: Linux/QMX runs continuously across consecutive FT8 slots, and Cardputer ADV/QMX USB-host RX decodes real on-air FT8 at 240 MHz.

```text
RX core + protocol decode        COMPLETE
MiniShell Audio integration      COMPLETE
live QMX ALSA capture            COMPLETE
ADV QMX USB-host UAC RX          COMPLETE — hardware validated
12.64 s V2-compatible decode     COMPLETE
continuous multi-slot RX         COMPLETE
AutoSeq AS-0..AS-8              COMPLETE
V2-style ADIF logging            COMPLETE
V2-style Field Day Cabrillo      COMPLETE
physical QMX CAT TX              COMPLETE — Linux/QMX and ADV/QMX hardware validated
live QMX band CAT sync           COMPLETE — 1 s debounce, hardware validated
V -> 3 daily QSO view            COMPLETE — compact current-day ADIF list, validated
first real two-way QSO           COMPLETE — 2026-09-18 UTC
WinBook/TW700 QMX RX/CAT/TX      PASS
```

Linux remains the deterministic regression/reference environment. Linux/pc-1 + QMX has completed a real two-way MiniFT8-V3 QSO; WinBook/TW700 and rpi3-2 also validate portable Linux RX/CAT/TX. Cardputer ADV is now an accepted embedded RX/TX deployment target: real QMX CAT transmission, RX recovery, band sync, color status, and compact RX/TX paging have been exercised on hardware. T031 and T032 remain the accepted live band-sync and current-day QSO-view baselines.

## Working live QMX path

QMX native USB audio:

```text
48000 Hz
24-bit packed little-endian
2 channels
```

Linux and ADV each convert this to the same public Audio contract:

```text
12000 Hz
signed 16-bit PCM
2 ordered channels
```

MiniFT8 then runs:

```text
QMX USB-UAC
    -> Linux ALSA capture worker
       or ADV ESP-IDF USB-host UAC worker
       continuous buffered capture
    -> MiniShell Audio
       12 kHz / S16 / 2-channel
    -> rx_audio_adapter
    -> RxFrontend
       stereo -> mono
       12 kHz -> 6 kHz
    -> RxSlotFramer
       UTC schedules each live slot
       sample count frames producer blocks
       960 samples/block
    -> Ft8Engine
       monitor / waterfall
       candidate search
       LDPC / CRC
       message codec / hash store
    -> RxResultBuilder
    -> RxBatch
    -> app_controller
    -> AutoSeq + UiModel
```

The Linux capture worker is important: decoding is synchronous and can consume enough CPU time to overflow a small ALSA hardware buffer. Capture therefore runs independently and feeds MiniFT8 through a canonical-audio ring buffer. This matches the V2 architectural property that USB audio acquisition continues while decoding runs.

## ADV hardware milestone

T017 has crossed the main MiniFT8-V3 embedded RX milestone:

```text
Cardputer ADV / ESP32-S3 @ 240 MHz
    -> QMX USB Host
    -> UAC 48 kHz / 24-bit / stereo
    -> ADV canonical Audio 12 kHz / S16 / stereo
    -> MiniFT8-V3
    -> real on-air FT8 messages decoded
```

The validated ADV engine profile is the pinned-V2-compatible:

```text
time_osr = 2
freq_osr = 1
```

Representative live V -> Memory at `freq_osr=1`:

```text
Heap free      142.2K
Largest         82.0K
App allocated  129.2K
Alloc count          3
Largest/free       57%
RX                  ON
```

A temporary `freq_osr=2` comparison survived allocation but used about 103 KiB
more application memory:

```text
Heap free       58.8K
Largest         31.0K
App allocated  232.2K
Alloc count          3
Largest/free       52%
RX                  ON
```

No live messages were observed in that short comparison, but decode time/candidate
load were not measured and `freq_osr=2` substantially increases compute as well as
RAM. It is therefore deferred rather than classified as a decoder failure.
`freq_osr=1` remains the validated production profile.

T017 hardware acceptance established live decode across consecutive slots,
initial disconnected start followed by first QMX attachment, provider/ring
diagnostics, and post-FT8 `usbmsc`. One practical limitation remains: ADV tears
down the USB Host/UAC/CDC session when the final QMX user exits, and real QMX
hardware can fail a subsequent fresh enumeration. In that case QMX must be
power-cycled before another ADV MiniFT8 session. Linux normally does not show
this because the kernel keeps QMX enumerated while applications only reopen
ALSA/CDC handles.

## FT8 slot timing

MiniFT8-V3 now follows the V2 live cadence.

At 6 kHz:

```text
FT8 symbol period        160 ms
engine block             960 samples
FT8 symbols              79
V2 decode point          79 * 960 = 75840 samples = 12.64 s
full slot                90000 samples = 15.00 s
complete 960 blocks      93
slot-end remainder       720 samples
```

Live behavior (T081 software implementation; ADV/QMX acceptance pending):

```text
UTC - 1.60 s    reset slot-local waterfall writer and FFT history
UTC            anchor this slot's decode view
UTC + 12.64 s  submit this slot's decode
next -1.60 s   reset producer independently of decode/result state
```

UTC drives each action, including decode submission when missing Audio samples
leave the current waterfall incomplete. Capture never waits for decode or result
publication. ADV retains one core-1 worker and one `protocol_messages[50]`
buffer. A completed result is consumed promptly into an `RxBatch`; a completed
zero-message slot advances the batch/display generation and clears RX rows.

A prior RUNNING decode or READY result at the next decode trigger is an explicit
invariant failure, with the previous slot and elapsed time/result age reported.
It fails the RX step rather than silently dropping the new slot. An unusually
late decoder may read waterfall data overwritten by the next producer; this can
reduce yield without delaying capture.

Live Audio discontinuities reset frontend conversion only. They neither cancel
decode nor reacquire timing; the next UTC capture reset recovers the producer.
Startup waits for the next capture opportunity. Intentional physical-TX
pause/resume retains its existing cancellation and timing reinitialization.
`Ft8Engine` never reads wall-clock time directly; producer resets do not clear
decode jobs, and callsign-hash aging belongs to accepted decode-slot progression.

## Live QMX commands

Linux/pc-1:

```text
M$> ft8
```

T018 validates the Linux composition defaults:

```text
presentation  ADV
RX endpoint   alsa:hw:2,0
```

Explicit `--profile` and `--rx` still override these defaults. The deterministic
WAV override was validated with `/sd/kfs16b12k.wav`, and explicit
`--profile desktop` remains functional.

Cardputer ADV:

```text
M$> ft8
```

The packaged ADV application defaults to `uac:qmx`; QMX physical presence is not
supposed to be required for FT8 startup.

The exact ALSA device index can vary by host.

`audio_probe` is retained as a useful MiniShell Audio diagnostic. A healthy QMX result is approximately:

```text
rate       ~12000 Hz
L/R        identical for current QMX firmware
peak       nonzero
mean_abs   nonzero
```

## Linux QMX CAT

T019 validates the control transport boundary on real pc-1/QMX hardware:

```text
MiniFT8 radio_qmx
    -> MD6; FR0; FT0; FA...........;
    -> MiniShell Serial/CDC
    -> Linux tty / QMX USB CDC
```

MiniShell owns only raw Serial/CDC bytes and lifecycle. MiniFT8 owns QMX CAT syntax
and radio semantics.

Current operator form:

```text
M$> ft8 --cat serial:<QMX-CDC-path>
```

The receive-safe startup synchronization changes QMX to the selected MiniFT8
mode/VFO/dial frequency while live FT8 RX continues. Hardware validation also
confirms clean repeated close/reopen and no RF keying.

T019 itself deliberately did not emit TX/RX/TA/TM CAT commands. Later T022-T024
and the ADV physical-TX integration added the accepted transmitter lifecycle
without changing the T019 receive-safe synchronization contract.

## AutoSeq

The V2-equivalent structural AutoSeq port is complete through AS-8.

```text
RxBatch / RxMessage
        |
        v
app_controller
        |
        v
AutoSeq
   |       \
   |        -> semantic TxIntent / log eligibility
   v
QsoView -> UiModel -> T UIScreen
```

AutoSeq owns QSO queue/state/retry/priority/inactive-reactivation policy. It remains pure C with fixed-size storage and no MiniShell, UI, DSP, filesystem, Audio, or platform dependency.

The controller remains the sole coordinator.

## Logging

Logging is triggered at the same AutoSeq TX-start eligibility point used by V2.

### ADIF

Daily file:

```text
/flash/ft8/YYYYMMDD.txt
```

V2 behavior retained:

```text
mode             FT8
UTC date/time    MiniShell Time/Location API
frequency        selected FT8 dial frequency
station call     setting.txt callsign
my grid          first four grid characters
unknown -99 RST  omitted
write ACK         only after successful filesystem write
```

V3 currently has no V2-style configurable comment/radio metadata, so the ADIF comment field is empty rather than inventing new configuration.

### Field Day Cabrillo

File:

```text
/flash/ft8/fieldday.txt
```

The V2 Cabrillo structure is retained, including:

```text
START-OF-LOG: 3.0
CREATED-BY: Mini-FT8
CONTEST: ARRL-FIELD-DAY
...
QSO: ...
END-OF-LOG:
```

New QSO records are inserted before `END-OF-LOG:` and acknowledged independently from ADIF writes.

### RX/TX trace log

V2-compatible `RTYYMMDD.txt` traffic logging is implemented in V3 and controlled
by `rxtx_log` in `setting.txt`; it is part of the accepted physical-TX baseline.

## UI

Canonical ADV layout remains:

```text
20 x 7 text
```

Top row is exactly 20 characters:

```text
[screen:2] [band:2] [UTC HH:MM:SS] [page/total:3] [counter:1]
```

Top-level letter keys switch UIScreens case-insensitively. Current reserved screen keys are:

```text
R T O S V Q
```

Switching screens always enters the destination at top level. Page navigation wraps. UIScreen selection and TX/RX state are independent.

See `ui.md` for the canonical UI contract. T049 color status and T050 bare
`;`/`.` RX/TX page shortcuts are hardware accepted on ADV.

## Configuration

MiniFT8 owns:

```text
/flash/ft8/setting.txt
```

Current fields include station callsign/grid, profile, band, Skip TX1, retry count, CQ type/free text, general free text, and Field Day exchange.

The grid stored in `setting.txt` remains the persistent station grid. A live MiniShell location may override the controller's runtime effective grid for AutoSeq/logging during the session, but it does not mutate or persist over the configured grid.

MiniShell owns platform configuration separately under `/flash/config.txt`.

## Ownership summary

```text
MiniShell Audio
    owns public stream/device lifecycle and platform transport

rx_audio_adapter
    owns MiniFT8 Audio handle lifecycle

RxFrontend
    owns 12 kHz S16 stereo -> 6 kHz mono float

RxSlotFramer
    owns sample-count slot framing and decode-ready timing

Ft8Engine
    owns FT8 DSP/protocol/hash state

RxResultBuilder
    owns factual decode projection

AutoSeq
    owns QSO policy/state only

app_controller
    owns application coordination, TX-start log ordering and TX lifecycle

log_service
    owns ADIF/Cabrillo serialization, date/frequency/path policy and
    copy-on-write persistence through injected MiniShell FS/Time APIs
```

## Canonical documents

Read these first:

```text
README.md          current application status and contracts
development.md     completed stages, current baseline and next work
ui.md              canonical MiniFT8-V3 UI behavior
architecture.md    application ownership/dependency architecture
```

Detailed RX and AS stage documents remain in this directory as implementation history and regression rationale. They are subordinate to the current contracts above when wording conflicts.

## Non-standard / compound callsign TX

T027 completes the T021 deferred non-standard/hash TX path. Directed QSO messages
to a compound call such as `W1AW/9` remain normal STANDARD FT8 messages with the
V2-compatible 22-bit callsign hash, preserving the grid/report/terminal field.
Plain CQ from a non-standard local callsign uses FT8 type-4 and therefore carries
the full callsign without a grid. Modified non-standard CQ forms that cannot be
represented safely are rejected rather than degraded.

Software acceptance uses 36 pinned-V2 payload/tone vectors plus the production
decode -> selection -> AutoSeq -> mocked-QMX physical regression. Real RF
confirmation is opportunistic when a compound callsign appears naturally on air.

## RX display ordering

T028 adds controller-owned RX display/selection ordering while preserving factual
decode order for automatic processing and RT logging:

```text
reply-to-me   strongest -> weakest
CQ            strongest -> weakest
regular       strongest -> weakest
```

Equal-SNR entries preserve original decode order. Manual selection maps the
displayed row back to the original factual RxMessage. Live validation passed.
T049 adds presentation-only coloring from the same factual flags: reply-to-me
red, CQ green, regular white.

## RX display lifetime

T029 separates decoded-message display lifetime from RX transport state.

```text
completed RX batch
    -> display sorted rows

following TX slot
    -> keep previous RX rows visible during TX
    -> TX completion / RX resume clears them

ordinary RX audio/framer reset
    -> keep current displayed rows

next completed RX batch
    -> replace current display
    -> empty batch clears display
```

This avoids losing useful context at TX start while also avoiding stale prior-slot
messages after a TX slot has completed. Live Linux/QMX validation passed.

## Current follow-up boundaries

The QMX physical-TX boundary is complete on Linux and operational on ADV:

```text
AutoSeq TxIntent
    -> Ft8TxPlan
    -> app_controller slot-anchored scheduler
    -> MiniFT8 radio_qmx
    -> MiniShell Serial/CDC
    -> QMX CAT TX / TA / RX
```

Two portability/protocol follow-ups are deliberately deferred rather than active:

1. Permanent disambiguation of terminal `RR73` versus the legitimate Maidenhead
   locator `RR73`. T026 temporarily uses the pinned-V2 keyword-before-grid rule:
   exact GRID-coded `RR73` is treated as TX4; other grids remain TX1.
2. Stable Linux QMX endpoint discovery so ALSA card and tty enumeration do not
   require host-specific numeric endpoints after reboot.

ADV USB-host reuse remains a future architecture improvement: after FT8 exits,
ADV currently tears down the complete host/class-driver session. A second fresh
QMX enumeration can fail and require a QMX power cycle. A persistent resident
QMX session would make repeated ADV FT8 launches more Linux-like without
changing AutoSeq or FT8 encoding.