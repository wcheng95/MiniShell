# MiniShell Progress Log

## Current baseline

- Maintained targets: Linux Mint and Cardputer ADV / ESP32-S3.
- Portable QRP radio scope is closed at five modes: CW (Mini-CW), FT8, JS8 Normal, classic 45.45-baud/170-Hz RTTY, and Robot 36 SSTV. Protocol breadth is frozen; future work should deepen field-useful features inside these five rather than add FT4, extra JS8/SSTV modes, or a sixth radio mode.
- Linux runtime applications use `.so`; ADV supports compiled-in applications plus runtime external `.elf` loading.
- ADV application resolution is `compiled-in > /flash/apps/<app>.elf > /sd/apps/<app>.elf`.
- Public MiniShell API generation is v3 and exposes App, System, Console, Memory, Filesystem, Time/Location, Display, Input, Audio, and Digital I/O.
- Architecture cleanup C0-C4 is complete.
- The superseded `apps/keyer` implementation/build/tests have been removed. Mini-CW (`minicw`) is the accepted CW application under MiniShell; T042-T048 and the ownership audit are hardware accepted. Historical T038/T040 task records remain for reference.
- MiniFT8 AutoSeq AS-0..AS-8 is complete.
- MiniFT8 live Linux/QMX RX is working continuously across consecutive FT8 slots. T018 makes bare Linux `ft8` the validated operator command: ADV presentation plus live `alsa:hw:2,0` QMX RX by default.
- MiniFT8 live Cardputer ADV/QMX USB-host RX is fully hardware-validated at 240 MHz with the V2-compatible `time_osr=2, freq_osr=1` engine profile: live decode, consecutive slots, initial late attach, repeated FT8 lifecycle, provider continuity, and post-FT8 `usbmsc` all pass.
- T081 is COMPLETE: MiniFT8 live RX capture and decode lifecycles are decoupled. UTC independently drives the `-1.60 s` slot-local waterfall reset and `+12.64 s` decode trigger; live Audio discontinuities no longer cancel decode or restart slot timing; late RUNNING/READY state is an invariant fault rather than a normal slot-drop path. ADV/QMX live validation passed on 2026-09-25, and the previous held-RX-slot symptom was no longer observed.
- MiniFT8 V2-style ADIF and Field Day Cabrillo logging are implemented through MiniShell APIs.
- Linux MiniShell Serial/CDC plus MiniFT8-owned receive-safe QMX CAT is hardware-validated on pc-1: mode/VFO/dial-frequency sync works with live RX and no transmit.
- Linux/pc-1 + QMX now has a completed real two-way MiniFT8-V3 QSO. Physical CAT keying, immutable 79-tone plans, RX recovery, RxTxLog, CQ/POTA beacon operation, and V2-compatible Random/Fixed/RX offset selection are accepted production behavior.
- WinBook/TW700 runs the pc-1-built Linux MiniShell/ft8 binaries with QMX ALSA RX/decode and CDC CAT/TX validated; the login user must have normal `dialout` access.
- `rpi3-2` (Raspberry Pi 3, AArch64 Linux) natively builds and runs MiniShell/MiniFT8 with QMX: portable `.so` apps load, live ALSA RX decodes FT8, CDC CAT works, and physical TX/RX operation is validated. Fresh-host build prerequisites were `cmake`, `build-essential`, and `libasound2-dev`; without `libasound2-dev`, MiniShell still builds but the ALSA provider is compiled out. Keep this result as the Linux/AArch64 bring-up reference for future CardputerZero work.
- T066 is COMPLETE: Linux JS8 can receive browser/WebSDR audio through a PulseAudio/PipeWire monitor source using the platform `pulse:` endpoint. Real pc-1 KFS WebSDR traffic decoded with `--rx-delay-ms 0`, zero observed drops/discontinuities, and multiple valid HB/directed frames; source delay correction remains available but was not needed for the accepted KFS path.
- T065 is COMPLETE: Linux MiniShell now has an architect-accepted external `rtty` WAV receiver using the portable 12 kHz S16 streaming RTTY/ITA2 core. Clean generated 45.45-baud/170-Hz normal-LSB audio decodes through the actual runtime app; live QMX/WebSDR receive and ADV `rtty.elf` packaging remain deferred.
- T025 adds resident MiniShell aliases from `/flash/minishell/alias.txt` with first-`=` parsing, one-level expansion, and live reload.
- T026 temporarily restores V2 keyword-before-grid precedence for exact `RR73`. Because `RR73` is also a valid Maidenhead locator, permanent disambiguation remains a deferred design follow-up.
- T027 completes V2-compatible non-standard/hash FT8 TX: directed compound calls use 22-bit hash packing in normal STANDARD messages, while plain CQ from a non-standard local call uses type-4. Software acceptance is complete; RF confirmation is opportunistic when such a station appears on air.
- T028 adds controller-owned RX display/selection ordering: reply-to-me, CQ, regular; strongest-to-weakest within each group. The factual RxBatch and automatic processing/logging order remain unchanged. Live validation passed.
- T029 completes RX display lifetime semantics: previous decoded rows remain visible throughout TX and ordinary RX transport resets, then clear when TX completes/RX resumes or are replaced by the next completed RX batch. Live validation passed.
- T031 completes live QMX band synchronization: O -> 3 updates MiniFT8 immediately, then an already-connected QMX follows the final selected band after a 1-second debounce. Rapid band stepping coalesces to one final CAT sync; hardware validation passed on 2026-09-20.
- T032 completes the read-only `V -> 3` current-day QSO view: MiniFT8 streams today's ADIF log into six-row compact `HH:MM band call` pages, refreshes after new QSOs, and uses the accepted large-page top-line rules. Hardware/use validation passed on 2026-09-20.
- T033 is COMPLETE: ADV WebFS read-only SoftAP/browser file management is hardware validated. `/flash` and `/sd` browsing plus downloads work without cable handoff; FT8/QMX works before and after WebFS in the same boot using CPU1-owned USB Host lifetime.
- T034 is COMPLETE: safe WebFS mutations are hardware validated — streamed upload/replace with temp-file commit, mkdir, same-directory regular-file rename, file delete, empty-directory delete, interrupted replacement safety, `/sd` operation, and same-boot FT8/QMX all pass.
- T035 is COMPLETE: WebFS uses stable SoftAP credentials from `/flash/minishell/setting.txt`; iPhone reconnects without re-entering a generated password, invalid/missing settings fall back safely, T034 file operations remain intact, and same-boot FT8/QMX still works.
- T069/T071 are COMPLETE: `/flash/minishell/setting.txt` now supports hardware-accepted `startup=cmd1;cmd2` sequencing through the normal shell/alias path before the first prompt. Foreground apps block later startup commands until exit, failures continue, and the prompt returns normally afterward. The attempted resident `brightness=` feature was dropped and removed; legacy lines are ignored.
- T072 is COMPLETE: MiniShell now has a service-owned session CWD with resident `cd`/`pwd`; relative paths work through the portable Filesystem API and are inherited by foreground apps. ADV hardware validation passed, including relative `ls`/nano and CWD persistence after app exit.
- T073 is COMPLETE: shell defaults now match the CWD model — bare `ls` means `ls .`, bare `cd` means `cd /`, and explicit `ls /` retains its established root presentation. ADV hardware validation passed.
- T074 is COMPLETE: portable `cp`/`mv` now accept existing directory destinations (`DIR`, `DIR/`, `DIR/.`) and target `DIR/<source-basename>`, including relative operands under the session CWD. ADV hardware validation passed; `mv` remains rename-only with no cross-filesystem copy/delete fallback.
- T075 is COMPLETE: the shared 10-command RAM-only editable resident history is accepted on both ADV and pc-1. Startup/redirected input are excluded, duplicates/FIFO eviction and draft restoration work, Linux uses Up/Down with normal cursor editing, and history survives foreground app return but not reboot/new session.
- T076 is COMPLETE: ADV resident controls are hardware accepted as Ctrl+`;`/Ctrl+`.` for five-row console scrollback, Fn+`;`/Fn+`.` for history previous/next, and Fn+`,`/Fn+`/` for cursor left/right.
- T077 is COMPLETE: ADV resident command editing now has a hardware-accepted blinking inverse-cell cursor that follows wrapped/long commands without mutating retained console history.
- T078 is COMPLETE: pathname completion is explicitly Tab-gated on both ADV and pc-1. Typing/paste remain literal; Tab invokes the shared public-Filesystem longest-common-prefix matcher. Absolute/relative completion, ambiguity handling, paste safety, ADV cursor/history/scrollback interaction, and pc-1 terminal editing are hardware/host accepted. T079 remains reserved for showing ambiguous pathname choices when Tab cannot grow the prefix further.
- T079 is COMPLETE: after Tab reaches an ambiguous longest pathname prefix, another Tab lists all remaining valid pathname choices and restores the same editable prompt/line/cursor. ADV and pc-1 manual validation passed, including repeated listing, directory display markers, literal typing/paste, ADV scrollback/cursor restoration, and Enter submitting only the command line.
- T080 is COMPLETE: resident `clear` is hardware/host accepted on ADV and pc-1. It clears resident console output/history while preserving T075 command history, CWD, aliases/settings and T078/T079 Tab behavior; Linux uses TTY-only ANSI clear/home and ADV resets its retained 50-row console plus USB mirror. This completes the current resident-shell usability round T075-T080.
- T036 is BREAK / NOT ACCEPTED: the one-shot ADV MiniFT8 web mirror passed software/build review but prevented FT8 RX startup on hardware under concurrent Wi-Fi/HTTP + QMX preflight. The experimental mirror code was removed from main; T033-T035 remain the accepted WebFS/Wi-Fi baseline.
- T037 is COMPLETE: UI-first ADV FT8 startup is hardware validated. MiniFT8 remains fully usable with QMX absent, late attachment transitions through existing CAT sync into RX without restarting the app, and already-connected startup/cleanup behavior remains intact.
- ADV is a validated embedded MiniFT8 RX/TX deployment target. Physical QMX CAT TX, RX recovery, the red TX-status separator, RX message colors, and RX/TX paging shortcuts have been exercised on real ADV/QMX hardware.

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
T033         COMPLETE — WebFS read-only SoftAP/browser, hardware validated
T034         COMPLETE — safe WebFS mutations, hardware validated
T035         COMPLETE — persistent WebFS SoftAP credentials, hardware validated
T036         BREAK — mirrored web front panel not accepted; code reverted from main
T037         COMPLETE — UI-first FT8 startup with late QMX attach, hardware validated
T049         COMPLETE — ADV color status: TX separator red/white; CQ green; reply-to-me red path software-proven
T050         COMPLETE — bare `;` / `.` RX/TX page shortcuts on ADV
T051         COMPLETE — 50-row ADV resident console scrollback foundation; current 5-row trigger is Ctrl+;/Ctrl+. via T076
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

V2-compatible `RTYYMMDD.txt` RxTxLog is implemented and hardware validated through T022-T026; `rxtx_log` defaults ON and is configurable in `setting.txt`. It captured the first completed two-way QSO and the responder RR73 regression evidence.

## CW application baseline

The original `apps/keyer` development track (K0-K6 / T038-T040) is retired.
It proved runtime ELF, Digital I/O, GPIO KeyIn/KeyOut, sidetone transport and UI
experiments; its implementation/build/tests have now been removed.

The accepted CW application is `minicw`, derived from the pinned standalone
Mini-CW V1.2 behavior:

```text
T042 foundation / platform boundary   COMPLETE
T043 continuous tone audio            COMPLETE — paddle + M1 clean/no-pop
T044A persistence                     COMPLETE
T045 UI / I/O cleanup                 COMPLETE
boundary audit                        COMPLETE
T046 full callsign lookup             COMPLETE
T047 color UI                         COMPLETE
T048 transcript + safe note mode      COMPLETE
```

Current Mini-CW behavior includes the fixed 20x7 header, clean CW audio,
persistent settings, full callsign lookup, white/green/cyan presentation with
2-pixel green separator, and daily compact transcript logging with dual-quote
safe note mode.

Canonical current documents:

```text
docs/MiniCW/migration.md
docs/MiniCW/baseline-audit.md
apps/minicw/README.md
```

Historical T038/T040 task packets remain under `docs/project/codex/`; the old `docs/keyer/README.md` and `keyer.elf` project were removed with the retired implementation.

## Configuration ownership

```text
/flash/minishell/setting.txt  MiniShell resident settings
/flash/minishell/alias.txt    MiniShell resident command aliases
/flash/<app>/setting.txt      application-owned configuration/deployment settings
```

WebFS uses the resident `setting.txt`, which also supports `startup=cmd1;cmd2` for one-shot boot sequencing through the normal shell/alias path. Future operator-facing
MiniShell settings should share that file rather than grow separate documented
configuration paths. Resident brightness control was explicitly dropped after hardware testing.

MiniFT8 currently retains its established path:

```text
/flash/ft8/setting.txt
```

MiniFT8 uses `/flash/ft8/setting.txt` as its application-owned configuration file.

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

Linux CTest covers shell/runtime behavior, service contracts, filesystem/resource policy, terminal input, audio transport, Digital I/O, Mini-CW, MiniFT8 UI/runtime behavior, AutoSeq, and focused FT8 DSP/protocol tests.

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

docs/MiniCW/migration.md
docs/MiniCW/baseline-audit.md
apps/minicw/README.md
platform/adv/README.md
```


## Field-use milestone — 2026-09-21

MiniShell + MiniFT8 + Mini-CW is considered operational for this development
round. The hardware-accepted code baseline entering the documentation-coherence
pass is:

```text
48909320646f3a989c812a88052b0b7420a32dc7
```

Later commits in this milestone pass are documentation-only and do not change the
accepted runtime behavior.

Accepted ADV behavior includes MiniFT8 RX/TX, Mini-CW, WebFS, filesystem tools,
50-row resident console scrollback, color/separator Display support, and normal
foreground app return to `M$>`.

Known operational caveat: ADV currently tears down the ESP32-S3 USB Host/UAC/CDC
session when the final QMX user exits. A subsequent fresh QMX enumeration can
fail on real hardware and may require power-cycling QMX. Linux hosts keep the
device enumerated in the kernel, so restarting MiniFT8 there normally only
reopens ALSA/CDC handles. Treat persistent/reusable ADV QMX USB-host ownership as
future architecture work, not a blocker for this milestone.

## Mini-CW migration track

Pinned golden reference:

```text
wcheng95/Mini-CW
3bfbf169b7c2d49a1be3e9a4c80f945edb32033e
```

Standalone Mini-CW V1.2 hardware comparison is clean for both paddle and automatic M1.
The migration goal is specifically Mini-CW **Keyer-mode** behavior under MiniShell,
not full standalone-firmware parity. Battery/sleep and USB-MSC remain MiniShell
responsibilities; trainer/lesson/word/callsign/plaintext modes are not migrated.
See `docs/MiniCW/migration.md`.

T042-T045 completed the Mini-CW Keyer-mode migration and ownership cleanup. The pinned Mini-CW continuous-audio behavior is preserved under MiniShell Tone, with both paddle and automatic M1 hardware-validated clean and pop-free. UTC is read through MiniShell Time/Location; GPS remains outside Mini-CW scope. T046-T048 are completed feature additions from the audited baseline.

T046 Mini-CW callsign lookup     COMPLETE — full V1.2 table, <base-call>: <name> row, audio/header/clearing/exit all hardware accepted
T047 Mini-CW color UI            COMPLETE — V1.2 white/green/cyan text and 2-pixel green separator accepted on ADV; audio and lookup remain clean
T048 Mini-CW transcript log      COMPLETE — compact daily transcript, whitespace-aware truncation, dual-quote **note** mode and clean audio all accepted on ADV
T049 MiniFT8 color status         COMPLETE — ADV red/white TX separator and green CQ rows hardware validated; reply-to-me red software-proven and pending only opportunistic on-air observation
T050 MiniFT8 RX/TX page keys     COMPLETE — ADV bare ; previous-page and . next-page shortcuts accepted on RX/TX
T051 ADV console scrollback       COMPLETE — 50-row resident history hardware accepted; current five-row trigger is Ctrl+;/Ctrl+. via T076
