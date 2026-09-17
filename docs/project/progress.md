# MiniShell Progress Log

## Current baseline

- Maintained targets: Linux Mint and Cardputer ADV / ESP32-S3.
- Linux runtime applications use `.so`; ADV supports compiled-in applications plus runtime external `.elf` loading.
- ADV application resolution is `compiled-in > /flash/apps/<app>.elf > /sd/apps/<app>.elf`.
- Public MiniShell API generation is v3 and exposes App, System, Console, Memory, Filesystem, Time/Location, Display, Input, Audio, and Digital I/O.
- Architecture cleanup C0-C4 is complete.
- K1 runtime ELF, K2 Digital I/O, K3 Keyer engine, and K4 GPIO KeyIn/KeyOut are complete.
- MiniFT8 AutoSeq AS-0..AS-8 is complete.
- MiniFT8 live Linux/QMX RX is working continuously across consecutive FT8 slots.
- MiniFT8 V2-style ADIF and Field Day Cabrillo logging are implemented through MiniShell APIs.

## Current MiniFT8 baseline

```text
QMX USB-UAC 48 kHz / S24_3LE / stereo
        |
        v
Linux ALSA capture worker
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
AutoSeq      MiniShell FS logging
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

Working command:

```text
M$> ft8 --profile adv --rx alsa:hw:2,0
```

Current MiniFT8 status:

```text
RX-0..RX-7   COMPLETE
RX-8         COMPLETE — live QMX ALSA, V2 timing, continuous capture
AS-0..AS-8   COMPLETE
LOG-1        COMPLETE — daily ADIF + Field Day Cabrillo
physical TX  NEXT MAJOR PRODUCTION BOUNDARY
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

Logging eligibility originates in pure AutoSeq state, but file/time I/O remains in `app_controller` using MiniShell Filesystem and Time/Location APIs. Writes are acknowledged to AutoSeq only after successful persistence, preserving V2 duplicate-prevention semantics.

V2's optional `RTYYMMDD.txt` traffic log remains intentionally unported until V3 exposes the corresponding `rxtx_log` setting.

## Keyer baseline

```text
K0 architecture gate          COMPLETE
K1 ADV runtime ELF proof      COMPLETE
K2 MiniShell Digital I/O      COMPLETE
K3 portable Keyer engine      COMPLETE
K4 GPIO KeyIn/KeyOut          COMPLETE
K5 sidetone                   NEXT KEYER STAGE
```

K4 keeps orchestration in `app_controller`:

```text
config_service
      |
      v
app_controller
   /      |       \
  v       v        v
keyin  keyer_engine keyout
  |                  |
  v                  v
MiniShell Digital I/O
```

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

The Linux QMX work reinforced this rule: ALSA/UAC mechanics remain platform-side; MiniFT8 sees only canonical MiniShell Audio.

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
