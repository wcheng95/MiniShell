# MiniShell Cardputer ADV backend

This directory is the ESP-IDF firmware composition for the Cardputer ADV backend.

## Current stage: A3

A1 proved the portable MiniShell runtime on real Cardputer ADV hardware. A2 added the real Cardputer display/keyboard plus System and Memory providers. A3 adds persistent Filesystem and Time/Location services while keeping the device fully usable without an SD card.

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
             System        USB/debug diagnostic sink
             Console       resident text console + USB mirror
             Memory        ESP-IDF heap
             Display       20 x 7 logical text surface
             Input         normalized TCA8418 key events
             Filesystem    /flash LittleFS + optional /sd FATFS
             Time/Location monotonic + session UTC + persistent default location
```

Applications never include M5, ESP-IDF, TCA8418, GPIO, I2C, SPI, FATFS, LittleFS, or display-driver headers.

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

On ADV, Console output joins the resident text-console stream and is mirrored to USB. Therefore utilities such as `date`, `ls`, and `cat` print on the Cardputer screen; after they return, the next `M$>` continues below their output. System diagnostics remain USB-only so they cannot overwrite a foreground application's Display UI.

The Cardputer ADV keyboard uses the proven V2 wiring:

```text
I2C0
SDA       GPIO8
SCL       GPIO9
TCA8418   0x34 @ 400 kHz
matrix    7 x 8
```

## A3 storage

Canonical ADV storage policy:

```text
/flash    LittleFS on internal flash, initially 2 MiB (provisional)
/sd       FATFS on removable MicroSD, optional
NVS       not used
```

`/flash` must work whether or not an SD card exists. The initial 2 MiB LittleFS allocation is provisional and is not part of the public API.

The optional SD card uses the proven Cardputer/V2 wiring:

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

ESP-IDF v5.5.x defaults FATFS to 8.3-only filenames. The ADV configuration explicitly enables heap-backed long filenames, a 255-character LFN limit, and UTF-8 API encoding so `/sd` can satisfy the MiniShell Filesystem filename contract instead of exposing an 8.3-only backend.

## A3 time policy

Cardputer ADV has no time source enabled in A3. To avoid pretending that flash persistence is an RTC, every boot starts from this deterministic UTC anchor:

```text
2026-09-01 06:00:00 UTC
```

MiniShell advances that anchor using monotonic time while powered on. The user may correct UTC for the current session with:

```text
M$> date YYYY-MM-DD HH:MM:SS
```

That manual correction is **session-only**. A power cycle returns to the fixed default. RTC and GPS providers are intentionally deferred; they can replace the bootstrap source later through the existing Time/Location ownership model without changing applications.

Default geographic location remains persistent as an ordinary file under `/flash/minishell/`. No NVS is used.

## A3 probe

The compiled-in `a3probe` uses only the public MiniShell API. It tests:

```text
/flash LittleFS create/write/read/sync/rename/directory/delete
optional /sd FATFS using the same operations when mounted
monotonic time and sleep
session UTC availability
persistent default-location set/get/restore
Display/Input foreground lifecycle
```

With no SD card it should show:

```text
MiniShell A3 probe
LittleFS PASS
File/Dir PASS
TimeLoc PASS
UTC session PASS
SD absent OK
q/Enter exits
```

With a working SD card, line 6 becomes:

```text
SD FATFS PASS
```

After exit the USB diagnostic sink prints:

```text
a3_probe: PASS
```

A3 also packages the portable `date`, `ls`, and `cat` utilities on ADV for direct service-level checks. These utilities use the public Console service for user-facing output.

## Flash layout

The current 8 MiB flash layout is:

```text
0x010000 .. 0x5FFFFF   factory application   0x5F0000 bytes
0x600000 .. 0x7FFFFF   /flash LittleFS       2 MiB
```

## Runtime stack

ADV currently executes compiled-in applications on ESP-IDF's main task. A3 filesystem depth exposed the default stack as too small, so the ADV configuration explicitly uses an 8 KiB main-task stack and enables the FreeRTOS stack-overflow canary. This is an implementation choice, not an application API promise. A later application-lifecycle design may give foreground apps their own managed execution task/stack.

## Build and flash

ESP-IDF v5.5.x is the current reference family.

```bash
cd ~/projects/MiniShell/platform/adv
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Use the actual `/dev/ttyACM*` device on the host.

## Current A3 validation status

Passed on real ADV:

```text
SD-less boot
/flash LittleFS
file/directory API probe
monotonic Time/Location baseline
Display/Input foreground probe
a3_probe: PASS
```

Remaining A3 hardware check:

```text
optional MicroSD mounts as /sd
A3 probe reports SD FATFS PASS
ls / shows /flash and /sd when SD is present
```
