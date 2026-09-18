# ADV Audio TX latency probe

External diagnostic only; not registered as a resident application. Opens the
public `speaker` endpoint at 48 kHz/S16/mono and submits silent 48-frame blocks.
Each phase has 100 warm-up calls and 5,000 measured calls: A requests 20 ms,
B requests `MINI_WAIT_NONE`. No console writes occur within measured intervals.
Counts exclude warm-up. Partial means 1..47 accepted frames; threshold counts
are strictly greater than the stated microseconds. Each call requests a new
48-frame block; partial writes are reported rather than retried within that call.
TIMEOUT continues sampling. Other transport errors end the phase, abort/close,
and return failure; zero-progress OK is counted and cannot cause an infinite loop.
There is no latency pass/fail threshold. An internally stuck provider call cannot
be interrupted by this synchronous diagnostic.

Build on the inspected development machine:

```bash
cd /home/wei/projects/MiniShell
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv/elf_apps/audio_tx_probe elf
xtensa-esp32s3-elf-readelf -rW platform/adv/elf_apps/audio_tx_probe/build/audio_tx_probe.app.elf
xtensa-esp32s3-elf-readelf --dyn-syms -W platform/adv/elf_apps/audio_tx_probe/build/audio_tx_probe.app.elf
```

The `.rela.plt` table must have exactly one `R_XTENSA_JMP_SLOT`: `mini_api_get`.
It must also be the only named undefined dynamic symbol. The ESP ELF packaging
can produce a readelf “no .dynamic section in the dynamic segment” diagnostic;
inspect the displayed relocation/dynamic-symbol tables, not that diagnostic alone.
The build pins `espressif/elf_loader` 1.3.3; first resolution may need registry access.

Host statistics/cleanup regression:

```bash
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -Iinclude \
  platform/adv/elf_apps/audio_tx_probe/probe_host_test.c -o /tmp/T009-probe-host-test
/tmp/T009-probe-host-test
```

Install with the ADV microSD mounted on this host (enter its actual mount path):

```bash
cd /home/wei/projects/MiniShell
read -r -p 'ADV microSD mount path: ' T009_SD_MOUNT
mountpoint -q "$T009_SD_MOUNT" && \
  mkdir -p "$T009_SD_MOUNT/apps" && \
  cp platform/adv/elf_apps/audio_tx_probe/build/audio_tx_probe.app.elf \
     "$T009_SD_MOUNT/apps/audio_tx_probe.elf" && \
  sync "$T009_SD_MOUNT/apps/audio_tx_probe.elf"
```

Safely eject the card and insert it into ADV running the current MiniShell
speaker-capable firmware. No probe firmware flashing is needed. In MiniShell:

```text
ls /sd/apps
cp /sd/apps/audio_tx_probe.elf /flash/apps/audio_tx_probe.elf
audio_tx_probe
```

The explicit copy updates `/flash/apps`, which takes precedence over `/sd/apps`.
The file name is `audio_tx_probe.elf` on device; the build artifact is
`audio_tx_probe.app.elf`. Retain both complete phase reports and any error lines,
then paste them into the T009 architect hardware-result section. The probe returns
to MiniShell. After collecting evidence it can be removed with:

```text
rm /flash/apps/audio_tx_probe.elf
rm /sd/apps/audio_tx_probe.elf
```

Hardware output is still required. Local source inspection found that ADV ignores
the requested timeout and the resolved codec passes 1000 ms to I2S. The task packet
records exact paths/functions; the probe measures actual latency without changing
that provider or Keyer scheduling.
