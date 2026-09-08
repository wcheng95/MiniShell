# MiniShell Display API

Status: **implemented on the Linux reference backend.**

## Purpose

Display gives applications a platform-neutral logical display surface without exposing terminal escape sequences, LCD/e-paper controllers, framebuffers, fonts, pixel formats, or board-driver objects.

```text
application
    |
Display API
    |
portable Display service
    |
private backend
    |
terminal / TFT / e-paper / framebuffer
```

Display is output-only. Input is a separate API.

## Text capability

```c
#define MINI_DISPLAY_CAP_TEXT (1ull << 0)
```

When text is available, `api->display->text` exposes a logical character grid.

```c
typedef struct {
    uint32_t struct_size;
    uint32_t columns;
    uint32_t rows;
} mini_text_display_info_t;
```

Applications query geometry and use zero-based row/column coordinates. They must not assume 80x24 or any physical pixel geometry.

## Text operations

The current text API contains:

```c
get_info
clear
clear_at
write_at
write_at_attr
```

`present()` remains on the top-level Display service.

### `clear()`

Clears the logical text surface to the backend's default blank state.

### `clear_at()`

Clears a logical text rectangle. Zero-sized rectangles succeed as no-ops; rectangles extending beyond the surface are clipped; an invalid starting coordinate returns `MINI_ERR_INVALID`.

### `write_at()`

Writes text starting at one logical cell. Text is clipped at the right edge and never wraps implicitly. Newline/tab/ANSI bytes do not acquire terminal semantics through this API.

Printable ASCII is the baseline repertoire. Additional character support is backend-dependent until a future Unicode capability is specified.

## Text attributes

Nano's cursor requirement justified the first optional text-style primitive:

```c
#define MINI_TEXT_ATTR_NONE     0u
#define MINI_TEXT_ATTR_INVERSE  (1u << 0)

mini_result_t write_at_attr(uint32_t row,
                            uint32_t column,
                            const char *text,
                            uint32_t byte_count,
                            uint32_t attributes);
```

A backend that does not implement it may leave the pointer NULL; portable apps must fall back gracefully when the attribute is optional.

The initial defined attribute is `INVERSE`. Linux maps it to terminal reverse video below MiniShell. A framebuffer/TFT/e-paper backend can implement the same semantic operation natively.

Applications must not embed ANSI sequences to obtain styling.

## `present()`

```c
mini_result_t present(void);
```

`present()` asks MiniShell to make preceding logical changes visible as far as the platform can reasonably guarantee.

Typical mappings:

```text
Linux terminal   fflush/lightweight flush
immediate TFT    possibly a no-op or flush
e-paper          physical refresh policy
buffered target  flush/swap backing state
```

The application does not select panel waveform, framebuffer address, or other physical details.

## Foreground ownership

V1 has one foreground application and one primary display. Applications do not acquire a hardware display handle.

MiniShell/backend remains the owner of the underlying terminal/display throughout.

## API evolution

Applications should check the fields, capabilities, and optional pointers they actually require. `struct_size` remains useful for defensive discovery, but MiniShell does **not** currently promise stable field offsets, append-only growth, source compatibility, or binary compatibility across API revisions.

## Current Linux mapping

The Linux backend maps the logical text surface to terminal operations:

- geometry from terminal size with a fallback;
- clear/positioned writes through ANSI below the API;
- inverse attribute through reverse video;
- `present()` through output flush.

Terminal implementation details never appear in portable app source.

## Current verification

Linux tests exercise:

- Display availability and geometry;
- positioned text writes/clear/present;
- real PTY use by `nano`;
- inverse-video cursor rendering through `write_at_attr()`;
- clean app return to the shell.

## Deferred capabilities

Add only when application requirements justify them:

```text
foreground/background colors
additional text styles
font selection
graphics/pixel drawing
images
framebuffer access
brightness/backlight
orientation
multiple displays
explicit partial-refresh policy
```

The text API should remain useful independently of any future graphics sub-API.
