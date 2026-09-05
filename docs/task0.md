# Task 0 - USB Serial/JTAG Shell + Runtime ELF

Task 0 proves the smallest useful vertical slice of MiniShell on the M5Stack
Tab5 / ESP32-P4.

## Status

**Complete and hardware-validated on M5Stack Tab5 / ESP32-P4 rev v1.3.**

The validated system used ESP-IDF 5.5.4, `espressif/elf_loader` 1.3.3, the
ESP32-P4 USB Serial/JTAG controller for the interactive console, and a FAT32
microSD card mounted at `/sd`.

## Scope

Included:

- ESP32-P4 built-in USB Serial/JTAG as the user console
- a resident `M$` shell
- microSD mounted and owned by MiniShell
- `help`, `status`, `ls`, and `exec` built-ins
- unknown-command lookup under `/sd/apps`
- runtime loading of a native RISC-V ELF app
- one resident MiniShell API entry point: `mini_api_get()`
- normal app return to the shell without rebooting

Explicitly excluded from Task 0:

- display and touch
- Wi-Fi / ESP32-C6
- USB host
- audio
- RTC
- internal flash filesystem
- shell history, pipes, redirection, jobs, or POSIX compatibility

## USB Serial/JTAG Console

ESP32-P4 includes a fixed-function USB Serial/JTAG controller. On Tab5, use the
board's USB connection that exposes this controller to the host. The same
connection can provide:

- `idf.py flash`
- `idf.py monitor`
- interactive MiniShell stdin/stdout
- JTAG debugging through OpenOCD/GDB

On Linux, the serial function normally appears as `/dev/ttyACM*` or under
`/dev/serial/by-id/`.

Task 0 configures USB Serial/JTAG as the **primary** ESP-IDF console, not merely a
secondary log output. MiniShell also installs the interrupt-driven USB
Serial/JTAG driver and switches the VFS to that driver so blocking shell input
yields normally instead of busy-polling CPU0.

## SD Card Layout

Task 0 expects:

```text
/sd/
└── apps/
    └── hello.elf
```

A missing or broken SD card is intentionally **not fatal**. MiniShell should
still reach `M$` so the platform can be diagnosed independently of an app.

## Build MiniShell

From the repository root, with ESP-IDF exported:

```bash
idf.py set-target esp32p4
idf.py build
```

For the current Tab5 ESP32-P4 rev v1.x hardware, `sdkconfig.defaults` selects the
pre-v3 P4 target required by ESP-IDF 5.5.

Flash and monitor through USB Serial/JTAG:

```bash
idf.py flash monitor
```

Or specify the port explicitly when needed:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Managed component dependencies are intentionally limited for Task 0:

- `espressif/m5stack_tab5_noglib` for Tab5 hardware support without a graphical library
- `espressif/elf_loader` for native ELF loading

## Build hello.elf

The example app is a separate ESP-IDF ELF project:

```bash
cd examples/hello
idf.py set-target esp32p4
idf.py elf
```

The expected output is:

```text
build/hello.app.elf
```

Copy it to the SD card as:

```text
/apps/hello.elf
```

The app source includes only the public MiniShell header. It does not include
ESP-IDF or Tab5 headers.

## Task 0 Runtime Binding

Task 0 validates this model:

```text
hello.elf
   |
   | unresolved runtime import: mini_api_get()
   v
Espressif ELF loader symbol resolver
   |
   v
resident MiniShell mini_api_get()
   |
   v
versioned mini_api_t service table
   |
   v
api->system->write(...)
```

This remains deliberately provisional. The runtime binding works on real
hardware, but the ABI is not frozen simply because Task 0 passed.

## Validated Session

Representative hardware session:

```text
MiniShell Task 0
sd: mounted at /sd
type 'help' for commands
M$ status
platform : M5Stack Tab5 / ESP32-P4
console  : OK - USB Serial/JTAG (interrupt-driven)
sd       : OK
app path : /sd/apps
M$ hello
app: loading /sd/apps/hello.elf
Hello from a MiniShell ELF app.
M$
```

The `hello` application was launched repeatedly in succession and returned to
`M$` each time without rebooting or an obvious leak.

A separate negative test also confirmed that MiniShell can reach the shell when
SD initialization fails, preserving the diagnostic-first design goal.

## Success Criteria

Task 0 hardware validation passed all intended criteria:

1. USB Serial/JTAG is usable as an interactive console. **PASS**
2. MiniShell reaches `M$` even when SD initialization fails. **PASS**
3. `ls /sd/apps` lists `hello.elf` when the card is valid. **PASS**
4. Typing `hello` loads the ELF without rebooting. **PASS**
5. `hello.elf` successfully calls the resident MiniShell runtime API. **PASS**
6. The ELF returns and is unloaded. **PASS**
7. `M$` works again immediately. **PASS**
8. `hello` can be run repeatedly without rebooting or an obvious leak. **PASS**

Task 0 is therefore complete.

## Observed Non-Blocking Warnings

Two warnings were observed during validation but did not prevent Task 0 from
passing:

```text
ldo: The voltage value 0 is out of the recommended range [500, 2700]
M5Stack Tab5: Warning: Long filenames on SD card are disabled in menuconfig!
```

The ELF loader also reports padding before one ELF segment. The application
still loads, runs, and returns normally, so this is not a Task 0 blocker. These
items may be investigated independently if they become relevant to later
milestones.
