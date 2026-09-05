# Task 0 - USB Serial/JTAG Shell + Runtime ELF

Task 0 proves the smallest useful vertical slice of MiniShell on the M5Stack
Tab5 / ESP32-P4.

## Scope

Included:

- ESP32-P4 built-in USB Serial/JTAG as the user console
- a resident `$>` shell
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
secondary log output. This is required for shell input as well as output.

## SD Card Layout

Task 0 expects:

```text
/sd/
└── apps/
    └── hello.elf
```

A missing or broken SD card is intentionally **not fatal**. MiniShell should
still reach `$>` so the platform can be diagnosed independently of an app.

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

Task 0 tests this model:

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

This is deliberately provisional. If the real ESP32-P4 ELF toolchain exposes a
better binding mechanism, the ABI document may be adjusted before ABI v1 is
frozen.

## Expected Session

With a working SD card:

```text
MiniShell Task 0
sd: mounted at /sd
type 'help' for commands
$> status
platform : M5Stack Tab5 / ESP32-P4
console  : OK - USB Serial/JTAG
sd       : OK
app path : /sd/apps
$> ls /sd/apps
hello.elf
$> hello
app: loading /sd/apps/hello.elf
Hello from a MiniShell ELF app.
$>
```

With no usable SD card, success means MiniShell still becomes diagnostic:

```text
MiniShell Task 0
sd: mount failed: ...
type 'help' for commands
$> status
...
sd       : ...
$>
```

## Success Criteria

Task 0 is complete only after it is verified on real Tab5 hardware that:

1. USB Serial/JTAG is usable as an interactive console.
2. MiniShell reaches `$>` even when SD initialization fails.
3. `ls /sd/apps` lists `hello.elf` when the card is valid.
4. Typing `hello` loads the ELF without rebooting.
5. `hello.elf` successfully calls the resident MiniShell runtime API.
6. The ELF returns and is unloaded.
7. `$>` works again immediately.
8. `hello` can be run repeatedly without rebooting or leaking obvious memory.

The framework is not considered fully validated until this hardware test passes.
