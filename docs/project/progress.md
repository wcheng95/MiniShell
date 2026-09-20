# MiniShell Progress Log

## Current baseline

- Maintained targets: Linux Mint and Cardputer ADV / ESP32-S3.
- Linux runtime applications use `.so`; ADV supports compiled-in applications plus runtime external `.elf` loading.
- ADV application resolution is `compiled-in > /flash/apps/<app>.elf > /sd/apps/<app>.elf`.
- Public MiniShell API generation is v3 and exposes App, System, Console, Memory, Filesystem, Time/Location, Display, Input, Audio, and Digital I/O.
- Architecture cleanup C0-C4 is complete.
- K1 runtime ELF, K2 Digital I/O, K3 Keyer engine, and K4 GPIO KeyIn/KeyOut are complete.
- MiniFT8 AutoSeq AS-0..AS-8 is complete.
- MiniFT8 live Linux/QMX RX is working continuously across consecutive FT8 slots. T018 makes bare Linux `ft8` the validated operator command: ADV presentation plus live `alsa:hw:2,0` QMX RX by default.
- MiniFT8 live Cardputer ADV/QMX USB-host RX is fully hardware-validated at 240 MHz with the V2-compatible `time_osr=2, freq_osr=1` engine profile: live decode, consecutive slots, initial late attach, repeated FT8 lifecycle, provider continuity, and post-FT8 `usbmsc` all pass.
- MiniFT8 V2-style ADIF and Field Day Cabrillo logging are implemented through MiniShell APIs.
- Linux MiniShell Serial/CDC plus MiniFT8-owned receive-safe QMX CAT is hardware-validated on pc-1: mode/VFO/dial-frequency sync works with live RX and no transmit.
- Linux/pc-1 + QMX now has a completed real two-way MiniFT8-V3 QSO. Physical CAT keying, immutable 79-tone plans, RX recovery, RxTxLog, CQ/POTA beacon operation, and V2-compatible Random/Fixed/RX offset selection are accepted production behavior.
- WinBook/TW700 runs the pc-1-built Linux MiniShell/ft8 binaries with QMX ALSA RX/decode and CDC CAT/TX validated; the login user must have normal `dialout` access.
- `rpi3-2` (Raspberry Pi 3, AArch64 Linux) natively builds and runs MiniShell/MiniFT8 with QMX: portable `.so` apps load, live ALSA RX decodes FT8, CDC CAT works, and physical TX/RX operation is validated. Fresh-host build prerequisites were `cmake`, `build-essential`, and `libasound2-dev`; without `libasound2-dev`, MiniShell still builds but the ALSA provider is compiled out. Keep this result as the Linux/AArch64 bring-up reference for future CardputerZero work.
- T025 adds resident MiniShell aliases from `/flash/minishell/alias.txt` with first-`=` parsing, one-level expansion, and live reload.
- T026 temporarily restores V2 keyword-before-grid precedence for exact `RR73`. Because `RR73` is also a valid Maidenhead locator, permanent disambiguation remains a deferred design follow-up.
- T027 completes V2-compatible non-standard/hash FT8 TX: directed compound calls use 22-bit hash packing in normal STANDARD messages, while plain CQ from a non-standard local call uses type-4. Software acceptance is complete; RF confirmation is opportunistic when such a station appears on air.
- T028 adds controller-owned RX display/selection ordering: reply-to-me, CQ, regular; strongest-to-weakest within each group. The factual RxBatch and automatic processing/logging order remain unchanged. Live validation passed.
- T029 completes RX display lifetime semantics: previous decoded rows remain visible throughout TX and ordinary RX transport resets, then clear when TX completes/RX resumes or are replaced by the next completed RX batch. Live validation passed.
- T031 completes live QMX band synchronization: O -> 3 updates MiniFT8 immediately, then an already-connected QMX follows the final selected band after a 1-second debounce. Rapid band stepping coalesces to one final CAT sync; hardware validation passed on 2026-09-20.
- T032 completes the read-only `V -> 3` current-day QSO view: MiniFT8 streams today's ADIF log into six-row compact `HH:MM band call` pages, refreshes after new QSOs, and uses the accepted large-page top-line rules. Hardware/use validation passed on 2026-09-20.
- T033 is TESTING: ADV WebFS read-only SoftAP/browser implementation passed software/build review and is ready for hardware validation. It browses `/flash` and `/sd` and streams downloads through the existing MiniShell Filesystem API without USB media handoff or cable-role changes.
- ADV remains a validated embedded RX deployment target. Physical FT8 TX is currently accepted on Linux/QMX; carrying the proven TX boundary to ADV is future work rather than an active task.

## Current MiniFT8 baseline

```text
QMX USB-UAC 48 kHz / S24_3LE / stereo
        |
        +--> Linux ALSA capture worker
        |
        `--> ADV ESP-IDF USB-host UAC worker
                    |
                    v
MiniShell Audio ring
12 kHz / S16 / stereo
        |
        v
RxFrontend
6 kHz mono float
        |
        v
RxSlotFramer
960 samples/block
        |
        v
Ft8Engine
        |
        v
RxResultBuilder -> RxBatch
        |
        v
app_controller
   /          \
  v            v
AutoSeq      log_service -> MiniShell FS/Time
  |
  v
TxIntent -> Ft8TxPlan
  |
  v
radio_qmx -> MiniShell Serial/CDC -> QMX CAT TX/RX/TA
```

Live FT8 timing follows the pinned MiniFT8-V2 behavior:

```text
0.00 s   begin slot
12.64 s  decode after 79 x 960 samples
15.00 s  discard incomplete 720-sample tail and start next slot
```

Linux capture continues independently while synchronous FT8 decoding runs, preventing ALSA/UAC overrun from breaking later-slot alignment.

Working commands:

```text
Linux: M$> ft8
ADV:   M$> ft8
```

On Linux/pc-1, bare `ft8` composes to the ADV presentation and
`alsa:hw:2,0`. Explicit `--profile` and `--rx` options remain available for
desktop presentation, WAV fixtures, or alternate devices. The ALSA card number
and Linux tty number are host-enumeration details and may change after reboot;
stable QMX endpoint discovery remains a deferred portability improvement.

The packaged ADV application defaults bare `ft8` to `uac:qmx`. Real hardware now passes USB-host/UAC bring-up and live on-air decode at 240 MHz. The accepted ADV engine profile remains `time_osr=2, freq_osr=1`. A temporary `freq_osr=2` experiment remained alive but reduced free/largest heap to about 58.8/31.0 KiB. Decode time/candidate load were not measured, so the lack of observed messages is inconclusive; its higher RAM/compute cost is deferred to a later performance study. It is not the production baseline.

QMX post-enumeration unplug/replug recovery is not a T017 requirement: real QMX hardware can return a short device descriptor on a second enumeration, matching the practical MiniFT8-V2 limitation. Initial startup with QMX absent followed by the first attachment remains the required disconnected-start behavior.

Current MiniFT8 status:

```text
RX-0..RX-7   COMPLETE
RX-8         COMPLETE — live QMX ALSA, V2 timing, continuous capture
T017         COMPLETE — ADV QMX USB-host RX live decode + lifecycle + usbmsc hardware validation
T018         COMPLETE — Linux bare ft8 -> ADV presentation + live QMX RX defaults
T019         COMPLETE — Linux Serial/CDC + MiniFT8-owned QMX CAT frequency sync
T020         COMPLETE — QMX CAT TX primitives; real RF path validated through T022-T024
T021         COMPLETE — pure FT8 TX text/payload/79-tone plan, V2-vector verified
AS-0..AS-8   COMPLETE
LOG-1        COMPLETE — daily ADIF + Field Day Cabrillo
T022         COMPLETE — integrated Linux/QMX physical FT8 TX + RX recovery + RxTxLog
T023         COMPLETE — CQ/CQ POTA + beacon OFF/EVEN/ODD, hardware validated
T024         COMPLETE — V2-compatible Random/Fixed/RX TX offset, hardware validated
T025         COMPLETE — resident MiniShell aliases from /flash/minishell/alias.txt
T026         COMPLETE — temporary V2-compatible RR73-before-grid responder fix
T027         COMPLETE — V2-compatible non-standard/hash TX + type-4 plain CQ
T028         COMPLETE — RX display/selection priority groups + descending SNR
T029         COMPLETE — RX rows persist through TX; clear at TX completion/resume
T031         COMPLETE — live QMX band CAT sync, 1 s debounce, hardware validated
T032         COMPLETE — V -> 3 current-day QSO compact view, hardware validated
T033         TESTING — ADV WebFS read-only SoftAP/browser proof, hardware pending
First QSO    COMPLETE — real two-way Linux/QMX contact on 2026-09-18 UTC
WinBook      RX/TX PASS — pc-1 binaries run; QMX ALSA decode + CAT TX validated (user must be in dialout)
rpi3-2       RX/TX PASS — native AArch64 build; QMX ALSA decode + CDC CAT + physical TX validated
```

Canonical MiniFT8 entry points:

```text
docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/ui.md
docs/MiniFT8/architecture.md
```

Detailed `rx-*` and `as-*` documents remain historical implementation records and regression rationale.

## MiniFT8 logging

Daily ADIF:

```text
/flash/ft8/YYYYMMDD.txt
```

Field Day Cabrillo:

```text
/flash/ft8/fieldday.txt
```

AutoSeq owns pure log eligibility/events and per-format ACK state. `app_controller` coordinates TX-start ordering and passes station/QSO facts to `log_service`, which owns ADIF/Cabrillo serialization, date/frequency/path policy and copy-on-write persistence through injected MiniShell Filesystem and Time/Location APIs (T006/T007). Sync/close precede rename as the commit point; the controller ACKs only successful persistence.

V2-compatible `RTYYMMDD.txt` RxTxLog is implemented and hardware validated through T022-T026; `rxtx_log` defaults ON and is configurable in `station.txt`. It captured the first completed two-way QSO and the responder RR73 regression evidence.

## Keyer baseline

```text
K0 architecture gate          COMPLETE
K1 ADV runtime ELF proof      COMPLETE
K2 MiniShell Digital I/O      COMPLETE
K3 portable Keyer engine      COMPLETE
K4 GPIO KeyIn/KeyOut          COMPLETE
K5 sidetone                   IMPLEMENTED / TRANSPORT HARDWARE-VALIDATED
```

The controller keeps orchestration in `app_controller`:

```text
config_service
      |
      v
                  +--> keyin ------> MiniShell Digital I/O
                  +--> keyer_engine
app_controller ---+--> keyout ------> MiniShell Digital I/O
                  `--> sidetone ----> MiniShell Audio TX
```

K5 software is implemented; T009/T010 provide ADV Audio TX transport hardware evidence. K6 UI/settings and K7 audible/operator field acceptance remain future work.

Default ADV deployment remains:

```text
G13  KeyIn tip    active-low input + pull-up
G15  KeyIn ring   active-low input + pull-up
G3   KeyOut tip   active-low open-drain
G6   KeyOut ring  active-low open-drain
```

Real Cardputer ADV hardware validation passed for physical GPIO KeyIn/KeyOut, clean application exit, released outputs, and no observable RAM leakage across repeated load/run/exit.

## Configuration ownership

```text
/flash/config.txt           MiniShell-owned resident/platform configuration
/flash/minishell/alias.txt  MiniShell resident command aliases
/flash/<app>/setting.txt    application-owned configuration/deployment settings
```

MiniFT8 currently retains its established path:

```text
/flash/ft8/station.txt
```

Any future rename to `/flash/ft8/setting.txt` is a separate migration.

## Public architecture

Applications use MiniShell public services only. Platform-specific transport remains below the service boundary.

```text
Applications
      |
      v
MiniShell API
      |
      v
portable services/core
      |
      v
Linux / ADV backends
```

The Linux and ADV QMX work reinforce this rule: ALSA and ESP-IDF USB-host/UAC mechanics remain platform-side; MiniFT8 sees only canonical MiniShell Audio.

## Testing

Linux CTest covers shell/runtime behavior, service contracts, filesystem/resource policy, terminal input, audio transport, Digital I/O, Keyer, MiniFT8 UI/runtime behavior, AutoSeq, and focused FT8 DSP/protocol tests.

Useful MiniFT8 live diagnostic:

```text
audio_probe alsa:hw:2,0 5
```

A healthy current QMX stream is approximately 12 kHz canonical S16 stereo with identical L/R samples and nonzero signal level.

## Canonical documents

```text
docs/README.md
docs/project/progress.md
docs/project/architecture-cleanup.md

docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/configuration.md
docs/architecture/resident-vs-app.md

docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/ui.md
docs/MiniFT8/architecture.md

docs/keyer/README.md
platform/adv/README.md
platform/adv/elf_apps/keyer/README.md
```
