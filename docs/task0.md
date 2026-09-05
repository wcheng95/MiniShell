# Task 0 - UART Shell + Runtime ELF

Task 0 proves the smallest useful vertical slice of MiniShell on the M5Stack
Tab5 / ESP32-P4.

## Scope

Included:

- physical UART0 as the only user console
- a resident `$>` shell
- microSD mounted and owned by MiniShell
- `help`, `status`, `ls`, and `exec` built-ins
- unknown-command lookup under `/sd/apps`
- runtime loading of a native RISC-V ELF app
- one resident MiniShell API entry point: `mini_api_get()`
- normal app return to the shell without rebooting

Explicitly excluded from Task 0:

- display and touch
- Tab5 keyboard
- Wi-Fi / ESP32-C6
- USB host
- audio
- RTC
- internal flash filesystem
- shell history, pipes, redirection, jobs, or POSIX compatibility

## UART Console

MiniShell uses the Tab5 M5-Bus UART0 signals:

```text
Tab5 G37 / TXD0  -> USB-UART RX
Tab5 G38 / RXD0  <- USB-UART TX
Tab5 GND          -> USB-UART GND
```

Use a 3.3 V TTL USB-UART adapter. Do not connect the adapter's VCC unless you
intentionally want it involved in powering the board.

Console format:

```text
115200 baud
8 data bits
no parity
1 stop bit
```

On Linux, for example:

```bash
picocom -b 115200 /dev/ttyUSB0
```

The shell performs its own character echo and backspace handling, so local echo
should normally be disabled in the terminal program.

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

Flash the Tab5 using the normal ESP32-P4 programming connection:

```bash
idf.py flash
```

The runtime console is the separate physical UART0 connection described above.

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
console  : UART0 115200 8N1, TX=G37 RX=G38
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

1. UART is usable as an interactive console.
2. MiniShell reaches `$>` even when SD initialization fails.
3. `ls /sd/apps` lists `hello.elf` when the card is valid.
4. Typing `hello` loads the ELF without rebooting.
5. `hello.elf` successfully calls the resident MiniShell runtime API.
6. The ELF returns and is unloaded.
7. `$>` works again immediately.
8. `hello` can be run repeatedly without rebooting or leaking obvious memory.

The framework is not considered validated until this hardware test passes.
