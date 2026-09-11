# MiniFT8-V3 First QSO Plan

Status: **implementation plan**

Goal: complete the first real MiniFT8-V3 QSO using the Linux backend on `pc-1` with a QMX connected over USB.

This milestone deliberately proves the portable MiniFT8-V3 application path before implementing ADV USB UAC. MiniFT8-V2 remains the behavioral reference for the QMX UAC/CAT path, pinned at:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

The V2 behavior is reused where useful; its platform coupling and structure are not copied wholesale.

## Milestone boundary

The first-QSO path is:

```text
RX
QMX USB UAC
    -> Linux Audio provider
    -> MiniShell Audio API
    -> rx_audio_adapter
    -> RxFrontend
    -> RxSlotFramer
    -> Ft8Engine
    -> RxResultBuilder
    -> app_controller
    -> AutoSeq / UI

TX
app_controller
    -> TxLifecycle
    -> radio_service
    -> QMX CAT adapter
    -> MiniShell serial byte-stream API
    -> Linux tty/CDC provider
    -> QMX
```

MiniFT8 application code must remain platform-independent. Linux device APIs, ALSA/USB details, tty details, and later ADV USB-host details stay below MiniShell providers.

`app_controller` remains the sole production coordinator. `auto_seq`, `tx_lifecycle`, `rx_audio_adapter`, and the QMX adapter must not develop side channels to each other.

## In scope

- real Linux UTC and FT8 15-second slot timing
- live QMX USB-UAC receive audio
- UTC-to-audio-stream slot anchoring
- compact `rtYYYYMMDD.txt` RxTxLog
- configurable RxTxLog directory through `station.txt`
- minimal portable MiniShell serial byte-stream API
- Linux tty/CDC serial provider
- QMX CAT protocol adapter using proven V2 behavior
- real QMX CAT transmission at the correct FT8 slot boundary
- safe return to RX on normal completion, error, cancel, and app exit
- one complete real FT8 QSO

## Out of scope

- ADV USB UAC
- ADV USB CDC/CAT backend
- ADIF generation
- generalized multi-radio discovery policy
- broad radio abstraction beyond what is required to preserve a clean MiniFT8/MiniShell boundary
- changes to AutoSeq behavior that are not required for the first QSO

ADIF is intentionally deferred. The RxTxLog is expected to retain enough factual information to derive ADIF later.

# FQ-0 — Freeze the boundary

Before implementation, preserve these ownership rules:

```text
MiniShell Time/Location
    owns portable UTC access

MiniShell Audio
    owns logical audio stream transport

MiniShell serial byte-stream service
    owns portable byte transport only

Linux providers
    own ALSA/device/tty details

RxSlotFramer
    owns sample-count slot progression after anchoring

TxLifecycle
    owns FT8 slot/parity execution eligibility

radio_service
    owns application-level radio execution

QMX CAT adapter
    owns QMX protocol commands

storage_service
    owns MiniFT8 filesystem operations

app_controller
    owns production coordination
```

MiniShell serial transport must not know QMX CAT commands. The QMX adapter must not know Linux tty APIs.

# FQ-1 — Real FT8 timing

Timing is a hard prerequisite for both live RX and live TX.

## Timing rule

```text
UTC establishes the real 15-second FT8 boundary.
Sample counting owns progression inside the RX stream after that anchor.
```

MiniShell Time/Location is the only production UTC source visible to MiniFT8.

For FT8:

```text
slot_id = floor(UTC_seconds / 15)
slot phase = UTC modulo 15 seconds
```

The application-facing audio stream is:

```text
12000 Hz
S16
2 ordered channels
```

Therefore one 15-second input slot contains:

```text
180000 input frames
```

After `RxFrontend`, the engine-native stream remains:

```text
6000 Hz mono float
90000 samples per 15-second slot
```

## RX anchor

The live UAC path must bridge real UTC and the first captured audio frames.

At stream start or discontinuity:

1. obtain a coherent MiniShell UTC/monotonic reference;
2. determine current `slot_id` and position within the 15-second slot;
3. initialize/reset `RxSlotFramer` with the matching slot identity and sample offset;
4. discard the current partial slot if the stream did not begin exactly at a full boundary;
5. after the next full boundary, progress by sample count rather than repeated wall-clock polling.

This preserves the existing V3 rule that the first partial slot is not decoded.

## TX boundary

TX uses the same UTC-derived `slot_id` and parity as RX.

The existing `TxLifecycle` boundary window remains a missed-boundary/catch-up guard, not the desired TX timing accuracy. Real TX should begin as close to the actual `00/15/30/45` UTC boundary as practical on Linux.

A missed slot or UTC correction must re-anchor and must never cause a catch-up transmission.

## FQ-1 tests

- deterministic unit tests at `:00`, `:15`, `:30`, `:45`
- tests around `14.999 -> 15.000` and UTC date rollover
- partial-stream-start test proving the first incomplete slot is discarded
- discontinuity/reset test proving a new UTC anchor is installed
- live Linux observation showing consecutive slot boundaries remain aligned while RX advances by sample count

FQ-1 is complete when live QMX audio can be associated with the correct real FT8 slot identity before decoding.

# FQ-2 — RxTxLog

RxTxLog is required for the first-QSO milestone because it is the primary operational/debug record.

## Configuration

Add to `station.txt`:

```text
rxtxlog_path=/flash
```

The user may instead choose, for example:

```text
rxtxlog_path=/sd
```

`rxtxlog_path` names the directory only. MiniFT8 creates the daily filename.

## Filename

V3 uses UTC date and the new canonical filename:

```text
rtYYYYMMDD.txt
```

Examples:

```text
/flash/rt20260911.txt
/sd/rt20260911.txt
```

At UTC date rollover, subsequent records go to the new daily file.

## Base record semantics

Preserve the useful MiniFT8-V2 information content:

```text
TX: direction, timestamp, frequency, transmitted message, audio offset Hz
RX: direction, timestamp, frequency, decoded message, SNR, audio offset Hz
```

Use `T` and `R` direction markers as in V2.

RX records are written after a successful decoded message is accepted into the factual RX result path.

TX records are written when the radio transmission actually starts, not when AutoSeq merely creates an intent.

## V3 compact timestamp format

The first record of every MiniFT8 run/reopen must contain a full independent UTC anchor:

```text
[YYYYMMDD HHMMSS]
```

Example:

```text
[20260911 184300]
```

Subsequent records use the shortest changed suffix:

```text
[SS]              inherit YYYYMMDD HHMM
[MMSS]            inherit YYYYMMDD HH
[HHMMSS]          inherit YYYYMMDD
[YYYYMMDD HHMMSS] full anchor
```

Example sequence:

```text
[20260911 184300]
[15]
[30]
[45]
[4400]
[15]
[30]
[45]
[190000]
```

The file date already changes at UTC midnight, but a new app run still emits a new full anchor even when appending to an existing daily file. This makes every run independently recoverable after restart, crash, or partial-file extraction.

## V3 compact frequency format

Frequency is written in MHz on:

- the first record of each MiniFT8 run/reopen;
- every later record where the frequency changes.

Otherwise it is inherited from the previous record.

Example:

```text
[14.074]
```

## Example log

```text
R [20260911 184300][14.074] CQ W1XYZ FN42 -12 1534
R [15] K6ABC W1XYZ -08 1422
T [30] W1XYZ AG6AQ CM97 1534
R [45] AG6AQ W1XYZ R-12 -10 1534
T [4400] W1XYZ AG6AQ RR73 1534
R [15] AG6AQ W1XYZ 73 -09 1534
R [4515][7.074] CQ K7ABC CN87 -15 1280
```

The format is intentionally optimized for compact debug logging rather than visual neatness.

## Storage implementation

Extend `storage_service` with append support instead of bypassing it from logging code.

Conceptually:

```text
logging owner
    -> storage_service_append_text(...)
    -> MiniShell FS API
```

The implementation may use:

```text
MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_APPEND
```

No MiniFT8 module outside the storage boundary should call platform filesystem APIs directly.

## FQ-2 tests

- `station.txt` parse/serialize test for `rxtxlog_path`
- default-path test
- daily filename test
- full-anchor test on first record
- `[SS]`, `[MMSS]`, and `[HHMMSS]` compression tests
- frequency inheritance/change tests
- app-reopen test proving a fresh full timestamp and frequency anchor
- UTC midnight file-rotation test
- RX and TX record-format tests

# FQ-3 — Live Linux QMX UAC RX

Replace the Linux WAV fixture provider for production receive with a real Linux audio-device provider while preserving the public MiniShell Audio contract:

```text
12000 Hz / S16 / 2 channels
```

The provider may convert from the QMX native Linux audio format below the MiniShell API if necessary, but channel order and application-facing format remain unchanged.

Do not change `rx_audio_adapter`, `RxFrontend`, `RxSlotFramer`, or `Ft8Engine` merely to accommodate Linux device APIs.

Reference MiniFT8-V2 areas:

```text
main/audio_source.cpp
main/stream_uac.cpp
```

FQ-3 success criteria:

```text
QMX on-air RX
    -> Linux Audio provider
    -> MiniShell Audio
    -> real UTC/sample-count slot framing
    -> Ft8Engine
    -> decoded real FT8 messages
    -> rtYYYYMMDD.txt RX records
```

# FQ-4 — MiniShell serial byte-stream API and Linux provider

MiniShell currently has no public serial/CAT transport in the application API. Add the smallest portable byte-stream interface needed by the QMX adapter.

Conceptual V1 operations:

```text
open(endpoint)
read(..., timeout)
write(..., timeout)
close()
```

The exact public type names may be chosen during implementation, but the boundary is fixed:

```text
MiniFT8/QMX adapter sees portable bytes.
Linux provider sees tty/CDC details.
```

Do not create a QMX-specific MiniShell API.

Use host unit tests or a PTY-style test before using QMX hardware.

Device-discovery/configuration policy is not frozen by this plan; the first-QSO implementation only needs a reliable way to select the QMX endpoint on `pc-1`.

# FQ-5 — QMX CAT radio service

Port the proven QMX behavior from MiniFT8-V2 into a MiniFT8-V3 QMX adapter above the MiniShell serial transport.

Reference:

```text
main/radio_control_qmx.cpp
```

Required first-QSO operations are approximately:

```text
ready
sync frequency/mode
begin TX
set FT8 tone
end TX / return RX
```

Proven V2 commands include:

```text
MD6;
FR0;
FT0;
FA...........;
TX;
TAxxxx.xx;
RX;
```

QMX command formatting and protocol details belong in the QMX adapter/radio service, never in MiniShell serial transport.

`radio_service` is called through `app_controller`; `TxLifecycle` must not call QMX or MiniShell serial directly.

# FQ-6 — Real TX execution

Replace AS-7's simulated TX completion with real execution while preserving existing AutoSeq/TxLifecycle semantics.

At an eligible real UTC slot boundary:

```text
TxLifecycle boundary
    -> app_controller snapshots AutoSeqTxIntent
    -> radio_service prepares QMX
    -> QMX TX starts
    -> CAT tone sequence executes
    -> QMX returns to RX
    -> TX completion advances AutoSeq
```

The transmitted message is added to RxTxLog only when transmission actually begins.

## Safety invariant

Every path that may leave QMX transmitting must have an explicit fail-safe return to RX.

This includes:

- normal completion
- CAT write error
- timing abort
- user cancel/ESC
- app exit
- partial startup failure after QMX entered TX

If the system cannot prove that TX completed normally, it should still attempt `RX;` before releasing the control stream.

# FQ-7 — First real QSO and hardening

The first-QSO milestone passes when all of the following are true on `pc-1` with QMX:

```text
real MiniShell UTC
    -> correct 15 s slot identity
    -> live QMX UAC RX
    -> real FT8 decode
    -> compact RxTxLog RX record
    -> manual/AutoSeq reply intent
    -> correct next-slot CAT TX
    -> compact RxTxLog TX record
    -> QMX returns to RX
    -> addressed reply is decoded
    -> one normal QSO completes
    -> ft8 exits cleanly
```

After the first successful QSO, exercise at least:

- repeated `ft8 -> exit -> ft8`
- QMX audio disconnect/reconnect
- CAT endpoint failure
- CAT timeout during TX
- cancel during an armed or active TX
- app exit after TX
- frequency change with correct compact-log frequency anchor
- log path on `/flash`
- log path on `/sd` where supported

# Completion state

The first-QSO implementation is intentionally a Linux reference milestone.

Once it is stable, the ADV UAC/CAT work should mostly replace providers beneath the same MiniShell boundaries:

```text
Linux Audio provider   -> ADV UAC provider
Linux serial provider  -> ADV USB CDC provider
```

The following should remain unchanged across that transition:

```text
app_controller
AutoSeq
TxLifecycle
radio_service semantics
QMX CAT protocol adapter
RX frontend/framer/engine
RxTxLog format and policy
```

That separation is the main architectural proof delivered by the first-QSO milestone.
