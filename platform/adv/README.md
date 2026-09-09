# MiniShell Cardputer ADV backend

This directory is the ESP-IDF firmware composition for the Cardputer ADV backend.

## A1 scope

A1 proves the second real MiniShell backend with the smallest useful vertical slice:

```text
ESP-IDF app_main()
      |
      v
minishell_run()
      |
      +-- ADV platform identity
      +-- explicit resource policy
      +-- private resident console
      +-- compiled-in app registry
      +-- portable hello app
```

The A1 resident console uses the ESP32-S3 USB Serial/JTAG console. This is deliberately a bring-up implementation. Cardputer display and keyboard become the real resident/UI providers in A2; applications still never use the private console directly.

Public services available in A1:

```text
System.write    ready
Memory          unavailable until A2
Filesystem      unavailable until A3
Time/Location   unavailable until A3
Display         unavailable until A2
Input           unavailable until A2
Audio           unavailable until a later RX/TX slice
```

## Flash layout

The initial 8 MiB flash layout is:

```text
0x010000 .. 0x5FFFFF   factory application   0x5F0000 bytes
0x600000 .. 0x7FFFFF   /flash LittleFS       2 MiB
```

The 2 MiB LittleFS size is provisional. A1 reserves the partition but does not mount it. NVS is not used. The removable SD card is also not brought up in A1.

The MiniShell global memory/storage quotas are left at `0` on ADV A1, meaning no additional MiniShell-wide quota beyond the physical/backend resource limits. In particular, the 2 MiB internal LittleFS size must not become a global cap on future `/sd` storage.

## Build

ESP-IDF v5.5.1 is the current reference.

```bash
cd ~/projects/MiniShell/platform/adv
idf.py set-target esp32s3
idf.py build
```

Flash and open the temporary USB Serial/JTAG console:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Use the actual `/dev/ttyACM*` device on the host.

## A1 hardware check

At the prompt:

```text
M$> status
platform : adv
system   : ready
...

M$> apps
hello

M$> hello
Hello from MiniShell.
M$>
```

`hello` is the existing portable `apps/hello/hello.c` source. The ADV build renames its normal `main()` symbol privately while statically linking it; the application source does not know whether Linux loads it dynamically or ADV selects it from a compiled-in registry.

Returning from `hello` must return cleanly to `M$>`. The shell `exit` command ends the MiniShell task on this embedded bring-up build; reset the device to start again.

## Next

A2 replaces the temporary serial console path with the Cardputer-facing implementation and adds the real public System/Memory/Display/Input providers. The agreed ADV Display surface is 20 columns x 7 rows; that work does not belong in A1.
