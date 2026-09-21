# T047 — Mini-CW V1.2 color UI

Status: COMPLETE

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


## Implementation handoff

Implementation starts at task-branch head
`1b717fd134eccb80b2f8f3b8fd723dfcc25aa177`, which contains the accepted
`920141cbc5a2e8b792f7dd24b10d0ecf08731a95` recovery baseline.

### Implementation summary / files changed

- `include/minishell/api.h` adds generic foreground attributes (default/white/
  green/cyan), color and row-separator capability bits, and an optional text API
  tail `set_row_separator(after_row, foreground)`. DEFAULT removes a separator;
  other foreground values select its color. Existing API version 3 and PCM/Tone
  contracts are unchanged. `docs/api/display-api.md` documents discovery,
  validation, buffered presentation, reset and unsupported-provider behavior.
- `core/minishell_services/{display_service.c,minishell_services.h}` validates
  attributes and separator row/color, gates capabilities on backend support, and
  forwards through a private generic hook. No application color policy resides
  here. Linux retains default/inverse rendering without advertising new support.
- `platform/adv/{adv_display.cpp,adv_internal.h,adv_backend.c}` advertises both
  capabilities, maps foreground colors to RGB white/green/cyan on black, preserves
  inverse, and renders separator-after-row-0 at y=19, height=2. Other boundaries
  return UNSUPPORTED. Full clear and shell-console restoration reset the separator.
  Existing font, row/cell geometry and rendering cadence are untouched.
- `apps/minicw/src/ui_service/ui_screen.{c,h}` preserves semantic color data,
  defaulting header/history/bottom to white/green/cyan. The shared private color
  vocabulary lives in `port/minicw_port.h`; `port/minicw_port.c` maps it to generic
  Display attributes, groups equal-color runs, tracks color-only changes, and
  sets the structural green separator once per frame-session. It checks both
  capability and struct size, with plain text fallback for absent/unsupported
  optional styling. `apps/minicw/README.md` describes the restored appearance.
- `tests/unit/test_display.c`, `tests/minicw_lookup_test.c`,
  `tests/adv_display_color_test.py` and `CMakeLists.txt` add service validation,
  UI/provider behavior and executable host-stub ADV renderer coverage.

### Behavior / invariants preserved

The pinned reference's effective TFT_WHITE/TFT_GREEN/TFT_CYAN colors and fixed
separator geometry are restored through generic Display facilities. The whole
`ui_service.c` is byte-for-byte unchanged, including the fixed T045 header and
row priorities. Normal and Operation screen text compare byte-for-byte between
colored and monochrome paths. Explicit semantic colors override row defaults.

Protected audio source/header diff is **NONE** against the accepted recovery
baseline: Audio service, tone stream/simulator, ADV speaker and Mini-CW audio
wrapper are unchanged. Mini-CW app controller, Keyer timing/domain, lookup,
Memory ownership, storage and Digital I/O are unchanged. No logging, tasks,
audio transport, DMA or FT8 changes. Public header changes are limited to the
explicitly authorized generic Display extension.

### Tests run and results

```sh
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
# PASS: 86/86
cmake -S tests/unit -B /tmp/T047-unit
cmake --build /tmp/T047-unit -j8
ctest --test-dir /tmp/T047-unit --output-on-failure
# PASS: 27/27
ctest --test-dir build-linux -R 'minicw|display|tone|architecture|boundary' --output-on-failure
# PASS: 21/21
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: real before/after builds
idf.py -C platform/adv/elf_apps/minicw fullclean
idf.py -C platform/adv/elf_apps/minicw elf
# PASS: clean 1074-step external build
python3 tests/minicw_elf_inspect.py platform/adv/elf_apps/minicw/build/minicw.app.elf
# PASS: sole resident import mini_api_get; 646 mapped relocations
xtensa-esp32s3-elf-size -A platform/adv/build/minishell_adv.elf
git diff --check
# PASS
```

Tests cover all foreground values preserved through the service, inverse plus
color, invalid bits/rows, missing capabilities, unsupported separator boundaries,
ADV RGB mapping, exact separator geometry and black reset, console restoration,
normal/Operation row colors, explicit color-only updates, identical plain text,
legacy text-table prefix fallback, fixed header and callsign row regressions.
The ADV renderer test compiles actual provider source with recording M5 display
calls; it also checks that Mini-CW/history/operator policy names are absent.
Architecture/dependency checks passed without weakening rules. One initial
private-header dependency violation was corrected by putting shared vocabulary
in the existing private port seam before final validation.

### Resource evidence

ESP-IDF v5.5.4 / Xtensa GCC 14.2.0. Actual before/after artifacts from this task:

| Resident measure | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Firmware BIN bytes | 1,381,280 | 1,381,568 | +288 |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,016 | 27,016 | 0 |
| `.dram0.bss` | 40,336 | 40,336 | 0 |
| Static internal SRAM (these three sections) | 131,311 | 131,311 | **0** |

Added small provider state/API fields fit existing linked section alignment;
measured static SRAM section sizes are unchanged. Resident BIN is intentionally
changed by the authorized Display implementation.

| External measure | Before | After | Delta |
| --- | ---: | ---: | ---: |
| ELF file bytes | 44,912 | 45,324 | +412 |
| `.text` | 31,168 | 31,556 | +388 |
| `.rodata` | 2,012 | 2,012 | 0 |
| `.data` | 1,284 | 1,284 | 0 |
| `.bss` | 3,292 | 3,432 | +140 |
| Section-loader text + data allocation | 37,756 | 38,284 | +528 |

The 140-byte external BSS increase is the last-presented per-cell color cache.
The screen adapter also uses a bounded 140-byte temporary color array during
rendering. No dynamic allocations or tasks are added. Lookup table allocation
is unchanged. Sole resident ELF import remains **`mini_api_get`**.

### Hardware/manual validation / risks / commit reference

No PR or hardware testing by Codex. Supervisor review precedes the task's ADV
acceptance: white header, green 2-pixel separator, green rows 1–5, cyan bottom,
unchanged callsign text, clean paddle/M1 audio and normal Ctrl+C exit. Host tests
verify color calls and geometry, not physical panel appearance or audio quality.
Providers without optional styling remain monochrome. No scope deviations.

Commit reference: the single implementation commit containing this evidence;
exact SHA returned after pushing `codex/T047-minicw-color-ui`. Status: REVIEW.


## Supervisor review

Reviewed implementation commit:

```text
afc4f548647d3b47b563172ba912079c36c228ab
```

No functional blocker found.

The Display API extension is generic: foreground-color attributes and
row-separator capability contain no Mini-CW/CW/history/operator semantics.
ADV owns only RGB mapping and physical separator geometry. Mini-CW retains
semantic color policy and degrades to unchanged monochrome text when optional
styling is unavailable. The exact V1.2 separator geometry (y=19, height=2) and
white/green/cyan mapping are covered by provider-level tests.

The public API append is guarded by capability and `struct_size` checks, so
legacy text-table prefixes remain usable. Audio/timing/lookup/persistence have no
diff.

Minor cleanup debt, not a hardware blocker: `mini_cw_screen_color_t` currently
lives in the private `minicw_port.h` so both UI and port can share it. Semantic
color vocabulary conceptually belongs to Mini-CW UI/shared-private types rather
than the platform adapter. This can be cleaned up before or with the next
architecture-hygiene pass without changing T047 behavior.

Hardware acceptance remains: physical white header, green separator and rows
1-5, cyan bottom row, unchanged callsign text, clean paddle/M1 audio, normal
exit.


## Final hardware acceptance — 2026-09-21

Implementation:

```text
afc4f548647d3b47b563172ba912079c36c228ab
```

ADV hardware validation passed.

Accepted physical presentation:

```text
top/header     WHITE
separator      GREEN, 2 pixels high directly below header
rows 1..5      GREEN
row 6          CYAN
background     BLACK
```

The colored layout is visually accepted. Existing callsign text, full-table
lookup, persistence, paddle/M1 audio and Ctrl+C behavior remain correct.

T047 is COMPLETE.
