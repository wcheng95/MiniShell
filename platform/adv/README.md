# MiniShell Cardputer ADV backend

This directory is the ESP-IDF firmware composition for the Cardputer ADV backend.

## Current stage: P2 baseline + ADV storage/utilities increment

A1 proved the portable MiniShell runtime on real Cardputer ADV hardware. A2 added the real Cardputer display/keyboard plus System and Memory providers. A3 added Filesystem and Time/Location. P2 packages the real MiniFT8 `ft8` application into the ADV static registry using the same MiniFT8 sources as Linux.

The current ADV increment keeps MiniFT8 compiled in, expands the portable shell utility set, and changes internal `/flash` storage from LittleFS to FATFS over ESP-IDF wear levelling. This prepares the storage model for a later `usbmsc` USB Mass Storage utility and for future small/medium ELF-loaded applications.

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
             shell utilities + nano
             ft8 -> same MiniFT8 sources, ADV presentation default
```

Applications never include M5, ESP-IDF, TCA8418, GPIO, I2C, SPI, FATFS, wear-levelling, or display-driver headers. Those details remain backend-owned.

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
```

The filesystem utilities and `nano` are the existing portable MiniShell applications; ADV only supplies composition wrappers and the platform services they consume. This keeps the same application code usable on Linux and future MiniShell backends.

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

```text
ADV display hardware     adv_display
ADV keyboard/TCA8418     adv_keyboard
shared ADV I2C bus       adv_i2c
application allocations  portable Memory service + adv_memory provider
internal flash storage   adv_filesystem -> FATFS + wear levelling
MicroSD SPI/FATFS        adv_filesystem -> ESP-IDF SDSPI/FATFS
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

The internal FATFS volume uses the existing `flash` data partition and mounts at `/flash`. The MiniShell-visible path and 2 MiB partition size are unchanged; only the on-flash filesystem format changes.

### LittleFS -> FATFS migration

This change is intentionally format-incompatible with the earlier ADV LittleFS `/flash` volume. Existing `/flash` files must be copied off before installing this firmware if they need to be preserved.

On first boot after the change, the FATFS mount is allowed to format the `flash` partition when the previous LittleFS contents cannot be mounted. During this development stage there is no automatic LittleFS-to-FATFS data migration.

The optional SD card uses:

```text
SCK   GPIO40
MISO  GPIO39
MOSI  GPIO14
CS    GPIO12
SPI   SPI2_HOST
```

The display uses a different SPI host, so the SD bus remains owned independently by `adv_filesystem`.

At the MiniShell Filesystem root:

```text
/flash    always present after a successful internal FATFS mount
/sd       present only when the card mounted successfully
```

An absent or invalid SD card is not a boot failure. Same-filesystem `mv` can use the Filesystem rename operation. Cross-filesystem rename remains unsupported at the backend boundary; use `cp` followed by `rm` to move a file between `/flash` and `/sd` today.

ESP-IDF v5.5.x defaults FATFS to 8.3-only filenames. ADV explicitly enables heap-backed long filenames, a 255-character LFN limit, and UTF-8 API encoding for both volumes so they satisfy the MiniShell Filesystem filename contract.

The previous A3 hardware validation proved the MiniShell Filesystem behavior using LittleFS `/flash` plus FATFS `/sd`. The new internal FATFS path requires a fresh physical ADV storage validation after flashing.

## Planned `usbmsc` utility

The USB Mass Storage utility is named `usbmsc`.

Planned command interface:

```text
usbmsc             # same as: usbmsc all
usbmsc all         # export all available supported volumes
usbmsc flash       # export /flash only
usbmsc sd          # export /sd only
```

`all` is the default. If `/sd` is absent, `usbmsc` exports only the available `/flash` volume.

The ownership rule is strict:

```text
normal operation
    MiniShell owns mounted /flash and /sd

usbmsc starts
    selected volume(s) are closed/unmounted locally
    USB MSC becomes the exclusive owner

usbmsc exits / host releases storage
    USB MSC ownership ends
    MiniShell remounts the selected volume(s)
```

MiniShell and the USB host must never have writable filesystem ownership of the same volume at the same time.

`usbmsc` is intentionally **not implemented in this storage/utility increment**. First validate FATFS `/flash` and the expanded portable utility set on real ADV hardware; then add USB MSC as a separate platform-resource milestone with explicit mount/unmount and ownership tests.

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

## Runtime stack

The resident MiniShell runtime/shell uses an 8 KiB ESP-IDF main-task stack with the FreeRTOS stack-overflow canary enabled. Foreground applications execute on a separate ADV-managed FreeRTOS task with a 16 KiB stack. The task size is an ADV implementation choice, not part of the portable MiniShell application API.

## Build and flash

ESP-IDF v5.5.x is the current reference family.

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
ADV static registry includes utilities   PASS required
ESP-IDF firmware build                    PASS required
FT8 Reference                             gated by FT8-sensitive changes
```

Real ADV storage/utility check for this increment:

```text
M$> apps
    -> includes cp mv rm mkdir rmdir nano ft8

M$> mkdir /flash/test
M$> nano /flash/test/note.txt
M$> cp /flash/test/note.txt /flash/test/copy.txt
M$> mv /flash/test/copy.txt /flash/test/moved.txt
M$> rm /flash/test/moved.txt
M$> rm /flash/test/note.txt
M$> rmdir /flash/test

M$> ft8
    -> launches the 20 x 7 ADV presentation
q
    -> returns cleanly to M$>
```

After this passes on hardware, `usbmsc` can be implemented against the proven FATFS storage ownership model.
