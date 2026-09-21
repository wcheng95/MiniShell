# T047 — Mini-CW V1.2 color UI

Status: READY

## Baseline

Start from the accepted Mini-CW full-lookup baseline:

```text
golden/minicw-full-lookup-baseline
920141cbc5a2e8b792f7dd24b10d0ecf08731a95
```

T046 is complete and hardware accepted. Do not mix logging or any other feature
into this task.

Pinned standalone reference:

```text
wcheng95/Mini-CW
3bfbf169b7c2d49a1be3e9a4c80f945edb32033e
```

Relevant reference files:

```text
components/ui_service/private_include/ui_screen.h
components/ui_service/ui_screen.cpp
components/ui_service/ui_service.c
components/ui_service/ui_cardputer_port.cpp
```

## Objective

Restore the standalone Mini-CW V1.2 Keyer display color/layout semantics through
the MiniShell Display boundary.

Required ADV appearance:

```text
top line      WHITE
separator     GREEN, 2 pixels high, directly below top line
lines 1..5    GREEN
line 6        CYAN
background    BLACK
```

The existing 20x7 text, fixed header, history/status behavior and row priorities
remain unchanged.

## Ownership

```text
Mini-CW
  owns semantic presentation:
  - top row is white
  - lines 1..5 are green
  - line 6 is cyan
  - a green separator follows the top row

MiniShell Display
  owns generic rendering capabilities:
  - text foreground color attributes
  - generic row-separator/decorative-line capability

ADV
  owns hardware realization:
  - RGB values / M5.Display calls
  - the existing 240x135 geometry
  - the native 2-pixel separator region at y=19..20
```

Do not put CW-specific color policy into MiniShell or ADV.

## Existing useful state

Mini-CW already carries V1.2 color vocabulary in `mini_cw_screen_t`:

```c
MINI_CW_SCREEN_COLOR_WHITE
MINI_CW_SCREEN_COLOR_GREEN
MINI_CW_SCREEN_COLOR_CYAN
```

and `ui_service` already marks normal Keyer history rows green and row 6 cyan.

Current migration loss occurs because `apps/minicw/src/ui_service/ui_screen.c`
flattens the screen to plain characters and `minicw_port_present()` calls only
`write_at()`, dropping color.

ADV already reserves the exact V1.2 separator geometry:

```text
kGapY      = 19
kGapHeight = 2
```

but currently fills it black.

## Generic Display API extension

Keep the extension generic and small.

Preferred shape:

1. Extend `write_at_attr()` text attributes with generic foreground-color
   values sufficient for at least DEFAULT/WHITE/GREEN/CYAN.
2. Add a Display capability bit so applications can detect text-color support.
3. Add one generic text-row separator operation/capability. It should express
   "separator after row N using this generic color/style", not Mini-CW or
   Cardputer semantics.
4. On ADV, separator-after-row-0 maps to the existing 2-pixel y=19..20 region.
5. Providers that do not support color/separator must fail or degrade cleanly;
   Mini-CW must still remain usable in monochrome.

Equivalent API design is acceptable if it preserves this ownership and does not
introduce a Mini-CW-specific resident hook.

Do not add a general pixel framebuffer/graphics subsystem merely for this task.

## Mini-CW rendering

Update the private Mini-CW screen/port path so the color data already present in
`mini_cw_screen_t` reaches MiniShell Display rather than being discarded.

For the Keyer app, enforce:

```text
row 0 / header  WHITE
rows 1..5       GREEN
row 6           CYAN
separator       GREEN
```

This should apply consistently across the normal Keyer view and its operation/
settings UI unless an existing explicit semantic color overrides it.

The separator is structural and remains green regardless of transient row text.

## ADV rendering

Map generic colors to the same effective V1.2 colors:

```text
WHITE -> TFT_WHITE equivalent
GREEN -> TFT_GREEN equivalent
CYAN  -> TFT_CYAN equivalent
BLACK background
```

The separator is exactly 2 pixels high below the top row on ADV.

Do not change font, text size, row y coordinates, cell width, row height,
rotation or 20x7 geometry.

## Freeze

Do not modify Mini-CW audio/Tone behavior, Morse timing, keyer semantics,
callsign lookup, Memory ownership, persistence, filesystem behavior, Digital I/O,
or logging.

Protected audio remains frozen.

## Tests

Cover at minimum:

- generic Display color attributes accepted and preserved;
- ADV color mapping for white/green/cyan;
- top row white;
- rows 1..5 green;
- row 6 cyan;
- green separator after row 0;
- ADV separator geometry is exactly y=19, height=2;
- text content remains byte-for-byte identical to baseline for equivalent UI state;
- fixed T045 header remains unchanged;
- callsign row text remains `<base-call>: <name>`;
- monochrome fallback works when color/separator capability is absent;
- no color policy names such as CW/history/operator leak into resident Display code;
- architecture/boundary checks pass.

Run full Linux CTest, portable units, focused Mini-CW/Display tests, real ADV
firmware build, clean minicw ELF build/inspection and `git diff --check`.

Because this task intentionally extends generic resident Display capability,
resident BIN/SRAM may change. Report exact deltas. Audio files themselves must
remain unchanged.

## Hardware acceptance

On ADV confirm:

1. header text is white;
2. 2-pixel green separator appears immediately below header;
3. lines 1..5 are green;
4. bottom line is cyan;
5. callsign display still reads e.g. `K7SHR: PAUL`;
6. paddle and M1 audio remain clean/no-pop;
7. Ctrl+C exits normally.

## Branch

Use:

```text
codex/T047-minicw-color-ui
```

No PR and no hardware testing by Codex.
Return exact SHA plus tests and resident/external resource deltas.
