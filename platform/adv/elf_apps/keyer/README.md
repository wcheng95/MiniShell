# ADV Keyer runtime ELF — K4

This project builds the portable Keyer application as a Cardputer ADV runtime ELF.

K4 validates only Digital I/O integration. Sidetone/audio and the full field UI are intentionally later stages.

## Build

```bash
cd ~/projects/MiniShell/platform/adv/elf_apps/keyer
idf.py fullclean
idf.py elf
```

Output:

```text
build/keyer.app.elf
```

Install the exact bytes as either:

```text
/sd/apps/keyer.elf
/flash/apps/keyer.elf
```

MiniShell resolution remains:

```text
compiled-in > /flash/apps > /sd/apps
```

## Default K4 wiring

KeyIn:

```text
Cardputer ADV G13  -> paddle tip
Cardputer ADV G15  -> paddle ring
input mode          Paddle
active state        low
pull-ups            enabled
```

KeyOut:

```text
Cardputer ADV G3   -> output tip
Cardputer ADV G6   -> output ring
output mode         SK
active state        low
electrical mode     open drain
idle/released       high-Z via output level 1
```

K4 opens both KeyOut lines in the released state before enabling output drive and releases both before application exit/close.

## Optional settings file

K4 reads:

```text
/flash/keyer/setting.txt
```

If the file does not exist, the defaults above are used. K6 will add the settings UI/persistence workflow; K4 only needs read-only deployment settings.

Example:

```text
wpm=20
key_in=Paddle
paddle=IambicA
key_out=SK
key_in_tip_gpio=13
key_in_ring_gpio=15
key_out_tip_gpio=3
key_out_ring_gpio=6
```

Accepted `key_in` values:

```text
Paddle
PaddleR
SK-T
SK-R
```

Accepted `paddle` values:

```text
IambicA
IambicB
Bug
```

Accepted `key_out` values:

```text
Paddle
PaddleR
SK
SK-M
Off
```

## First hardware test

Start Keyer:

```text
M$> keyer
```

Expected startup:

```text
keyer K4: using default settings
keyer K4: ready; q or ESC exits
```

(or the first line reports that `/flash/keyer/setting.txt` was loaded).

With a paddle connected to G13/G15 and ground, send a few characters. Decoded characters are written to the MiniShell console. There is intentionally no sidetone in K4.

For KeyOut, use a meter/logic analyzer or a suitably isolated radio key/PTT input with the required external pull-up. In default `SK` mode, both G3 and G6 are released while idle and pulled low while the engine is key-down.

Press `q` or ESC to exit. Both KeyOut lines must be released before MiniShell returns to `M$>`.

Do not use `SK-M` for the first test: by Mini-CW compatibility, `SK-M` intentionally holds the ring output low while the application is running and releases it only on shutdown.
