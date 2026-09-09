# MiniShell Cardputer ADV backend

This directory is the ESP-IDF firmware composition for the Cardputer ADV backend.

## Current stage: A2

A1 proved that the portable MiniShell runtime and compiled-in application lifecycle run on real Cardputer ADV hardware. A2 replaces terminal-only interaction with Cardputer display/keyboard providers while preserving USB Serial/JTAG as a recovery/debug path during bring-up.

```text
ESP-IDF app_main()
      |
      v
minishell_run()
      |
      +-- private resident shell
      |      display + Cardputer keyboard
      |      USB Serial/JTAG mirror/fallback
      |
      +-- public MiniShell services
             System   -> USB/debug diagnostic sink
             Memory   -> ESP-IDF heap
             Display  -> 20 x 7 logical text surface
             Input    -> normalized TCA8418 key events
```

Applications never include M5, ESP-IDF, TCA8418, GPIO, I2C, or display-driver headers.

## M5 library policy

A2 deliberately reuses the proven MiniFT8-V2 display behavior rather than redesigning Cardputer hardware support. The build pins the V2-era library versions:

```text
M5GFX      0.2.17
M5Unified  0.2.11
```

M5 libraries are ADV-backend implementation dependencies, not MiniShell application dependencies. Replacing or further decoupling them is not an A2 goal unless a real problem requires it.

The critical V2 audio-ownership rule is preserved: A2 initializes the display through `M5.Display.begin()` and **does not call `M5.begin()`**. A2 does not initialize, configure, or claim microphone, speaker, codec, or I2S resources. Those resources remain available for the later Audio backend.

## Hardware ownership

```text
ADV display hardware     adv_display
ADV keyboard/TCA8418     adv_keyboard
shared ADV I2C bus       adv_i2c
application allocations  portable Memory service + adv_memory provider
```

The private resident shell and public Display/Input services share these backend owners; they do not initialize the hardware independently.

The ADV keyboard uses the proven V2 wiring and matrix mapping:

```text
I2C0
SDA       GPIO8
SCL       GPIO9
TCA8418   0x34 @ 400 kHz
matrix    7 x 8
```

## Display

The public Display provider reports:

```text
physical panel       240 x 135
logical surface      20 columns x 7 rows
cell target          12 x 16 glyph
row allocation       19 pixels
physical gap         2 pixels after logical row 0
capability            text only
inverse attribute    supported
```

Pixel coordinates and the physical gap remain backend-private.

The resident shell uses the same display owner as a scrolling 20 x 7 console. When an application uses the public Display API, the application owns the logical screen until it returns; the next resident-shell output restores shell-console presentation.

## System is not Display

`System.write()` remains a diagnostic sink. On ADV A2 it writes to USB/debug output only.

```text
System.write()                 USB/debug diagnostic sink
private resident shell output  Cardputer screen + USB mirror
Display API                    Cardputer application UI surface
```

This separation prevents application diagnostics from overwriting an application UI.

## Input

The Cardputer keyboard is normalized into the public logical Input API. Current ADV conventions include:

```text
Fn + ;          Up
Fn + ,          Left
Fn + .          Down
Fn + /          Right
Fn + `          Escape
Fn + Backspace  Delete
```

Shift, Ctrl, Alt, Fn, and Opt state is represented in the modifier mask. Because Cardputer applications may use those keys as actions by themselves, standalone modifier presses also have logical SPECIAL-key values.

The Input service still owns the portable logical event queue; the ADV keyboard provider only produces normalized events.

## Memory

The ADV Memory provider uses the ESP-IDF 8-bit-capable heap. The portable Memory service remains responsible for application allocation accounting, lifecycle cleanup, and any MiniShell resource limit.

`get_info()` can report both free heap and largest free block from the real device.

## A2 probe

The development firmware contains a compiled-in portable app named `probe`. Its source is `tests/apps/a2_probe.c`; it uses only `mini_api_get()`.

It checks:

```text
Memory alloc/realloc/accounting/free
Display geometry/text/inverse/present
Input delivery through the public Input API
```

Run it from the Cardputer shell:

```text
M$> probe
```

The screen asks for `x`. After `x`, it shows `Input PASS`; press Enter to return to `M$>`.

## Flash layout

The initial 8 MiB flash layout remains:

```text
0x010000 .. 0x5FFFFF   factory application   0x5F0000 bytes
0x600000 .. 0x7FFFFF   /flash LittleFS       2 MiB
```

The 2 MiB LittleFS size is provisional. A2 still does not mount it. NVS is not used for MiniShell persistence. Removable SD storage is A3.

## Build and flash

ESP-IDF v5.5.1 is the current reference.

```bash
cd ~/projects/MiniShell/platform/adv
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Use the actual `/dev/ttyACM*` device on the host.

## A2 hardware check

Using the **Cardputer screen and keyboard**, verify:

```text
M$> status
platform : adv
system   : ready
memory   : ready
display  : ready
input    : ready

M$> apps
hello
probe

M$> probe
```

`time` may also report ready because A2 provides the monotonic clock and sleep primitive needed by Input timeouts. UTC/location capabilities and persistence remain A3 work.

USB Serial/JTAG remains available as a mirrored shell/debug path during A2 bring-up.

## Next

After the display/keyboard and public `probe` checks pass on real hardware, close A2 and proceed to A3: LittleFS `/flash`, optional FATFS `/sd`, and Time/Location persistence.
