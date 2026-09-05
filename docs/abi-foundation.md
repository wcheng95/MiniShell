# MiniShell ABI Foundation

Status: **design contract for Task 1; not yet frozen ABI v1**

MiniShell exists to provide a small, reusable, platform-neutral ABI between MCU
applications and hardware/platform implementations. The ABI is therefore the
main architectural product of MiniShell, not merely an implementation detail.

Task 1 defines and validates six basic service ABIs before real applications are
added:

```text
system
memory
filesystem
time
display
input
```

The first implementation target is M5Stack Tab5 / ESP32-P4, but the public
contract must remain suitable for other MCU-like systems, including much smaller
MCUs and future platforms such as Ox64 running without Linux.

## 1. Layering

```text
application
    |
    | MiniShell ABI
    v
MiniShell resident services
    |
    | platform-private boundary
    v
ESP-IDF / RTOS / bare-metal support / drivers
    |
    v
hardware
```

Applications normally know nothing about ESP-IDF, FATFS, USB Serial/JTAG,
M5Stack BSP objects, RTOS handles, or hardware registers.

## 2. Top-Level API

The intended Task 1 top-level shape is:

```c
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;

    const mini_system_api_t  *system;
    const mini_memory_api_t  *memory;
    const mini_fs_api_t      *fs;
    const mini_time_api_t    *time;
    const mini_display_api_t *display;
    const mini_input_api_t   *input;
} mini_api_t;
```

Task 0 already proved runtime binding through `mini_api_get()`.

`system` is the minimum resident service. Other service pointers may be NULL on a
platform that does not provide that capability. The Tab5 Task 1 reference
implementation should provide all six.

An application must check `mini_api_t.struct_size` before accessing fields added
after the version it was compiled against.

## 3. Compatibility Rules

These rules apply to every public MiniShell ABI.

### 3.1 Append-only tables

Public function tables begin with `struct_size` and grow only by appending new
fields/function pointers.

Existing fields must not be reordered, removed, repurposed, or have their
meaning changed incompatibly.

### 3.2 Extensible data structures

Caller-owned output structures begin with:

```c
uint32_t struct_size;
```

The caller zero-initializes the structure and sets `struct_size` to the size it
knows. MiniShell writes only fields that fit in that size. Future fields are
appended.

### 3.3 Optional capabilities

A missing top-level service is represented by a NULL service pointer.

Services that contain distinct capability families, especially display and
input, expose capability bits and optional sub-API pointers. Unknown capability
bits are ignored by older applications.

### 3.4 ABI-owned types

Public signatures use MiniShell-owned fixed-width integer types, opaque
MiniShell handles, plain pointers to application memory, and documented UTF-8
strings.

Do not expose SDK/RTOS/backend-private types.

C `enum` layout is not relied upon across the ABI. Public constants are carried
in fixed-width integer fields.

### 3.5 Stable numeric meanings

Once a public flag, key code, capability bit, result code, or type value is
assigned in a stable ABI, that numeric meaning is never reused for something
else.

### 3.6 Synchronous first

Task 1 APIs are synchronous unless explicitly documented otherwise. Future
asynchronous operations are added as new functions or sub-APIs rather than
changing existing synchronous semantics.

### 3.7 Ownership is explicit

Every pointer or opaque handle crossing the ABI has a documented owner and
lifetime. MiniShell-managed resources acquired by an app are associated with the
current app context and reclaimed during normal app teardown where practical.

### 3.8 Source portability, architecture-specific binaries

The same application source should be rebuildable against the same MiniShell API
on RV32, RV64, Xtensa, ARM, or other supported targets. A compiled ELF is not
expected to be binary-compatible across CPU architectures.

## 4. Shared Result Codes

`mini_result_t` is shared by all services. Zero means success and negative values
mean errors.

The existing filesystem result set remains the base. Task 1 adds only errors
required by the new basic services:

```c
#define MINI_OK                  ((mini_result_t)  0)
#define MINI_ERR_INVALID         ((mini_result_t) -1)
#define MINI_ERR_NOT_FOUND       ((mini_result_t) -2)
#define MINI_ERR_EXISTS          ((mini_result_t) -3)
#define MINI_ERR_BAD_HANDLE      ((mini_result_t) -4)
#define MINI_ERR_ACCESS          ((mini_result_t) -5)
#define MINI_ERR_IO              ((mini_result_t) -6)
#define MINI_ERR_NO_SPACE        ((mini_result_t) -7)
#define MINI_ERR_TOO_MANY_OPEN   ((mini_result_t) -8)
#define MINI_ERR_NAME_TOO_LONG   ((mini_result_t) -9)
#define MINI_ERR_UNSUPPORTED     ((mini_result_t)-10)
#define MINI_ERR_NOT_DIR         ((mini_result_t)-11)
#define MINI_ERR_IS_DIR          ((mini_result_t)-12)
#define MINI_ERR_NO_MEMORY       ((mini_result_t)-13)
#define MINI_ERR_NOT_READY       ((mini_result_t)-14)
```

Additional result codes are added only when a real ABI requires a distinct
portable meaning.

## 5. System ABI v0

The system service is the smallest mandatory MiniShell service.

Task 0 already proved:

```c
typedef struct {
    uint32_t struct_size;
    void (*write)(const char *text);
} mini_system_api_t;
```

`write()` is a **diagnostic/system text sink**, not the application's display
surface. On Tab5 Task 0 it is routed to USB Serial/JTAG. Another platform may
route it elsewhere.

Semantics:

- `text` is a NUL-terminated UTF-8 byte string
- NULL is invalid application behavior
- the call is best-effort and synchronous
- the function does not grant ownership of a display, terminal, UART, or USB
  device
- applications must not use `system.write()` as a substitute for the display ABI
  when building a user interface

The v0 table remains deliberately tiny. Future system information or lifecycle
operations may be appended when justified.

## 6. Memory ABI v0

The memory service gives applications dynamic memory without exposing the
platform heap implementation.

Proposed v0 types:

```c
typedef struct {
    uint32_t struct_size;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t largest_free_block;
} mini_memory_info_t;
```

Service table:

```c
typedef struct {
    uint32_t struct_size;

    mini_result_t (*alloc)(
        uint32_t size,
        void **out_ptr);

    mini_result_t (*realloc)(
        void *ptr,
        uint32_t new_size,
        void **out_ptr);

    mini_result_t (*free)(
        void *ptr);

    mini_result_t (*get_info)(
        mini_memory_info_t *out_info);
} mini_memory_api_t;
```

Semantics:

- successful allocations are aligned suitably for normal objects required by the
  target C ABI; special DMA/over-aligned/executable memory is not promised
- `alloc(0, ...)` is invalid
- `*out_ptr` is set to NULL before an allocation attempt
- allocation failure returns `MINI_ERR_NO_MEMORY`
- `realloc()` requires an app-owned MiniShell allocation and `new_size > 0`
- if `realloc()` fails, the original allocation remains valid and owned by the
  application
- `free(NULL)` is a successful no-op
- freeing a pointer not owned by the current application returns
  `MINI_ERR_BAD_HANDLE`
- remaining MiniShell-managed allocations are reclaimed during normal app exit
- memory-class flags, aligned allocation, DMA allocation, and shared memory are
  deferred and may be appended later

`get_info()` reports a portable summary, not backend heap structures. Exact
numbers may change between calls.

## 7. Filesystem ABI v0

Filesystem ABI v0 is defined in detail in `docs/app-abi.md`.

The agreed operation set is:

```text
open
close
read
write
seek
sync
stat
```

Core properties:

- absolute MiniShell paths such as `/sd/...`
- opaque `mini_file_t` handles
- no FATFS, libc `FILE *`, or ESP-IDF types
- partial reads/writes are legal
- EOF is `MINI_OK` plus zero bytes read
- app-owned file handles are reclaimed during normal app teardown
- directory operations and convenience functions such as `readline()` remain
  deferred

The filesystem ABI remains a logical file namespace. Storage hardware itself is
below the ABI.

## 8. Time ABI v0

Time has two fundamentally different meanings and the ABI must keep them
separate:

```text
monotonic time    elapsed-time measurement; never adjusted by RTC/network time
UTC time          wall-clock time; may be unavailable or invalid
```

Capabilities:

```c
#define MINI_TIME_CAP_MONOTONIC  (1ull << 0)
#define MINI_TIME_CAP_UTC        (1ull << 1)
#define MINI_TIME_CAP_SLEEP      (1ull << 2)
```

UTC structure:

```c
typedef struct {
    uint32_t struct_size;
    int64_t  unix_seconds;
    uint32_t nanoseconds;
    uint32_t flags;
} mini_utc_time_t;
```

`unix_seconds` is seconds since 1970-01-01 00:00:00 UTC. `nanoseconds` is the
fractional part and may be zero on low-resolution hardware. No local timezone is
part of the ABI.

Service table:

```c
typedef struct {
    uint32_t struct_size;
    uint64_t capabilities;

    mini_result_t (*monotonic_us)(uint64_t *out_us);
    mini_result_t (*utc_get)(mini_utc_time_t *out_time);
    mini_result_t (*sleep_ms)(uint32_t milliseconds);
} mini_time_api_t;
```

Semantics:

- monotonic time has an unspecified zero point, normally related to boot
- it must not go backward during normal execution
- changing or correcting UTC must not affect monotonic time
- `utc_get()` returns `MINI_ERR_NOT_READY` when UTC capability exists but no valid
  wall-clock value is currently available
- an unsupported operation returns `MINI_ERR_UNSUPPORTED`
- `sleep_ms()` is a cooperative/blocking delay for the current app; it is not a
  hard real-time delay guarantee
- alarms, periodic timers, high-resolution sleep-until operations, and UTC set
  operations are deferred

## 9. Display ABI v0

Display is an output service. It is deliberately **not** called console because
a console is a higher-level text interaction model that combines output and
input.

The display service is designed as an extensible container of display
capabilities.

Initial capability:

```c
#define MINI_DISPLAY_CAP_TEXT  (1ull << 0)
```

Future capabilities may include graphics, color, framebuffers, partial update,
or other display models without changing the existing text contract.

Top-level display table:

```c
typedef struct {
    uint32_t struct_size;
    uint64_t capabilities;
    const mini_text_display_api_t *text;
    /* future fields are appended, e.g. graphics */
} mini_display_api_t;
```

### 9.1 Text display v0

Text-display information:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t columns;
    uint32_t rows;
} mini_text_display_info_t;
```

Text sub-API:

```c
typedef struct {
    uint32_t struct_size;

    mini_result_t (*get_info)(mini_text_display_info_t *out_info);
    mini_result_t (*clear)(void);
    mini_result_t (*write_at)(
        uint32_t row,
        uint32_t column,
        const char *utf8,
        uint32_t byte_count);
} mini_text_display_api_t;
```

Semantics:

- coordinates are zero-based character-cell coordinates
- the API has no implicit cursor state
- `clear()` clears the logical text surface
- `write_at()` draws text beginning at the supplied cell
- the first version guarantees rendering of 7-bit ASCII
- input is UTF-8 bytes so future implementations may support broader Unicode
  without changing the function signature
- unsupported non-ASCII characters may be rendered as a replacement glyph
- text extending beyond the right/bottom boundary is clipped
- a starting row/column outside the surface returns `MINI_ERR_INVALID`
- terminal scrolling, ANSI interpretation, and cursor semantics are not part of
  the display ABI

A future graphics API is appended as another optional sub-API, for example:

```text
mini_display_api_t
    text      -> existing text API
    graphics  -> future graphics API
```

Old applications continue using `text` unchanged.

## 10. Input ABI v0

Input is an input-device service, separate from display. The initial ABI supports
**text-oriented input** without assuming a particular physical keyboard.

Initial capability:

```c
#define MINI_INPUT_CAP_TEXT  (1ull << 0)
```

Future capability families can be appended independently:

```text
pointer / mouse / touch pointer
raw keyboard
buttons
encoder
joystick/game controls
```

Top-level input table:

```c
typedef struct {
    uint32_t struct_size;
    uint64_t capabilities;
    const mini_text_input_api_t *text;
    /* future fields are appended, e.g. pointer, buttons, raw keyboard */
} mini_input_api_t;
```

### 10.1 Text input v0

Text input normalizes physical keyboard or terminal input into logical events.
It is not a raw scan-code/HID interface.

Event kinds:

```c
#define MINI_TEXT_EVENT_CHAR  1u
#define MINI_TEXT_EVENT_KEY   2u
```

Modifiers:

```c
#define MINI_MOD_SHIFT  (1u << 0)
#define MINI_MOD_CTRL   (1u << 1)
#define MINI_MOD_ALT    (1u << 2)
```

Initial special-key values include:

```text
UP DOWN LEFT RIGHT
ENTER BACKSPACE DELETE ESCAPE TAB
HOME END PAGE_UP PAGE_DOWN
```

The exact numeric assignments are made in the public header when implementation
begins and must remain stable after ABI v1 is frozen.

Event structure:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t codepoint;
    uint32_t key;
    uint32_t modifiers;
} mini_text_input_event_t;
```

For `MINI_TEXT_EVENT_CHAR`:

- `codepoint` contains the Unicode scalar value
- `key` is zero
- modifiers describe the logical chord when known
- example: Ctrl-S may be represented as codepoint `s` plus `MINI_MOD_CTRL`

For `MINI_TEXT_EVENT_KEY`:

- `key` contains a MiniShell special-key value
- `codepoint` is zero
- arrows and navigation/editing keys use this form

Text sub-API:

```c
typedef struct {
    uint32_t struct_size;

    mini_result_t (*read)(mini_text_input_event_t *out_event);
} mini_text_input_api_t;
```

`read()` is blocking in v0 and returns the next normalized logical event.
Non-blocking polling, timeout reads, key-release events, and raw physical keyboard
state are deferred and can be appended without changing `read()`.

### 10.2 Future pointer input

Pointer input is intentionally not forced into the text event structure.
Instead, a later ABI can append:

```text
mini_input_api_t
    text     -> existing text events
    pointer  -> future pointer events
```

A pointer event can then define coordinates, buttons, wheel motion, touch
pressure, or other pointer-specific fields without bloating or changing the text
input ABI. Older applications remain binary-compatible because they only know the
prefix of `mini_input_api_t` that contains `text`.

## 11. Why Console Is Not a Basic ABI

A console is useful, but it is a composition rather than a fundamental hardware
boundary:

```text
console / terminal
      |
      +-- text display/output
      `-- text input
```

USB serial may implement a console without using a physical display at all. A
Tab5 LCD and touch panel may provide display/input without behaving like a
terminal.

MiniShell may later provide a reusable terminal/console library or service built
on top of lower-level capabilities, but `console` is not one of the six basic
Task 1 ABIs.

`system.write()` remains a diagnostic output path and is separate from both the
display ABI and a future console abstraction.

## 12. Task 1 ABI Tests

Each ABI is validated through a separately built ELF using only public MiniShell
headers:

```text
abi_system.elf
abi_memory.elf
abi_fs.elf
abi_time.elf
abi_display.elf
abi_input.elf
```

The tests should verify both normal behavior and important errors, then be run
repeatedly to catch resource leaks or lifecycle damage.

A service is not considered proven merely because its resident implementation
compiles. The decisive test is the complete path:

```text
ELF test
  -> public MiniShell ABI
  -> resident service
  -> platform implementation
  -> real hardware/backend
```

Only after these six boundaries have survived implementation and hardware tests
should MiniShell begin depending on them from real user applications.
