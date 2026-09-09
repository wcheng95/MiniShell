# MiniShell Cardputer ADV backend

This directory is the ESP-IDF firmware composition for the Cardputer ADV backend.

## Current stage: P2

A1 proved the portable MiniShell runtime on real Cardputer ADV hardware. A2 added the real Cardputer display/keyboard plus System and Memory providers. A3 added Filesystem and Time/Location. P2 now packages the real MiniFT8 `ft8` application into the ADV static registry using the same MiniFT8 sources as Linux.

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
      |      Filesystem    /flash LittleFS + optional /sd FATFS
      |      Time/Location monotonic + session UTC + persistent default location
      |
      `-- compiled-in apps
             hello / probes / utilities
             ft8 -> same MiniFT8 sources, ADV presentation default
```

Applications never include M5, ESP-IDF, TCA8418, GPIO, I2C, SPI, FATFS, LittleFS, or display-driver headers.

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

M5 libraries are backend implementation dependencies, not application dependencies. The critical V2 audio-ownership rule remains: ADV initializes the display through `M5.Display.begin()` and does **not** call `M5.begin()`. Display/keyboard/storage bring-up does not claim microphone, speaker, codec, or I2S resources.

## Hardware ownership

```text
ADV display hardware     adv_display
ADV keyboard/TCA8418     adv_keyboard
shared ADV I2C bus       adv_i2c
application allocations  portable Memory service + adv_memory provider
internal flash storage   adv_filesystem -> LittleFS
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

On ADV, Console output joins the resident text-console stream and is mirrored to USB. Utilities such as `date`, `ls`, and `cat` therefore print on the Cardputer screen. System diagnostics remain USB-only so they cannot overwrite a foreground application's Display UI.

The Cardputer ADV keyboard uses the proven V2 wiring:

```text
I2C0
SDA       GPIO8
SCL       GPIO9
TCA8418   0x34 @ 400 kHz
matrix    7 x 8
```

## Storage — A3 complete

Canonical ADV storage policy:

```text
/flash    LittleFS on internal flash, initially 2 MiB (provisional)
/sd       FATFS on removable MicroSD, optional
NVS       not used
```

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
/flash    always present after a successful A3 mount
/sd       present only when the card mounted successfully
```

An absent or invalid SD card is not a boot failure. Cross-filesystem rename is unsupported; portable copy logic can move data between volumes when needed.

ESP-IDF v5.5.x defaults FATFS to 8.3-only filenames. ADV explicitly enables heap-backed long filenames, a 255-character LFN limit, and UTF-8 API encoding so `/sd` satisfies the MiniShell Filesystem filename contract.

Real-hardware A3 validation passed with both internal LittleFS and removable FATFS, including `a3probe: PASS` and long-filename directory enumeration.

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
0x600000 .. 0x7FFFFF   /flash LittleFS       2 MiB
```

## Runtime stack

ADV currently executes compiled-in applications on ESP-IDF's main task. A3 filesystem depth exposed the ESP-IDF default stack as too small, so ADV explicitly uses an 8 KiB main-task stack and enables the FreeRTOS stack-overflow canary. This is an implementation choice, not an application API promise. A later application-lifecycle design may give foreground apps their own managed execution task/stack.

## Build and flash

ESP-IDF v5.5.x is the current reference family.

```bash
cd ~/projects/MiniShell/platform/adv
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Use the actual `/dev/ttyACM*` device on the host.

## P2 validation

Software checks:

```text
Linux MiniFT8 DESKTOP/ADV tests       PASS required
ADV static registry includes ft8      PASS required
ESP-IDF firmware build                PASS required
```

Real ADV check:

```text
M$> apps          -> includes ft8
M$> ft8           -> launches the 20 x 7 ADV presentation
V -> 5            -> Presentation: ADV / UI: text 20x7
q                 -> returns cleanly to M$>
```

After that, V1 compares Linux + ADV presentation against ADV + ADV presentation before RX-1B resumes.
