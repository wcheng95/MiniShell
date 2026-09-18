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
physical TX / CAT / Audio TX     NOT YET PORTED
```

Linux is the active MiniFT8 completion platform and deterministic regression/reference environment. The first real MiniFT8-V3 QSO is targeted on Linux/QMX. ADV remains a fully validated embedded RX deployment target and should be revisited only when work specifically depends on ESP32-S3 USB, memory, display/input, or other embedded-only behavior.

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
       UTC establishes initial slot phase
       sample count owns progression
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

T017 hardware acceptance is complete: live decode across consecutive slots,
initial disconnected start followed by first QMX attachment, repeated
`ft8 -> quit -> ft8`, provider continuity/ring diagnostics, and post-FT8
`usbmsc` all pass. Recovery from unplugging and replugging an already-enumerated
QMX is not required: the device can fail its own second enumeration, matching the
practical V2 limitation.

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

Live behavior:

```text
UTC slot boundary
    -> begin waterfall
    -> process one 960-sample block at a time
    -> decode immediately after block 79 / 12.64 s
    -> reset waterfall only
       preserve FFT sample history
    -> continue consuming audio through 15.0 s
    -> discard the final partial 720-sample block
    -> begin next slot
```

The first partial slot after stream start or discontinuity is discarded. `Ft8Engine` never reads wall-clock time directly.

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
station call     station.txt callsign
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

V2 also has optional `RTYYMMDD.txt` traffic logging controlled by `rxtx_log`. V3 does not yet expose that setting, so this optional diagnostic log has not been enabled silently.

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

See `ui.md` for the canonical UI contract.

## Configuration

MiniFT8 owns:

```text
/flash/ft8/station.txt
```

Current fields include station callsign/grid, profile, band, Skip TX1, retry count, CQ type/free text, general free text, and Field Day exchange.

The grid stored in `station.txt` remains the persistent station grid. A live MiniShell location may override the controller's runtime effective grid for AutoSeq/logging during the session, but it does not mutate or persist over the configured grid.

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

## Next

The next major production boundary is real TX integration on Linux/QMX, with the first MiniFT8-V3 QSO intentionally happening on Linux before the finished TX path is carried back to ADV:

```text
AutoSeq TxIntent
    -> app_controller
    -> MiniShell Control / Audio TX
    -> QMX CAT + USB audio TX
```

The existing simulated TX lifecycle and logging eligibility should remain the behavioral reference while physical TX is added.