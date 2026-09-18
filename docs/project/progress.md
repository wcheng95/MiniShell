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
- With ADV RAM/USB-host viability now proven, MiniFT8 completion returns to Linux; the first real MiniFT8-V3 QSO is targeted on Linux/QMX. ADV remains a validated embedded deployment target, not the active TX-development platform.

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
TxIntent / simulated TX lifecycle
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
desktop presentation, WAV fixtures, or alternate devices.

The packaged ADV application defaults bare `ft8` to `uac:qmx`. Real hardware now passes USB-host/UAC bring-up and live on-air decode at 240 MHz. The accepted ADV engine profile remains `time_osr=2, freq_osr=1`. A temporary `freq_osr=2` experiment remained alive but reduced free/largest heap to about 58.8/31.0 KiB. Decode time/candidate load were not measured, so the lack of observed messages is inconclusive; its higher RAM/compute cost is deferred to a later performance study. It is not the production baseline.

QMX post-enumeration unplug/replug recovery is not a T017 requirement: real QMX hardware can return a short device descriptor on a second enumeration, matching the practical MiniFT8-V2 limitation. Initial startup with QMX absent followed by the first attachment remains the required disconnected-start behavior.

Current MiniFT8 status:

```text
RX-0..RX-7   COMPLETE
RX-8         COMPLETE — live QMX ALSA, V2 timing, continuous capture
T017         COMPLETE — ADV QMX USB-host RX live decode + lifecycle + usbmsc hardware validation
T018         COMPLETE — Linux bare ft8 -> ADV presentation + live QMX RX defaults
AS-0..AS-8   COMPLETE
LOG-1        COMPLETE — daily ADIF + Field Day Cabrillo
physical TX  NEXT MAJOR PRODUCTION BOUNDARY — Linux/QMX first QSO target
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

V2's optional `RTYYMMDD.txt` traffic log remains intentionally unported until V3 exposes the corresponding `rxtx_log` setting.

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
/flash/config.txt          MiniShell-owned resident/platform configuration
/flash/<app>/setting.txt   application-owned configuration/deployment settings
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
