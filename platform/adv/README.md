# MiniShell Cardputer ADV backend

This directory is the ESP-IDF firmware composition for the Cardputer ADV backend.

## Current stage: P2 baseline + ADV USB MSC complete

A1 proved the portable MiniShell runtime on real Cardputer ADV hardware. A2 added the real Cardputer display/keyboard plus System and Memory providers. A3 added Filesystem and Time/Location. P2 packages the real MiniFT8 `ft8` application into the ADV static registry using the same MiniFT8 sources as Linux.

The current ADV storage baseline uses FATFS for both internal `/flash` and optional `/sd`. The `usbmsc` utility adds ADV-only USB Mass Storage handoff so either or both FAT media can be exposed temporarily to a host PC without violating filesystem ownership. This is primarily useful for moving test data such as MiniFT8 WAV files onto the Cardputer before live Audio providers exist.

```text
ESP-IDF app_main()
      |
      v
minishell_run()
      |
      +-- private resident shell
      |      Cardputer display + keyboard
      |      USB Serial/JTAG mirror/fallback
      |
      +-- public MiniShell services
      |      System        USB/debug diagnostic sink
      |      Console       resident text console + USB mirror
      |      Memory        ESP-IDF heap
      |      Display       20 x 7 logical text surface
      |      Input         normalized TCA8418 key events
      |      Filesystem    /flash FATFS + optional /sd FATFS
      |      Time/Location monotonic + session UTC + persistent default location
      |
      `-- compiled-in apps
             shell utilities + nano + usbmsc
             ft8 -> same MiniFT8 sources, ADV presentation default
```

Applications never include M5, ESP-IDF, TCA8418, GPIO, I2C, SPI, FATFS, wear-levelling, or display-driver headers. Those details remain backend-owned. `usbmsc` is deliberately an ADV platform utility because raw-media and USB-device ownership are backend concerns rather than portable application services.

## Compiled-in applications

The ADV static registry currently includes:

```text
hello
probe
a3probe
date
free
ls
cat
cp
mv
rm
mkdir
rmdir
nano
ft8
usbmsc
```

The filesystem utilities and `nano` are the existing portable MiniShell applications; ADV only supplies composition wrappers and the platform services they consume. `usbmsc` is different: it is intentionally platform-specific because it temporarily transfers raw storage ownership and the ESP32-S3 USB device peripheral.

MiniFT8 remains deliberately compiled into the firmware. Future small/medium utilities may instead be distributed as ELF applications once the ADV ELF loader is implemented.

`df` remains out of the ADV registry for now because the current public `Filesystem.space()` implementation is quota-based and ADV intentionally has no global storage quota across `/flash` and `/sd`. Proper per-volume capacity reporting should be defined separately rather than reporting misleading numbers.

## MiniFT8 P2 composition

The ADV firmware compiles the existing MiniFT8 sources directly. The only ADV-specific application composition file is:

```text
platform/adv/main/ft8_static.c
```

It renames the portable application entry and supplies `ADV` as the default presentation. MiniFT8 itself does not ask which platform it is running on.

```text
Linux composition             default DESKTOP
ADV static composition        default ADV
same ft8 controller/UI/config/scheduler sources
```

The ADV resident shell therefore exposes:

```text
M$> apps
...
ft8

M$> ft8
```

The normal ADV launch uses the 20 x 7 MiniFT8 presentation. Presentation is not stored in `/flash/ft8/station.txt`; the O-screen station `Profile` is a different concept.

## M5 library policy

The ADV backend deliberately reuses proven MiniFT8-V2 Cardputer behavior where useful. The build pins:

```text
M5GFX      0.2.17
M5Unified  0.2.11
```

M5 libraries are backend implementation dependencies, not application dependencies. The critical audio-ownership rule remains: ADV initializes the display through `M5.Display.begin()` and does **not** call `M5.begin()`. Display/keyboard/storage bring-up does not claim microphone, speaker, codec, or I2S resources.

## Hardware ownership

Normal operation:

```text
ADV display hardware     adv_display
ADV keyboard/TCA8418     adv_keyboard
shared ADV I2C bus       adv_i2c
application allocations  portable Memory service + adv_memory provider
internal flash storage   adv_filesystem -> FATFS + wear levelling
MicroSD SPI/FATFS        adv_filesystem -> ESP-IDF SDSPI/FATFS
USB Serial/JTAG          adv_console
```

During `usbmsc`, ownership changes temporarily:

```text
Cardputer display/keys   remain local control path
MiniShell FATFS          unmounted/quiesced
selected raw media       usbmsc -> TinyUSB MSC LUN(s)
ESP32-S3 USB device      usbmsc -> TinyUSB while MSC is active
```

The private resident shell and public Display/Input services share the same backend owners; they do not initialize hardware independently.

## Display, Console, and Input

```text
physical panel       240 x 135
logical surface      20 columns x 7 rows
cell target          12 x 16 glyph
row allocation       19 pixels
physical gap         2 pixels after logical row 0
capability            text only
inverse attribute    supported
```

Pixel coordinates remain backend-private.

Output domains are intentionally distinct:

```text
Console.write()   user-facing line-oriented utility output
Display           application-owned/full-screen UI
System.write()    USB/debug diagnostics
```

On ADV, Console output joins the resident text-console stream and is mirrored to USB. Utilities such as `date`, `ls`, `cat`, `cp`, `mv`, `rm`, `mkdir`, and `rmdir` therefore use the normal MiniShell utility interface. `nano` uses the MiniShell Display, Input, Filesystem, and Memory APIs and remains platform-independent. System diagnostics remain USB-only so they cannot overwrite a foreground application's Display UI.

`usbmsc` is an exception to normal console availability: the ESP32-S3 USB path is temporarily reassigned to TinyUSB MSC, so the USB Serial/JTAG monitor may disappear while the utility is running. The Cardputer display and keyboard remain active and are the authoritative local control path. After the host ejects the exported drive(s), press `Q` or Esc on the Cardputer to stop MSC, restore normal storage ownership, and return to MiniShell.

The Cardputer ADV keyboard uses the proven wiring:

```text
I2C0
SDA       GPIO8
SCL       GPIO9
TCA8418   0x34 @ 400 kHz
matrix    7 x 8
```

## Storage

Canonical ADV storage policy:

```text
/flash    FATFS on internal SPI flash through wear levelling, 2 MiB
/sd       FATFS on removable MicroSD, optional
NVS       not used
```

The internal FATFS volume uses the existing `flash` data partition and mounts at `/flash`. The MiniShell-visible path and 2 MiB partition size are unchanged.

### LittleFS -> FATFS migration

The FATFS storage baseline is format-incompatible with the earlier ADV LittleFS `/flash` volume. Existing `/flash` files must be copied off before installing that earlier migration if they need to be preserved.

On first boot with the FATFS baseline, the FATFS mount is allowed to format the `flash` partition when previous LittleFS contents cannot be mounted. During this development stage there is no automatic LittleFS-to-FATFS data migration.

The optional SD card uses:

```text
SCK   GPIO40
MISO  GPIO39
MOSI  GPIO14
CS    GPIO12
SPI   SPI2_HOST
```

The display uses a different SPI host, so the SD bus remains owned independently by `adv_filesystem` during normal operation.

At the MiniShell Filesystem root:

```text
/flash    always present after a successful internal FATFS mount
/sd       present only when the card mounted successfully
```

An absent or invalid SD card is not a boot failure. Same-filesystem `mv` can use the Filesystem rename operation. Cross-filesystem rename remains unsupported at the backend boundary; use `cp` followed by `rm` to move a file between `/flash` and `/sd` today.

ESP-IDF v5.5.x defaults FATFS to 8.3-only filenames. ADV explicitly enables heap-backed long filenames, a 255-character LFN limit, and UTF-8 API encoding for both volumes so they satisfy the MiniShell Filesystem filename contract.

ADV deliberately disables `CONFIG_FATFS_PER_FILE_CACHE`. ESP-IDF's default per-file cache allocates a sector cache inside every FATFS file slot; with eight slots on each mounted volume this consumed exactly 64 KiB of avoidable idle heap in the measured two-volume configuration. Shared-cache/tiny mode preserves the eight-file limit while using substantially less resident RAM. `adv_config_guard.c` rejects builds that accidentally re-enable per-file caching.

Hardware RAM audit after this change:

```text
/flash only:
  shell-ready heap free    336288 B (328.4 KiB)
  largest free block       286720 B (280.0 KiB)

/flash + /sd mounted:
  shell-ready heap free    324700 B (317.1 KiB)
  largest free block       278528 B (272.0 KiB)
```

With both volumes mounted, filesystem initialization now consumes 22076 B (21.6 KiB), down from 87612 B (85.6 KiB) before the shared-cache change. The recovered heap is exactly 65536 B (64.0 KiB).

## `usbmsc` utility

Command interface:

```text
usbmsc             # same as: usbmsc all
usbmsc all         # export all available supported volumes
usbmsc flash       # export /flash only
usbmsc sd          # export /sd only
```

`all` is the default. If `/sd` is absent, `usbmsc all` exports only `/flash`. When both are available, `all` presents two MSC LUNs.

The ownership rule is strict:

```text
normal operation
    MiniShell owns mounted /flash and /sd

usbmsc starts
    local FAT filesystems are unmounted/quiesced
    raw media for selected target(s) are initialized for MSC
    USB MSC becomes the exclusive owner of selected media

host work
    copy/delete files through the PC
    eject/unmount the USB drive(s) on the host

user presses Q or Esc on Cardputer
    TinyUSB MSC stops
    raw-media ownership ends
    MiniShell remounts normal FAT filesystems
    application returns to M$>
```

The first implementation conservatively quiesces both MiniShell FAT volumes for the whole foreground `usbmsc` session even when only one target is exported. Only the selected medium is exposed to the host. This keeps the ownership rule simple; no other foreground application can run concurrently anyway.

MiniShell and the USB host must never have writable filesystem ownership of the same volume at the same time. Always eject/unmount the host drive before pressing `Q` to return storage to MiniShell.

The build pins `espressif/esp_tinyusb` and enables MSC with a 4096-byte transfer buffer. The 4096-byte size is chosen to satisfy the existing internal-flash wear-levelling sector requirement without changing the FATFS/WL format already validated on hardware.

Hardware validation on real Cardputer ADV is complete:

```text
usbmsc sd                         PASS
usbmsc flash                      PASS
usbmsc all (two LUNs)             PASS
return to MiniShell               PASS
/flash remount after usbmsc       PASS
/sd remount after usbmsc          PASS
```

## Time policy — A3 complete

Cardputer ADV has no RTC/GPS source enabled yet. Every boot starts from the deterministic UTC anchor:

```text
2026-09-01 06:00:00 UTC
```

MiniShell advances that anchor using monotonic time while powered on. The user may correct UTC for the current session with:

```text
M$> date YYYY-MM-DD HH:MM:SS
```

That correction is session-only. RTC and GPS providers are intentionally deferred. Default geographic location remains persistent as an ordinary file under `/flash/minishell/`; no NVS is used.

## Flash layout

The current 8 MiB flash layout is:

```text
0x010000 .. 0x5FFFFF   factory application   0x5F0000 bytes
0x600000 .. 0x7FFFFF   /flash FATFS          2 MiB
```

## Runtime stack and RAM guardrails

The resident MiniShell runtime/shell uses an 8 KiB ESP-IDF main-task stack with the FreeRTOS stack-overflow canary enabled. Foreground applications execute on a separate ADV-managed FreeRTOS task with a 16 KiB stack. The task size is an ADV implementation choice, not part of the portable MiniShell application API.

ADV CI reports the ESP-IDF size summary and allocated ELF sections on every firmware build so static `.data`/`.bss` growth remains visible. Hardware free-heap measurements remain the authority for boot-time allocations made by mounted filesystems, drivers, and tasks.

The ADV config guard also enforces the ESP32-S3 target and the RAM-efficient FATFS shared-cache configuration.

## Build and flash

ESP-IDF v5.5.x is the current reference family. The project defaults to the ESP32-S3 target in `sdkconfig.defaults` so regenerating `sdkconfig` cannot silently fall back to classic ESP32.

```bash
cd ~/projects/MiniShell/platform/adv
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Use the actual `/dev/ttyACM*` device on the host.

## Validation

Software checks:

```text
Linux build/tests                         PASS required
ADV static registry includes usbmsc      PASS required
ESP-IDF firmware build                    PASS required
FT8 Reference                             gated by FT8-sensitive changes
```

Real ADV baseline validated:

```text
/flash FATFS                 PASS
/sd FATFS                    PASS
shared-cache FATFS           PASS
nano / ls / rm               PASS
MiniFT8 launch/navigation    PASS
MiniFT8 clean exit/memory    PASS
usbmsc sd                    PASS
usbmsc flash                 PASS
usbmsc all                   PASS
filesystem remount           PASS
```

The next ADV milestone is deterministic WAV-backed MiniFT8 RX through the public MiniShell Audio contract, reusing the existing RX-7 core unchanged.
