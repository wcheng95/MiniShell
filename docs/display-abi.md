# MiniShell Display ABI v0

Status: **Task 1 design contract; provisional until implementation and hardware ABI tests pass**

## 1. Purpose

The Display ABI provides applications with a platform-neutral logical display
surface without exposing LCD/e-paper controllers, framebuffers, M5Stack display
objects, ESP-IDF drivers, pixel formats, or panel update details.

```text
application
    |
    | MiniShell Display ABI
    v
MiniShell display service
    |
    | logical display surface
    v
platform implementation
    |
    +-- TFT
    +-- e-paper
    +-- framebuffer-backed display
    `-- future display hardware
```

Display is output-only. Input is a separate ABI. A console/terminal is a higher-level
composition of display/output and input and is not the fundamental display boundary.

## 2. V0 scope

Display ABI v0 provides one primary logical display with an initial text capability.

```text
display
|-- capabilities
|-- text
|   |-- get_info()
|   |-- clear()
|   |-- clear_at()
|   `-- write_at()
`-- present()
```

Deferred capabilities include:

```text
graphics/pixel drawing
colors and styles
font selection
images
framebuffer access
partial-update policy controls
brightness/backlight
orientation
layers
animation
multiple displays
display power control
```

These are added later through append-only extension or optional sub-APIs without
changing the v0 text contract.

## 3. Top-level service table

```c
#define MINI_DISPLAY_CAP_TEXT  (1ull << 0)

typedef struct {
    uint32_t struct_size;
    uint64_t capabilities;

    const mini_text_display_api_t *text;

    mini_result_t (*present)(void);

    /* Future fields are appended here, for example:
       const mini_graphics_display_api_t *graphics;
    */
} mini_display_api_t;
```

The ordering above is part of the v0 prefix. Future capability pointers are appended
after `present()`; they are never inserted into the existing prefix.

If the display service itself is unavailable, the top-level `mini_api_t.display`
pointer is NULL. If the display exists but text is unsupported, the text capability
bit is clear and `text` is NULL.

## 4. Text display information

```c
typedef struct {
    uint32_t struct_size;
    uint32_t columns;
    uint32_t rows;
} mini_text_display_info_t;
```

The caller zero-initializes the structure and sets `struct_size` before calling
`get_info()`.

`columns` and `rows` define the logical text grid.

Valid text coordinates are:

```text
row     0 .. rows - 1
column  0 .. columns - 1
```

Therefore the application obtains the maximum usable row and column from
`get_info()`; separate `max_row` and `max_column` fields are intentionally not
provided.

The logical character geometry is independent of physical pixels. MiniShell decides
how character cells map to fonts, pixel dimensions, or another backend representation.

## 5. Text sub-API

```c
typedef struct {
    uint32_t struct_size;

    mini_result_t (*get_info)(
        mini_text_display_info_t *out_info);

    mini_result_t (*clear)(void);

    mini_result_t (*clear_at)(
        uint32_t row,
        uint32_t column,
        uint32_t rows,
        uint32_t columns);

    mini_result_t (*write_at)(
        uint32_t row,
        uint32_t column,
        const char *text,
        uint32_t byte_count);
} mini_text_display_api_t;
```

The table is append-only after ABI stabilization.

## 6. Logical text surface

Text drawing uses zero-based character-cell coordinates.

```text
(0,0) ----------------------> column
  |
  |
  v
 row
```

There is no implicit application-visible cursor in v0. Applications maintain their
own cursor state and call `write_at()` with explicit coordinates.

The text API does not implement terminal semantics. It does not interpret ANSI
sequences, carriage return, newline, tab, backspace, scrolling, or cursor movement.
Those behaviors belong in a higher-level terminal/console layer if needed.

## 7. `get_info()`

```c
mini_result_t get_info(mini_text_display_info_t *out_info);
```

Semantics:

- `out_info` must be non-NULL;
- the caller supplies `struct_size`;
- success returns the current logical text geometry;
- the geometry may differ between platforms even when the application source is the
  same;
- applications must query geometry rather than assume a fixed 80x24 or other size.

## 8. `clear()`

```c
mini_result_t clear(void);
```

`clear()` clears the entire logical text surface to the implementation-defined default
blank state.

It does not necessarily cause an immediate physical refresh. Applications that require
portable visibility of pending changes call `present()` afterward.

Conceptually, `clear()` is equivalent to clearing the complete logical text rectangle,
although a backend may implement it more efficiently.

## 9. `clear_at()`

```c
mini_result_t clear_at(
    uint32_t row,
    uint32_t column,
    uint32_t rows,
    uint32_t columns);
```

`clear_at()` clears a rectangular region of the logical text surface starting at
`(row, column)`.

Example:

```text
clear_at(5, 10, 3, 20)

clears rows    5..7
       columns 10..29
```

Semantics:

- starting `row` or `column` outside the surface returns `MINI_ERR_INVALID`;
- `rows == 0` or `columns == 0` is a successful no-op;
- a rectangle extending beyond the right or bottom edge is clipped to the logical
  surface;
- the operation clears logical character cells, not physical pixel coordinates;
- the operation does not necessarily cause an immediate physical refresh.

## 10. `write_at()`

```c
mini_result_t write_at(
    uint32_t row,
    uint32_t column,
    const char *text,
    uint32_t byte_count);
```

Semantics:

- starting `row` or `column` outside the logical surface returns `MINI_ERR_INVALID`;
- `text` must be non-NULL when `byte_count > 0`;
- `byte_count == 0` is a successful no-op;
- text begins at the specified character cell;
- text extending past the right edge is clipped;
- text does not automatically wrap to another row;
- newline, carriage return, tab, ANSI sequences, and other terminal control semantics
  are not interpreted;
- the operation updates the logical surface but does not necessarily force a physical
  refresh.

### Character support

Display ABI v0 guarantees rendering of 7-bit printable ASCII.

The function accepts byte strings so future text capabilities can support UTF-8 without
changing the basic call shape, but v0 does not promise full Unicode rendering.
Unsupported bytes/characters may be represented by a replacement glyph or another
implementation-defined fallback.

Future Unicode capability, if added, must define its own precise semantics rather than
silently changing the v0 ASCII guarantee.

## 11. `present()`

```c
mini_result_t present(void);
```

`present()` asks MiniShell to make all display changes issued before the call visible as
far as the platform can reasonably guarantee.

This separates logical drawing from physical update policy:

```text
immediate TFT
    write_at() may already update hardware
    present() may be a lightweight flush/no-op

buffered display
    drawing updates backing state
    present() flushes or swaps the visible state

e-paper
    drawing updates logical/backing state
    present() performs the appropriate panel refresh
```

V0 `present()` is synchronous. Asynchronous present, wait-for-present, dirty-region
submission, waveform selection, or explicit partial-refresh policy may be added later as
extensions without changing v0 semantics.

## 12. Foreground ownership

V0 has one foreground application at a time and one primary display. Therefore the
application does not explicitly acquire or release a display handle.

Conceptually:

```text
shell owns foreground
    -> launch app
    -> foreground app may use Display ABI
    -> app returns
    -> shell resumes foreground
```

MiniShell remains the actual hardware owner throughout.

Multiple-display enumeration or explicit display sessions are deferred until a real use
case requires them.

## 13. Extension model

The display service is an extensible capability container.

V0:

```text
display
`-- text
```

Future:

```text
display
|-- text       unchanged
|-- graphics   appended
|-- framebuffer maybe appended
`-- devices    maybe appended
```

Existing function-table fields and meanings are never reordered, repurposed, or removed
once the ABI is stabilized. Applications check `struct_size`, capability bits, and
optional sub-API pointers before using appended functionality.

## 14. ABI test requirements

`abi_display.elf` should validate at least:

1. query `get_info()` and verify nonzero geometry;
2. clear the full logical surface and `present()`;
3. write text at `(0,0)`;
4. write text near the center;
5. write text at or near the bottom-right corner;
6. verify right-edge clipping without wrapping;
7. clear a small rectangle with `clear_at()` and visually verify only that region;
8. verify a `clear_at()` rectangle extending beyond the surface is clipped;
9. verify zero-sized `clear_at()` is a successful no-op;
10. verify out-of-range starting row/column returns `MINI_ERR_INVALID`;
11. call `present()` after multiple logical changes;
12. launch, run, exit, and repeat without destabilizing MiniShell.

The hardware test should combine automatic return-code checks with simple visual
verification of the resulting display content.

## 15. V0 boundary summary

```text
Display ABI v0
|
|-- primary logical display
|-- capability discovery
|-- text character grid
|   |-- geometry via get_info()
|   |-- clear()
|   |-- clear_at()
|   `-- write_at()
`-- present()
```

The core design principles are:

```text
logical surface, not panel hardware
text cells, not physical pixels
explicit coordinates, no implicit cursor
partial logical clear is supported
physical refresh is separated through present()
append-only capability growth
```
