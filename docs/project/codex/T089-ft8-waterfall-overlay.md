# T089 — MiniShell RGB565 raster + MiniFT8 RX waterfall overlay

Status: REVIEW

## Intent

Restore the useful MiniFT8-V2 local waterfall display in the current V3.088
architecture without restoring V2's UI/platform coupling.

The operator goal is modest:

> Press W on the RX top level to replace the 19-pixel top status row with a
> small live waterfall. It is primarily a quick view of band activity and a
> visual confirmation that the QMX USB-UAC RX path is alive.

This is a nice-to-have presentation feature. It must not change FT8 decode
mathematics, slot timing, USB/UAC ownership, decoder scheduling, or the accepted
V3.088 field baseline.

The architect explicitly chose a small generic MiniShell raster capability
rather than an ADV-private MiniFT8 display hook.

## Baseline

Start from current main:

~~~text
2fffd8920017be3aed75f59ee8052cea511a9fcf
~~~

Expected branch:

~~~text
codex/T089-ft8-waterfall-overlay
~~~

Pinned MiniFT8-V2 behavioral reference:

~~~text
repository: wcheng95/Mini-FT8
commit:     491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
~~~

Primary V2 waterfall references:

~~~text
main/ft8_audio_pipeline.cpp
    push_waterfall_latest()

components/ui/ui.cpp
    ui_push_waterfall_row()
    ui_draw_waterfall()

components/ui/include/ui.h
~~~

V2's pinned local UI used an always-visible 18-pixel waterfall plus a 3-pixel
countdown bar. T089 does not restore that geometry. It restores the useful
V2 spectrum projection and scrolling behavior inside the cleaner V3 20x7 layout.

## Current ADV geometry

Current platform/adv/adv_display.cpp maps the 20x7 logical text UI to:

~~~text
y=0..18      text row 0       19 px
y=19..20     row separator     2 px
y=21..39     text row 1       19 px
...
             text row 6
~~~

T089 uses exactly the physical area of text row 0:

~~~text
x=0..239
y=0..18
~~~

The existing separator and rows 1..6 must not move.

Normal RX top level:

~~~text
+------------------------+
| RX 20 14:32:08 1/1 8  |  y=0..18
+------------------------+  existing separator y=19..20
| decoded message        |
| decoded message        |
| ...                    |
~~~

Waterfall enabled:

~~~text
+------------------------+
| 240 x 19 waterfall     |  y=0..18
+------------------------+  existing separator y=19..20
| decoded message        |
| decoded message        |
| ...                    |
~~~

## User interaction

W is an RX-top-level presentation toggle.

Required behavior:

- w and W are equivalent, following existing case-insensitive UI keys;
- only SCREEN_RX with UI_SUBMENU_NONE reacts to W;
- pressing W toggles the waterfall overlay ON/OFF;
- if the platform does not expose the new raster capability, W does nothing;
- W is ignored on TX/O/S/V and in all submenus;
- switching away from RX hides the overlay and shows the destination screen's
  normal top line;
- the session-local enabled state may remain latched while another screen is
  visible; returning to RX top level shows the waterfall again;
- pressing W again on RX top level disables it and immediately restores the
  normal status top line;
- do not persist this state in setting.txt;
- every new FT8 session starts with the waterfall disabled.

This is an overlay state, not a new UIScreen and not SCREEN_W.

## MiniShell raster capability

Add one deliberately small optional raster capability. Do not build a graphics
framework.

### Public capability

Append:

~~~c
#define MINI_DISPLAY_CAP_RASTER_RGB565 (1ull << 3)
~~~

### Raster info

Add:

~~~c
typedef struct {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
} mini_raster_display_info_t;
~~~

### Raster API

Add:

~~~c
typedef struct {
    uint32_t struct_size;

    mini_result_t (*get_info)(
        mini_raster_display_info_t *out_info);

    mini_result_t (*blit_rgb565)(
        uint32_t x,
        uint32_t y,
        uint32_t width,
        uint32_t height,
        const uint16_t *pixels,
        uint32_t stride_pixels);
} mini_raster_display_api_t;
~~~

Append the optional pointer to mini_display_api_t after the existing fields:

~~~c
const mini_raster_display_api_t *raster;
~~~

Follow the existing optional-tail pattern used by the Display API. Do not bump
MINISHELL_API_VERSION solely for this optional append-only capability.

### RGB565 semantic contract

Public RGB565 values are logical host-endian uint16_t values:

~~~text
bits 15..11  red
bits 10..5   green
bits 4..0    blue
~~~

The application must never know LCD/SPI byte order. If ADV/LovyanGFX needs byte
swapping, the ADV provider owns that conversion.

### blit_rgb565() semantics

Keep this operation intentionally simple:

- it is a synchronous, immediate raster blit;
- it does not create or expose a framebuffer;
- it does not modify MiniShell's logical text buffers;
- it does not require a later display->present();
- a later text present() may overwrite overlapping physical pixels;
- application/presentation code owns ordering when text and raster overlap;
- non-zero rectangles must fit fully within reported raster geometry;
- no clipping;
- pixels must be non-NULL for a non-zero rectangle;
- stride_pixels >= width;
- zero width or zero height is a successful no-op;
- no alpha, palette, image decode, scaling, rotation or color conversion API.

The MiniShell service layer validates these generic arguments before calling the
backend.

### Explicit non-goals for the raster API

Do not add:

~~~text
drawLine
drawRect / fillRect
circle
font/text rasterization
sprite API
framebuffer mapping
palette API
alpha/transparency
scrolling API
image/Png/Jpeg/BMP decode
rotation
display-specific byte swapping in the public API
~~~

One bounded RGB565 rectangle transfer is sufficient.

## Platform behavior

### ADV

ADV advertises:

~~~text
MINI_DISPLAY_CAP_RASTER_RGB565
~~~

and reports:

~~~text
width  = 240
height = 135
~~~

The backend performs the immediate physical RGB565 blit.

A bounded provider-side row scratch or a display-library byte-swap facility is
acceptable if required by the LCD library. Do not allocate a full-screen or
240x19 RGB framebuffer merely for byte-order conversion.

The existing text buffers, 20x7 geometry, separator behavior, console rendering
and text present() semantics remain unchanged.

### Linux

Linux does not need raster support in T089.

It must:

~~~text
not advertise MINI_DISPLAY_CAP_RASTER_RGB565
raster pointer may be NULL
retain all existing terminal/text behavior
~~~

MiniFT8 continues to work normally on Linux; W simply has no waterfall effect.

## MiniShell ownership boundary

The generic boundary remains:

~~~text
MiniFT8
    owns waterfall meaning, history and colors
        |
        v
MiniShell Display raster API
    owns validated RGB565 rectangle transport
        |
        v
ADV display backend
    owns M5.Display / LCD / wire byte order
~~~

MiniShell must not know:

~~~text
FT8
waterfall
spectrum bins
200..2900 Hz
160 ms symbols
yellow palette meaning
W key meaning
~~~

## V2 spectrum projection to preserve

Do not perform another FFT.

Current V3 already produces the FT8 monitor waterfall inside:

~~~text
apps/ft8/src/ft8_engine/ft8_monitor.c
~~~

Each successful engine block is:

~~~text
960 samples / 6000 Hz = 160 ms
~~~

T089 derives one display row from each newly completed monitor block.

### Exact pinned-V2 projection rule

The pinned V2 push_waterfall_latest() does this for the latest logical monitor
block:

1. point at the start of the block's stored waterfall data;
2. use the first time_sub row only;
3. for each frequency bin, take the maximum across freq_osr;
4. scale num_bins to 240 display pixels using V2 integer-range MAX pooling.

In pseudocode:

~~~text
base = latest block

for bin in 0 .. num_bins-1:
    collapsed[bin] =
        max(base[freq_sub * num_bins + bin]
            for freq_sub in 0 .. freq_osr-1)

for x in 0 .. 239:
    start = x       * num_bins / 240
    end   = (x + 1) * num_bins / 240
    if end <= start:
        end = start + 1

    row[x] = max(collapsed[start .. end-1])
~~~

Important: with time_osr=2, do not MAX or average the two 80-ms time-subrows
for the local display. Pinned V2 used the first time-subrow and generated one
screen row per 160-ms monitor block.

Current ADV freq_osr=1, so the frequency-oversample collapse is currently
trivial, but keep the helper correct for freq_osr > 1.

The projection must be pure/host-testable and must not modify the decoder
waterfall.

## Current V3.088 capture timing remains authoritative

T089 does not change the accepted slot-local schedule:

~~~text
UTC +0.00 s
    begin/reset current slot capture

0.00 .. 12.64 s
    79 x 960-sample blocks
    each completed block can produce one display row

UTC +12.64 s
    submit decode
    stop feeding current slot FFT/waterfall

12.64 .. 15.00 s
    continuously drain/discard Audio
    no FT8 monitor block is produced

next UTC
    begin next capture window
~~~

Therefore a normal RX slot produces up to 79 display-row updates at approximately
160-ms intervals during the 0..12.64 s capture window.

The waterfall is intentionally static during the existing 12.64..15.00 s
drain/discard interval and during physical TX. Do not add an extra FFT merely
to keep the visual moving during those periods.

This matches the architectural goal: display existing DSP work; do not create
new DSP load for a cosmetic feature.

## Row production boundary

Do not expose Ft8Monitor internals to the UI.

A preferred small shape is:

~~~text
rx_slot_framer ENGINE_BLOCK
        |
        v
ft8_engine_process_block()
        |
        +--> decoder waterfall unchanged
        |
        '--> increment a display-waterfall generation
~~~

Add a pure engine/monitor accessor that can project the latest completed block to
a caller-supplied width without modifying engine state.

The exact private function names are implementation details, but preserve these
rules:

- no second FFT;
- no copy of the full decoder waterfall;
- no UI/platform dependency under ft8_engine;
- no display width hard-coded into decoder mathematics;
- projection width is supplied by the caller;
- no projection work is required on platforms that do not expose raster;
- app/controller exposes only the newest row + monotonically changing generation,
  not raw engine pointers.

A small controller getter such as:

~~~text
current generation
latest projected row when generation changed
~~~

is preferred over putting 240-byte waterfall data into UiModel.

UiModel should remain application state, not a 6.25-Hz streaming pixel channel.

## Waterfall display history

The FT8 presentation edge owns a 19-row intensity history:

~~~text
19 rows x 240 uint8_t = 4560 bytes
~~~

Behavior:

- initialize all rows to zero/black when FT8 starts;
- push one 240-byte row per new monitor block;
- circular history;
- newest row appears at physical y=18;
- older rows scroll upward toward y=0;
- do not clear at each UTC slot boundary;
- continue advancing history while the overlay is hidden, if raster is available;
- no display row is generated during the 12.64..15.00 discard interval or TX;
- history remains session-local and is discarded when FT8 exits.

Do not allocate a persistent 19x240 RGB565 framebuffer.

For drawing, a temporary one-row conversion buffer is enough:

~~~text
240 x uint16_t = 480 bytes
~~~

A bounded stack scratch row is acceptable.

### Allocation rule

Do not add about 5 KiB to the 16-KiB ADV foreground task stack by embedding the
entire history directly in ft8_ui_adapter_t.

If raster is supported and geometry is at least 240x19, allocate the small
intensity-history object through MiniShell Memory.

If this optional allocation fails:

~~~text
waterfall support becomes unavailable for the session
FT8 continues normally
~~~

A nice-to-have display must never make FT8 startup fail.

Linux/no-raster builds allocate no waterfall history.

## Color

Use the simple pinned-V2 visual for T089:

~~~text
R = intensity
G = intensity
B = 0
~~~

yellow on black.

Logical RGB565 conversion is therefore equivalent to:

~~~c
((v & 0xF8u) << 8) | ((v & 0xFCu) << 3)
~~~

No blue component.

Do not add:

~~~text
AGC
palette selection
color scale
frequency labels
grid lines
cursor
TX-tone waterfall
S-meter
SWR/power overlay
~~~

Those are outside T089.

## UI ownership

UiShell owns the session toggle state.

Add bounded state equivalent to:

~~~text
waterfall_supported
waterfall_enabled
~~~

waterfall_supported is supplied by the presentation edge after MiniShell
capability discovery.

UiShell input handling:

~~~text
if RX top level and raster supported and key is W:
    waterfall_enabled = !waterfall_enabled
~~~

Other screens/submenus ignore W.

### UiFrame overlay flag

Add a presentation fact to UiFrame, equivalent to:

~~~text
waterfall_overlay
~~~

It is true only when:

~~~text
waterfall_supported
AND waterfall_enabled
AND screen == RX
AND submenu == NONE
~~~

This is important because the ordinary text frame itself remains valid and still
contains the normal top status line.

Do not blank or destroy the logical top text row.

## Rendering order

The raster is a physical overlay on top of the ordinary text presentation.

When a normal UI frame is rendered:

~~~text
1. render all ordinary text rows
2. display->present()
3. if frame.waterfall_overlay:
       redraw the complete 240x19 waterfall over physical y=0..18
~~~

Do not call text present() again after the raster overlay.

This gives clean toggle behavior.

### OFF -> ON

waterfall_overlay changes in UiFrame, so the normal frame comparison causes a
render. Text is rendered first, then the waterfall overlays row 0.

### ON -> OFF

waterfall_overlay changes again. The normal text render/present redraws row 0
and no raster is applied afterward, so the status line returns immediately.

### Ordinary top-line changes while ON

UTC/status changes may cause a normal text render. The adapter must redraw the
waterfall immediately after that text present() so the top status text does not
flash/stay visible over the waterfall.

### New waterfall row while ON

Push the new intensity row into the circular history, then redraw the 240x19
waterfall immediately through raster blits.

A row-at-a-time redraw using one 240-pixel RGB565 scratch row is preferred over a
persistent full RGB framebuffer.

## Presentation capability discovery

ft8_ui_adapter may treat waterfall support as available only when all of these
are true:

~~~text
display has MINI_DISPLAY_CAP_RASTER_RGB565
display struct is large enough to contain raster pointer
raster != NULL
raster struct contains get_info + blit_rgb565
reported geometry >= 240 x 19
small history allocation succeeds
~~~

Expose a small adapter query to ft8_main/UiShell; do not let ui_shell inspect
MiniShell APIs directly.

Portable MiniFT8 remains platform-independent.

## No changes to accepted RX/decode architecture

T089 must not change:

~~~text
QMX UAC transport format
2048-frame UAC ring
RxFrontend conversion
V3.088 UTC capture/decode schedule
time_osr=2
freq_osr=1 on ADV
candidate capacity 50
minimum score 5
LDPC iteration limit 25
decoder core/priority/stack
T086 RxTxLog durability policy
T088 stale-decode cancellation policy
CAT/TX behavior
physical USB/UAC lifecycle
~~~

The waterfall must be observational presentation only.

## MiniFT8 version

T089 changes production MiniFT8 behavior.

Set About to:

~~~text
MiniFT8-V3.089
~~~

and update the matching UI regression assertion.

Do not change the MiniShell API version merely because the optional raster tail is
added.

## Expected implementation areas

Likely public/service/platform files:

~~~text
include/minishell/api.h
core/minishell_services/minishell_services.h
core/minishell_services/display_service.c
platform/adv/adv_backend.c
platform/adv/adv_internal.h
platform/adv/adv_display.cpp
docs/api/display-api.md
~~~

Likely MiniFT8 files:

~~~text
apps/ft8/include/ft8/app_types.h
apps/ft8/main/ft8_main.c
apps/ft8/main/ft8_ui_adapter.c
apps/ft8/main/ft8_ui_adapter.h
apps/ft8/src/ui_shell/ui_shell.c
apps/ft8/src/ui_shell/ui_shell.h
apps/ft8/src/app_controller/app_controller.c
apps/ft8/src/app_controller/app_controller.h
apps/ft8/src/ft8_engine/ft8_engine.c
apps/ft8/src/ft8_engine/ft8_engine.h
apps/ft8/src/ft8_engine/ft8_monitor.c
apps/ft8/src/ft8_engine/ft8_monitor.h
~~~

Exact private helper placement may differ if ownership is cleaner, but do not
introduce a new platform escape hatch.

Update current MiniFT8 docs where appropriate:

~~~text
docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/architecture.md
docs/MiniFT8/ui.md
~~~

This task packet remains the implementation/history record.

## Automated tests

### MiniShell Display service

Extend display service/unit coverage for:

- raster capability absent -> public raster pointer absent/unsupported;
- capability present only when required backend hooks are present;
- get_info() validates struct_size and non-zero geometry;
- zero-size blit is a no-op;
- NULL pixels rejected for non-zero rectangle;
- stride smaller than width rejected;
- out-of-bounds non-zero rectangles rejected;
- valid rectangle/stride passed to backend unchanged;
- text behavior/capabilities remain unchanged;
- no MiniShell API version bump.

### ADV raster backend

Add host/source-level coverage for:

- reported geometry exactly 240x135;
- logical RGB565 values reach the physical display with correct color/byte order;
- a 240x1 row is accepted at y=0..18;
- no full-screen/full-19-row persistent RGB framebuffer is introduced;
- existing text row 0, separator, rows 1..6 and console paths remain unchanged;
- raster blit itself does not mutate text buffers.

### V2 waterfall projection

Add deterministic pure tests against the pinned-V2 projection rule:

- one output row per completed 960-sample block;
- first time_sub selected;
- freq_osr=1 exact values;
- synthetic freq_osr>1 uses MAX across frequency oversamples;
- FT8 bins -> 240 pixels uses the exact V2 integer MAX-pool boundaries;
- no mutation of source waterfall;
- no second FFT.

Prefer a small synthetic waterfall-layout vector that makes time-sub and
freq-sub mistakes obvious.

### UiShell

Cover:

- W unsupported -> no state change;
- W toggles on RX top level;
- lower/upper case both work;
- W ignored in RX submenu;
- W ignored on TX/O/S/V;
- enabled state may remain latched while another screen is shown;
- overlay flag false outside RX top level;
- returning to RX top level restores overlay when still enabled;
- second W disables and overlay flag changes immediately.

### FT8 UI adapter/history

Cover:

- no raster capability -> no optional allocation;
- insufficient raster geometry -> no optional allocation/support;
- optional allocation failure -> FT8 UI remains usable, waterfall unsupported;
- successful history allocation is approximately the 19x240 intensity store,
  not a full RGB framebuffer;
- initial history black;
- pushes scroll upward with newest row at y=18;
- history advances while hidden;
- normal text present happens before raster overlay;
- any ordinary frame redraw while overlay ON repaints the waterfall afterward;
- toggling OFF restores normal row-0 text;
- visible row update redraws raster without another text present();
- shutdown frees the optional history allocation.

### Integration / architecture

Add/source-check as appropriate:

- no ESP-IDF/M5/ADV include enters apps/ft8/**;
- ui_shell does not inspect MiniShell API/platform details;
- ft8_engine does not depend on Display/UI;
- MiniShell display service does not know FT8/waterfall semantics;
- Linux still runs with raster unsupported;
- current decoder/UAC/task configuration remains unchanged.

Run the complete existing Linux/unit/architecture suites, not only focused tests.

## Build gates

Run the current full gates from the repository root, including:

~~~bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T089-build-unit
cmake --build /tmp/T089-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T089-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
~~~

Use the current suite counts from the checkout. Do not hard-code old T017/T088 test
counts into the handoff.

## Resource evidence

Record ADV build/runtime impact.

At minimum report:

~~~text
firmware size delta
.dram0.data delta
.dram0.bss delta
any new permanent static SRAM
optional FT8 waterfall allocation size
V -> Memory with FT8 active:
    heap free
    largest free block
    app allocated
    allocation count
~~~

Expected design target:

~~~text
persistent FT8 intensity history    ~4560 bytes
temporary RGB row scratch            480 bytes stack
no persistent 240x19 RGB565 buffer
~~~

Do not fail the task solely because allocator metadata/alignment makes the
measured app allocation slightly larger than 4560 bytes.

The current architect observation before T089 is approximately:

~~~text
FT8 loaded:
heap free       ~87 KiB
largest block   ~31 KiB
~~~

Use the actual T089 before/after hardware measurements rather than assuming these
values remain exact.

## Manual ADV/QMX validation

After software review and merge candidate build:

1. Boot ADV and start bare ft8 with QMX.
2. Confirm normal V3.089 RX/decode first with waterfall OFF.
3. On RX top level press W.
4. Confirm only physical y=0..18 changes to waterfall.
5. Confirm the y=19..20 separator and six decoded-message rows do not move.
6. Observe band activity in yellow-on-black.
7. Confirm a healthy live QMX produces visible row progression during the
   0..12.64 s capture window.
8. It is expected for the waterfall to remain static during the existing
   12.64..15.00 s drain/discard interval; this is not a UAC failure.
9. Press W again; normal top status row must return immediately.
10. Enable W, switch to another UIScreen, and confirm that screen shows its normal
    top row. Return to RX; the overlay should reappear if still enabled.
11. Exercise RX paging/message selection with W enabled; rows 1..6 must behave
    exactly as before.
12. Confirm ordinary decode continues and no zero-decode regression appears in
    reasonable active-band testing.
13. Check UAC high-water/overflow/discontinuity diagnostics for regression.
14. Record V -> Memory with the feature available.
15. Exit FT8 normally; no display/resource cleanup error.

The visual feature is accepted only if it remains subordinate to FT8 correctness.
If raster refresh measurably damages UAC continuity or decode timing, stop and
record the evidence rather than increasing task/thread complexity.

## Non-goals

Do not include:

- continuous display-only FFT during 12.64..15.00;
- a second FFT path;
- freq_osr=2;
- V2 block normalization;
- WebUI;
- TX waterfall/tone marker;
- frequency ruler/labels;
- tuning cursor;
- spectrum analyzer controls;
- waterfall palette/settings;
- persistence of W state;
- generic graphics primitives beyond RGB565 blit;
- framebuffer exposure;
- ADV-private MiniFT8 display hooks;
- changes to USB/UAC, CAT, TX, decode worker or V3.088 timing.

## Acceptance summary

T089 is successful when:

~~~text
MiniShell:
    optional generic RGB565 raster blit exists
    ADV supports it
    Linux remains text-only

MiniFT8:
    W toggles RX-top-row waterfall
    240 x 19 pixels
    newest row every completed 160-ms monitor block
    pinned-V2 spectrum projection
    yellow on black
    history ~3.04 s
    no second FFT
    normal text/UI restored instantly when OFF
    no platform coupling

System:
    V3.088 decode/UAC behavior remains intact
    optional allocation failure cannot prevent FT8 startup
~~~

## Codex handoff requirements

Implement on:

~~~text
codex/T089-ft8-waterfall-overlay
~~~

Prefer one reviewable implementation commit.

Before handoff, append to this packet:

~~~text
implementation summary
files changed
API/ownership decisions
tests run/results
ADV build/resource evidence
known limitations
commit SHA
manual validation still required
~~~

Do not open a PR. Do not merge to main. Supervisor review comes first.


## Implementation evidence — 2026-10-17

### Implementation summary

Implemented the optional generic RGB565 Display tail, validated service transport,
ADV immediate raster provider, and MiniFT8 W/w RX-top-level overlay. About is
`MiniFT8-V3.089`. Linux remains text-only. This is a software review candidate;
no hardware acceptance is claimed and main has not been merged.

Started on `codex/T089-ft8-waterfall-overlay` from current main
`3b49673bf6d6f7224f2f14979dff23c1368009e4`. That commit adds only this READY packet
above the specified production baseline `2fffd8920017be3aed75f59ee8052cea511a9fcf`.
Pre-existing untracked caches and `rebuild-all.sh` were left untouched.

Read pinned V2 sources using `git show
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0:<path>` in the architect-provided local
repository `/home/wei/projects-migrated-to-mac/Mini-FT8`: the audio pipeline's
`push_waterfall_latest()` and UI push/draw helpers. The local checkout HEAD is
newer; only the pinned commit was used for preserved projection semantics.

### API/ownership decisions and invariants preserved

- API version remains 3. Raster is an optional append-only Display pointer.
  Service checks geometry, struct size, zero rectangles, pixels, stride and full
  bounds; backend receives valid rectangles unchanged. ADV uses LovyanGFX's
  `setSwapBytes(true)` for logical uint16_t RGB565, restoring the prior setting.
  Its installed `LGFXBase.hpp` maps that setting to `rgb565_t`, rather than
  wire-order `swap565_t`. Host tests exercise red, green and blue with both prior
  swap settings. No text-buffer or console changes occur in raster blits.
- Pure monitor projection reads the latest completed block's first time subrow,
  MAXes frequency subs and exact V2 integer pooling ranges. MAX commutativity
  avoids even an intermediate collapsed-spectrum allocation. Engine generation
  advances once per successful block and survives slot/stream resets.
- Controller offers the newest projected row/generation, without raw pointers.
  A small optional synchronous foreground notification invokes that getter once
  per block, because an RX step may drain enough audio to finish multiple blocks.
  This is a private implementation choice within the packet's row-production
  boundary: no second queue, scheduler, thread or DSP path. No observer or
  projection runs on text-only/unavailable sessions or the decode worker.
- UiShell owns support/enabled state; UiFrame adds only the overlay fact and
  retains normal row-0 text. The adapter owns optional circular intensity history,
  continues it while hidden, paints newest at y=18, and renders text/present
  before overlay. Visible row updates blit without text present. Session state
  is not persisted; allocation failure leaves FT8 usable.
- No USB/UAC/ring/frontend, UTC capture/decode/discard, candidate/LDPC, decoder
  task/core/priority/stack, CAT/TX, durability or cancellation policy changes.
  Existing tests continue checking the accepted configuration. No extra FFT,
  decoder-waterfall copy, persistent RGB framebuffer, or application platform
  dependency was introduced.
- No task requirement or approved architecture deviation. The optional
  notification complements the preferred getter to preserve every block row.

### Files changed

- `CMakeLists.txt`
- `apps/ft8/include/ft8/app_types.h`
- `apps/ft8/main/ft8_main.c`
- `apps/ft8/main/ft8_ui_adapter.c`
- `apps/ft8/main/ft8_ui_adapter.h`
- `apps/ft8/src/app_controller/app_controller.c`
- `apps/ft8/src/app_controller/app_controller.h`
- `apps/ft8/src/app_controller/app_controller_internal.h`
- `apps/ft8/src/ft8_engine/ft8_engine.c`
- `apps/ft8/src/ft8_engine/ft8_engine.h`
- `apps/ft8/src/ft8_engine/ft8_monitor.c`
- `apps/ft8/src/ft8_engine/ft8_monitor.h`
- `apps/ft8/src/ui_shell/ui_shell.c`
- `apps/ft8/src/ui_shell/ui_shell.h`
- `core/minishell_services/display_service.c`
- `core/minishell_services/minishell_services.h`
- `docs/MiniFT8/README.md`
- `docs/MiniFT8/architecture.md`
- `docs/MiniFT8/development.md`
- `docs/MiniFT8/ui.md`
- `docs/api/display-api.md`
- `docs/project/codex/T089-ft8-waterfall-overlay.md`
- `include/minishell/api.h`
- `platform/adv/adv_backend.c`
- `platform/adv/adv_display.cpp`
- `platform/adv/adv_internal.h`
- `tests/adv_console_scrollback_test.py`
- `tests/adv_display_color_test.py`
- `tests/adv_ft8_decode_policy_test.py`
- `tests/adv_ft8_defaults_test.py`
- `tests/adv_qmx_serial_test.py`
- `tests/ft8_engine_rx1g_test.c`
- `tests/ft8_monitor_rx1c_test.c`
- `tests/ft8_pending_start_test.c`
- `tests/ft8_rx_discontinuity_test.c`
- `tests/ft8_ui_smoke.c`
- `tests/ft8_waterfall_adapter_test.c`
- `tests/unit/test_display.c`

### Tests run and results

Commands run from repository root unless a baseline path is stated:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T089-build-unit
cmake --build /tmp/T089-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T089-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

- Linux configure/build: PASS. Final full CTest: **130/131 pass**, 60.97 s;
  sole failure is existing `linux_serial_unit`, line 67, asserting that a PTY
  write after queue saturation must time out with zero bytes.
- Standalone unit configure/build/CTest: **29/29 PASS**, 1.20 s.
- All three architecture/dependency/platform commands: PASS (exit 0).
- ADV build: PASS. Existing vendor-header pedantic warnings remain.
- `git diff --check`: PASS.
- Focused projection, engine, UI, adapter/history, controller (including ADV
  profile), physical renderer and console tests: PASS. The final extra adapter
  missing-get_info discovery case was rebuilt and rerun separately: 1/1 PASS.
- Initial full suite exposed wrapper harnesses that extracted all code before
  main and now unintentionally included the new callback. Their parser-only
  extraction was narrowed; the pending-start mock gained the new controller
  hooks. The existing decode-policy test's stale V3.087 assertion was updated
  to the explicitly required V3.089. Product assertions were not weakened.

Serial failure comparison (no changes to serial implementation or tests):

```bash
mkdir -p /tmp/T089-baseline-test
# Export unchanged current main without touching the working branch:
git archive main | tar -x -C /tmp/T089-baseline-test
cmake -S /tmp/T089-baseline-test -B /tmp/T089-baseline-build
cmake --build /tmp/T089-baseline-build --target linux_serial_unit -j"$(nproc)"
ctest --test-dir /tmp/T089-baseline-build -R '^linux_serial_unit$' --output-on-failure
```

The first unchanged-main run passed. Ten direct executions of each binary then
produced **1 pass / 9 failures on unchanged main and 1 pass / 9 failures on T089**,
all at the same line-67 assertion. This is recorded as a pre-existing flaky PTY
queue assumption; it remains unresolved and outside T089 scope. The full suite
was not reported as green.

Local logs: `/tmp/T089-linux-final-ctest.log`, `/tmp/T089-unit-ctest.log`,
`/tmp/T089-focused.log`, `/tmp/T089-recheck.log`, `/tmp/T089-baseline-serial.log`,
`/tmp/T089-adv-before.log`, `/tmp/T089-adv-after.log`.

### ADV build/resource evidence

Built current main before production edits, copied its map/bin to
`/tmp/T089-before.map` and `/tmp/T089-before.bin`, then compared the T089
`platform/adv/build/minishell_adv.map` and `.bin` with the same configuration:

| Resource | Before | T089 | Delta |
| --- | ---: | ---: | ---: |
| Firmware binary bytes | 1,394,656 | 1,396,832 | +2,176 |
| `.dram0.data` bytes | 27,528 | 27,528 | 0 |
| `.dram0.bss` bytes | 45,488 | 45,536 | +48 |

New permanent static SRAM is the generic raster callback table (12 bytes),
Display table growth (8 bytes including ABI alignment), two private port hooks
(8 bytes), and section/alignment effects, totaling the measured +48 BSS bytes.
No FT8 pixel history or RGB framebuffer is static. The optional history requests
**4,568 bytes**: 4,560 intensity bytes plus head/visible state and padding; one
tracked allocation, freed at shutdown. Conversion uses a **480-byte stack RGB
row**, plus a transient 240-byte projected intensity row at the presentation
callback. Small controller/engine generation/observer fields also reside in
existing app allocations. No full 240x19 RGB565 store exists.

Hardware `V -> Memory` with FT8 active is **not measured in this environment**:

| Runtime metric | Before / T089 |
| --- | --- |
| Heap free | Pending architect ADV measurement |
| Largest free block | Pending architect ADV measurement |
| App allocated | Pending architect ADV measurement |
| Allocation count | Pending architect ADV measurement |

The packet's approximate prior 87/31 KiB observations are not substituted for
actual before/after measurements.

### Hardware/manual validation still required

Architect must run the packet's complete 15-step ADV/QMX checklist after software
review: V3.089 identity; normal RX/decode with W off; exact y=0..18 geometry and
unchanged separator/message rows; yellow activity and 160-ms row progression;
expected static discard/TX interval; immediate toggle restoration; screen return
latching; paging/selection; active-band decode yield; UAC high-water/overflow/
discontinuity diagnostics; actual Memory metrics; and normal exit/resource cleanup.

### Known limitations or risks

- Physical color, refresh cost and UAC/decode continuity require real ADV/QMX
  evidence. Stop if refresh harms the accepted baseline; no worker/task tuning
  was added speculatively.
- One existing flaky Linux serial test prevents an all-green full suite.
- Hardware visual/Memory evidence is pending, not an implementation acceptance.

### Commit reference

This single implementation commit on `codex/T089-ft8-waterfall-overlay` contains
this evidence (resolve with `git rev-parse codex/T089-ft8-waterfall-overlay`). The
exact pushed SHA is supplied in the accompanying handoff; a commit cannot embed
its own SHA in its tree. Stop at **REVIEW**. No PR and no merge.


## Supervisor-review amendment — 2026-10-17

Status remains **REVIEW**. This additional review commit addresses both blockers
in the original implementation commit
`483347b40abdd5ac1a7fd964f4b545945e7f15ff` on the same
`codex/T089-ft8-waterfall-overlay` branch. It supersedes the original allocation
ordering and session-initialization evidence above.

### Implementation summary

- `ui_shell_init()` now explicitly sets `waterfall_supported = false` and
  `waterfall_enabled = false`. The focused test poisons the whole UiShell with
  `0xA5` before init, asserts both flags are false, and also checks reinitializing
  a previously supported/enabled shell. Discovering support in the new session
  still leaves the overlay OFF.
- `ft8_ui_adapter_init()` only discovers optional raster/Memory capability and
  geometry; it performs no allocation. New
  `ft8_ui_adapter_waterfall_prepare()` allocates/zeros history separately and
  returns false on unavailable capability or allocation failure. Repeated prepare
  after success reuses the allocation.
- `ft8_main` calls prepare only after `app_controller_start_rx()` succeeds and
  decode-worker startup succeeds. Only successful prepare enables UiShell
  support and registers the waterfall observer. Pending/failed CAT, failed RX,
  failed worker startup, and a session without RX allocate no history. Optional
  failure leaves the normal FT8 UI/RX lifecycle usable and registers no observer.

### Files changed

- `apps/ft8/main/ft8_main.c`
- `apps/ft8/main/ft8_ui_adapter.c`
- `apps/ft8/main/ft8_ui_adapter.h`
- `apps/ft8/src/ui_shell/ui_shell.c`
- `docs/MiniFT8/architecture.md`
- `tests/ft8_pending_start_test.c`
- `tests/ft8_ui_smoke.c`
- `tests/ft8_waterfall_adapter_test.c`
- This task packet.

### Behavior/invariants preserved

No changes to FT8 engine/workspace allocation, UAC/ring, decoder scheduling,
raster API/provider, projection, or V3.088 timing. About stays V3.089. History
size, palette, rendering order, hidden-history updates, W semantics and teardown
remain unchanged. This amendment follows the supervisor's specified startup
ordering; there are no additional scope/architecture deviations.

### Tests run and results

Re-ran the complete T089 gates from repository root:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T089-build-unit
cmake --build /tmp/T089-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T089-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

- Linux configure/build: PASS.
- Complete Linux CTest: **130/131 pass**, 60.51 s. Sole failure remains
  `linux_serial_unit` at the existing line-67 PTY queue-saturation assertion;
  unchanged-main reproduction is recorded in the original evidence. No serial
  implementation/test changes were made to mask it.
- Standalone unit configure/build/CTest: **29/29 PASS**, 1.12 s.
- Architecture, FT8 dependency and FT8 platform checks: PASS, exit 0 each.
- ADV build and diff whitespace check: PASS.
- Focused CTest command after final rebuild:
  `PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R
  'ft8_pending_start|ft8_waterfall_adapter|ft8_ui_smoke'`: **3/3 PASS**.
- Adapter tests explicitly prove valid-raster init allocates nothing; only prepare
  allocates; absent/undersized raster never allocates; failed prepare leaves
  support false and normal text rendering usable; successful prepare initializes
  black history and teardown frees it.
- The real lifecycle/adapter harness now advertises valid raster. Its allocator
  asserts RX succeeded and worker startup succeeded before every optional
  allocation. Observer registration asserts successful allocation and the same
  startup order. RX/worker failures and indefinitely pending control assert no
  allocations/observers; allocation failure still completes normal input and
  worker cleanup with a successful FT8 exit.

Logs: `/tmp/T089-amend-linux-ctest.log`, `/tmp/T089-amend-unit-ctest.log`,
`/tmp/T089-amend-focused.log`, `/tmp/T089-amend-linux-build.log`,
`/tmp/T089-amend-final-build.log`, `/tmp/T089-amend-adv-build.log`.

### ADV build/resource evidence

Compared the original T089 binary/map copied before amendment to the rebuilt
candidate with the same configuration:

| Resource | Original T089 | Amended T089 | Amendment delta |
| --- | ---: | ---: | ---: |
| Firmware binary bytes | 1,396,832 | 1,396,848 | +16 |
| `.dram0.data` bytes | 27,528 | 27,528 | 0 |
| `.dram0.bss` bytes | 45,536 | 45,536 | 0 |

No new permanent static SRAM. Optional history remains 4,568 requested bytes,
now allocated after critical startup, with the same single allocation/free and
480-byte temporary RGB row. Relative to pre-T089 main, firmware is +2,192 bytes
and BSS remains +48 bytes. Hardware V -> Memory values (heap free, largest block,
app allocated bytes and allocation count) remain unmeasured/pending; no runtime
memory claims are inferred from build sizes.

### Hardware/manual validation still required and known risks

The original complete ADV/QMX visual, startup/Memory, UAC continuity, decode-yield
and exit checklist still requires architect evidence. In particular, validate
successful critical RX/UAC startup before optional waterfall preparation on real
ADV, and check Memory after that preparation. No hardware was flashed/tested in
this amendment. The unrelated flaky serial assertion remains the only software
gate failure. These two supervisor blockers are fixed; hardware acceptance is
not claimed.

### Commit reference

Additional review commit on `codex/T089-ft8-waterfall-overlay`, parent
`483347b40abdd5ac1a7fd964f4b545945e7f15ff`. Resolve the amended review SHA with
`git rev-parse codex/T089-ft8-waterfall-overlay`; the exact pushed SHA accompanies
the handoff. No PR and no merge. Stop at **REVIEW**.


## Supervisor review — software PASS, hardware TESTING

Reviewed the complete T089 branch through
e0e6c741da6865dab77a834c088b27d49e892762, including the original implementation
and supervisor-review amendment.

Software review PASS.

The two review blockers are resolved:

- UiShell now initializes waterfall support/enabled deterministically OFF on every
  new session; regression coverage poisons prior state and proves reset.
- Raster capability discovery remains non-allocating. The optional 4,568-byte
  waterfall history is prepared only after successful RX/UAC startup and successful
  decode-worker startup. Allocation failure leaves FT8 operational and registers
  no waterfall observer.

Full review also confirms:

- pinned-V2 first-time-subrow / MAX-freq-osr / integer MAX-pool projection is
  preserved;
- no second FFT or decoder-waterfall copy was added;
- current V3.088 slot timing, UAC/ring, decoder task/priority/stack, CAT/TX and
  logging policies are unchanged;
- MiniShell raster semantics remain generic RGB565 transport; ADV alone owns LCD
  byte order; Linux remains text-only;
- waterfall history is optional heap state, not foreground-stack or static pixel
  storage;
- normal text render precedes raster overlay and W restores row-0 text through the
  ordinary frame path;
- the known linux_serial_unit failure is pre-existing/flaky and reproduced on
  unchanged main; no serial code or assertion was weakened.

Accepted software evidence:

~~~text
Linux build                 PASS
Linux CTest                 130/131
sole failure                known flaky linux_serial_unit
portable/unit suite         29/29 PASS
architecture checks         PASS
ADV build                   PASS
git diff --check            PASS

amendment resource delta:
firmware                    +16 bytes
.dram0.data                 +0
.dram0.bss                  +0
~~~

T089 now moves to TESTING. Remaining acceptance is the task packet's real ADV/QMX
hardware checklist: visual geometry/color/160-ms progression, W toggle/screen
latching, runtime Memory values, UAC continuity, decode behavior, and clean exit.

## Hardware finding — periodic hidden-row flash

Architect hardware test on V3.089 reports the waterfall is otherwise nearly
perfect, but the normal text status row flashes through the waterfall once per
second.

Cause:

~~~text
UTC second changes
    -> UiFrame row 0 changes
    -> whole-frame memcmp changes
    -> ft8_ui_adapter_render()
    -> text present() physically redraws row 0
    -> waterfall is immediately painted back
~~~

The waterfall is therefore correct; the flash is caused by unnecessarily
presenting a text row that is physically hidden by the active raster overlay.

### Required amendment

Do not change MiniShell raster semantics, ADV backend, FFT/projection, waterfall
history, UAC, decode timing, or task scheduling.

Add a small presentation-level frame comparison policy in ft8_main.c (or an
equally narrow portable helper):

- if old and new frames are exactly equal, no render;
- if both old and new frames have waterfall_overlay == true, ignore differences
  in rows[0] and row_color[0] when deciding whether a physical text render is
  required;
- all other frame fields remain significant:
  - overlay state;
  - geometry/footer facts;
  - rows 1..N;
  - row colors 1..N;
  - separator presence/color;
- even when physical rendering is skipped, update the cached rendered_frame to
  the new logical frame.

This gives the intended behavior:

~~~text
waterfall ON + UTC second changes only
    update cached logical row 0
    no text present()
    no flash

waterfall ON + RX content/separator/screen changes
    normal text render/present
    waterfall redrawn afterward

W ON -> OFF
    overlay flag changes
    physical render occurs
    current cached status row is restored immediately

leave RX / enter submenu
    overlay flag changes
    physical render occurs normally
~~~

Do not suppress actual model/UI changes below row 0. Do not alter UiFrame's
normal top-row contents; the status row remains logically current while hidden.

### Regression coverage

Add focused tests proving:

- successive overlay-enabled frames differing only in row 0 do not call the
  physical renderer;
- the cached logical frame still advances to the new row-0 value;
- a rows 1..6 change while overlay is enabled still renders;
- separator-color change still renders;
- overlay ON->OFF renders and restores the newest row-0 value;
- non-waterfall frames retain the existing full-frame comparison behavior.

This amendment is a hardware-driven presentation fix within T089. T089 remains
TESTING until the architect verifies the once-per-second text flash is gone and
the rest of the existing hardware checklist remains clean.


## Hardware-flash amendment implementation — 2026-10-17

Status: **REVIEW**. Continued on the existing
`codex/T089-ft8-waterfall-overlay` branch, fast-forwarded to current main
`09bc98ac81b750b65768a481e1844ba4bc573ce7` before editing. This addresses only the
periodic hidden-row flash finding above. Architect hardware acceptance remains
pending; the earlier TESTING notes are historical.

### Implementation summary and files changed

- `apps/ft8/main/ft8_main.c`: added a small private presentation comparison. When
  both old/new UiFrame overlays are true, it excludes exactly `rows[0]` and
  `row_color[0]` from comparison. Byte ranges covering every other field remain
  significant, including geometry/footer, rows/colors 1..N, overlay state and
  separators. Other frames retain full-frame memcmp behavior. No extra UiFrame
  copies or allocation are needed for the comparison.
- The physical-render helper always advances the cached logical frame after a
  successful decision, including when physical rendering is skipped. An overlay
  transition therefore renders normally using the latest logical status row.
- `tests/ft8_pending_start_test.c`: focused regression executes these production
  helpers with the actual adapter and recording Display callbacks.
- This packet: implementation, validation and resource evidence appended.

### Behavior/invariants preserved

UiFrame contents remain intact. Only the physical-render decision hides row-0
text/color when both overlays are active. Other UI changes still run the existing
text-present-then-overlay adapter path. Raster API/backend, waterfall DSP/history,
optional allocation/startup ordering, UAC/ring, decode timing and task scheduling
are unchanged. About remains MiniFT8-V3.089. No requirement or ownership deviation.

### Regression evidence

The extended lifecycle test proves:

- first and overlay-transition frames render; identical frames do not;
- successive overlay-enabled row-0 text/color changes produce no additional
  physical presents while the complete cached logical frame matches each update;
- every message row 1..6 and its color still triggers rendering;
- separator color/presence and footer changes still render; geometry changes
  remain significant in comparison;
- an additional hidden status update to Z leaves the physical text unchanged,
  advances the cache to Z, and overlay ON->OFF physically restores Z immediately;
- without the overlay, row-0 text and color changes retain full-frame behavior.

Existing adapter/history tests continue to cover repainting the waterfall after
ordinary text rendering. The new decision tests make no raster/DSP changes.

### Tests run and results

Complete normal T089 gates were run from repository root:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T089-build-unit
cmake --build /tmp/T089-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T089-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

Linux configure/build: PASS. Full Linux CTest: **130/131**, 59.70 s; only
`linux_serial_unit` failed at the same pre-existing line-67 PTY saturation
assertion documented and reproduced on unchanged main above. Serial code/tests
were left untouched. Standalone unit suite: **29/29 PASS**, 1.15 s. All three
architecture/boundary commands: PASS (exit 0). ADV build and diff check: PASS.

Focused command:
`PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R
'ft8_pending_start|ft8_waterfall_adapter|ft8_ui_smoke'`: **3/3 PASS**.

Logs: `/tmp/T089-flash-linux-config.log`, `/tmp/T089-flash-linux-build.log`,
`/tmp/T089-flash-linux-ctest.log`, `/tmp/T089-flash-unit-ctest.log`,
`/tmp/T089-flash-focused.log`, `/tmp/T089-flash-adv-build.log`.

### ADV build/resource evidence

Same-configuration comparison with the pre-amendment binary/map copied to
`/tmp/T089-flash-before.bin` and `/tmp/T089-flash-before.map`:

| Resource | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Firmware binary bytes | 1,396,848 | 1,397,008 | +160 |
| `.dram0.data` bytes | 27,528 | 27,528 | 0 |
| `.dram0.bss` bytes | 45,536 | 45,536 | 0 |

No new permanent static SRAM or allocation. History remains the existing optional
4,568-byte store. No new hardware Memory measurements are claimed.

### Hardware/manual validation still required and risks

Architect must confirm that UTC-second-only changes no longer flash status text
through the overlay on ADV/QMX, while message updates, separators, screen/submenu
transitions, W off/current status restoration, and normal waterfall progression
remain correct. Continue the original Memory/UAC/decode/exit checklist. Host
renderer evidence cannot establish physical LCD flash removal. The known flaky
Linux serial test remains the only software gate failure.

### Commit reference

Additional amendment commit on `codex/T089-ft8-waterfall-overlay`, based on
`09bc98ac81b750b65768a481e1844ba4bc573ce7`. Exact pushed SHA accompanies the
handoff; resolve branch HEAD with `git rev-parse codex/T089-ft8-waterfall-overlay`.
Stop at **REVIEW**. No PR and no merge to main.

## Supervisor review — hidden-row flash amendment PASS

Reviewed amendment `a1171d74ad65afb4997c8cb18c4a2e5f4970e9f4`.

Software review PASS.

The physical-render comparison now suppresses only changes to `rows[0]` and
`row_color[0]` when both old and new frames have the active waterfall overlay.
All remaining UiFrame bytes remain significant. The logical cached frame is
updated even when the physical render is skipped, so W-off restores the newest
status immediately. Overlay transitions fall back to full-frame comparison.

The byte-range implementation is valid for the current UiFrame layout: the
ignored elements are contiguous array elements, while geometry, footer/overlay
facts, rows 1..N, colors 1..N, separator fields and intervening padding remain
compared. Production `ui_shell_render()` zero-initializes UiFrame before filling
it, so compared padding is deterministic.

No raster API/backend, DSP, waterfall history, UAC, decoder, timing, allocation
ordering, or task-scheduling behavior changed.

Accepted software evidence remains:

~~~text
Linux/ADV builds            PASS
unit suite                  29/29 PASS
architecture checks         PASS
focused regressions         PASS
full Linux CTest            130/131
sole failure                known flaky linux_serial_unit
~~~

T089 remains TESTING pending ADV/QMX confirmation that the once-per-second
status flash is gone and the existing visual/UAC/decode/Memory checklist remains
clean.

## Hardware finding — RX-batch refresh still flashes hidden row 0

After the once-per-second status-only flash was removed, ADV/QMX hardware still
shows a brief row-0 status flash when an RX batch refreshes rows 1..6. This is
expected from the current backend behavior:

~~~text
RX rows 1..6 change
    -> MiniFT8 must render the text frame
    -> ADV display present() calls render_all()
    -> physical row 0 is redrawn even though waterfall owns y=0..18
    -> waterfall is then painted back
~~~

The remaining problem is therefore not frame comparison. It is that ADV text
`present()` redraws every physical text row instead of only rows modified through
the MiniShell Display API.

### Final T089 amendment — split row ownership

Adopt this physical ownership while the waterfall overlay is active:

~~~text
W OFF:
    text owns physical rows 0..6

W ON:
    raster owns physical y=0..18 (logical text row 0)
    text owns logical/physical rows 1..6
    separator y=19..20 remains text/backend-owned
~~~

This is preferable to blanking row 0. Row 0 must simply not be touched physically
while the raster overlay owns it.

### MiniFT8 adapter rule

When `frame->waterfall_overlay == false`, retain the normal full text-frame path.

When `frame->waterfall_overlay == true`:

- do not call `text->clear()`, because clearing the full logical text surface
  dirties/resets row 0;
- do not call `write_at()` or `write_at_attr()` for row 0;
- write the fixed-width rows 1..N normally;
- update the separator normally when required;
- call `display->present()`;
- redraw/update the waterfall only through the existing raster path;
- keep the logical UiFrame row 0 current in the cached application frame so W-off
  restores the latest status immediately.

The ADV profile uses fixed-width complete rows, so rows 1..6 do not require a
preceding full `clear()` before being rewritten.

### ADV backend dirty rendering

Current ADV `display_present()` calls `render_all()`. Change only the MiniShell
Display API path to track dirty text rows and separator state.

Required private backend behavior:

~~~text
display_text_write_at(row, ...)
display_text_write_at_attr(row, ...)
display_text_clear_at(row range, ...)
    -> update logical text buffers
    -> mark affected row(s) dirty

display_text_clear()
    -> clear logical text buffers/separator
    -> mark all rows + separator dirty

display_set_row_separator()
    -> update separator state
    -> mark separator dirty

display_present()
    -> render only dirty rows
    -> render separator only if dirty
    -> clear dirty flags
~~~

Do not add a new public MiniShell API or capability. `present()` still means
"make preceding logical Display API changes visible"; dirty tracking is an ADV
implementation optimization that makes this contract more precise.

### Resident console paths

Do not accidentally change resident shell/editor behavior. Existing private
console operations may continue to use `render_all()` directly:

~~~text
initial display setup
console clear/write
console editor redraw
scrollback redraw
~~~

The dirty-row mechanism is specifically for the application-visible MiniShell
Display text API and its `present()` path.

### Interaction with prior T089 row-0 comparison

Keep the earlier hidden-row comparison optimization. It remains useful:

- UTC-second-only row-0 changes while W is ON require no adapter render at all;
- RX content changes require adapter rendering, but the new split/dirty path
  updates only rows 1..6 and never touches physical row 0.

Together:

~~~text
UTC second changes only
    -> no text render

RX batch changes
    -> rows 1..6 dirty
    -> present rows 1..6 only
    -> row 0 untouched

W OFF
    -> full normal text path
    -> row 0 becomes dirty/rendered with current cached status
~~~

### Required regressions

Add/extend tests proving:

- ADV Display API writes to row 1 mark/render row 1 but not row 0;
- writes to multiple rows render exactly those rows;
- `clear_at()` dirties only its affected rows;
- full `clear()` dirties all rows and separator;
- separator-only change does not render row 0;
- `present()` clears dirty state so a second present without writes does no row
  rendering;
- private console/editor paths still redraw their complete view;
- MiniFT8 overlay render writes rows 1..6 only and does not call text clear or
  row-0 write;
- RX batch refresh with overlay enabled performs no physical row-0 text draw;
- W-off uses the normal full path and restores current row 0;
- Linux behavior remains unchanged.

Do not modify raster API/backend semantics, waterfall DSP/history, allocation
ordering, UAC, CAT/TX, decode scheduling, slot timing, or task priorities.

This is still T089. Keep status TESTING until real ADV/QMX confirms that both
the per-second and per-RX-batch row-0 flashes are gone.


## Final hardware-finding amendment implementation — 2026-10-17

Status: **REVIEW**, as requested for this engineering handoff. Continued on the
existing `codex/T089-ft8-waterfall-overlay` branch, fast-forwarded to current main
`a01558eab47aeea4c2a67f9c0dc550ad03894c05` before implementation. The prior TESTING
notes describe hardware acceptance still outstanding; no new hardware acceptance
is claimed by this amendment.

### Implementation summary

- Overlay-active MiniFT8 adapter rendering skips full text clear and skips row 0
  for both plain and attributed writes. It writes complete fixed-width rows 1..6,
  updates/removes the separator, presents text, and uses the existing raster
  repaint path. Overlay-off rendering retains full clear and all seven rows.
- ADV's MiniShell Display API marks rows dirty on non-zero plain/attribute writes
  and clear_at row ranges. Full clear dirties all rows plus separator; separator
  updates dirty only the separator. `adv_display_present()` draws those elements
  only and consumes their flags. Repeated present without writes draws no text.
- Private setup/console/editor/scrollback full redraws still call `render_all()`.
  A full redraw also consumes pending dirty state because it has made the entire
  logical view visible. Existing cursor-cell rendering is unchanged.
- The earlier hidden-row comparison/cache optimization in ft8_main is unchanged.

### Files changed

- `apps/ft8/main/ft8_ui_adapter.c`
- `platform/adv/adv_display.cpp`
- `tests/adv_display_color_test.py`
- `tests/ft8_color_adapter_test.c`
- `tests/ft8_pending_start_test.c`
- `tests/ft8_waterfall_adapter_test.c`
- `docs/MiniFT8/ui.md`
- `docs/api/display-api.md`
- This task packet.

### Behavior/invariants preserved

No public API/capability or raster semantics/provider changes. DSP, waterfall
projection/history, UAC/ring, CAT/TX, decode/slot timing, optional allocation
ordering and tasks are unchanged. About remains MiniFT8-V3.089. Linux uses the
unchanged normal text path because it has no overlay support. Logical UiFrame
row 0 stays current even though its provider text row is untouched while overlaid;
W-off restores the current frame through the full text path. No scope deviation.

### Regression evidence

- Actual ADV renderer host test records exactly row 1 for a row-1 write, exactly
  rows 2/5 for multiple writes, and exactly rows 3/4 for clear_at. Separator-only
  updates draw just the y=19 separator. Full clear draws all seven rows plus gap.
  Second presents and zero-sized edits produce no physical text draw.
- Private console write/clear, editor begin/line/discard, and scrollback redraw
  still draw all seven rows plus separator; the existing full console/editor
  regression also passes.
- Added adapter-to-ADV integration using the actual C adapter/presentation sources
  linked to the actual C++ backend with only M5 hardware stubbed. Three simulated
  RX batch refreshes each produce exactly six text rows (120 cells), the separator,
  and existing raster repaint, with no physical y=0 text draw. Provider row 0
  retains its prior text. W-off draws all rows and restores the newest status N.
- Plain, attributed, missing-tail and unsupported-attribute fallback adapter tests
  prove overlay rendering never clears or writes row 0; rows 1..6 still update,
  separator removal works without clear, and W-off resumes the normal full path.
- Existing hidden-row comparison regression still proves no render on row-0-only
  changes and cache advancement/restoration. Its known physical-row seed is now
  explicit; the stale expectation that an overlay batch painted C was replaced
  with the required unchanged physical text. All comparison/render-count checks
  remain intact.

### Tests run and results

Normal T089 gates from repository root:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T089-build-unit
cmake --build /tmp/T089-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T089-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

Linux configure/build: PASS. Full Linux CTest: **131/131 PASS**, 59.67 s (the
previously documented flaky serial test passed this run; its code/test was not
changed). Standalone configure/build/unit suite: **29/29 PASS**, 1.14 s. All three
architecture/boundary commands: PASS, exit 0 each. ADV build and diff check: PASS.

Focused command:
`PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R
'adv_display_color|adv_console_scrollback|ft8_pending_start|ft8_waterfall_adapter|ft8_color_adapter|ft8_ui_smoke'`:
**6/6 PASS**, 0.78 s.

Logs: `/tmp/T089-dirty-linux-config.log`, `/tmp/T089-dirty-linux-build.log`,
`/tmp/T089-dirty-linux-ctest.log`, `/tmp/T089-dirty-unit-ctest.log`,
`/tmp/T089-dirty-focused.log`, `/tmp/T089-dirty-adv-build.log`.

### ADV build/resource evidence

Compared the same-configuration pre-amendment binary/map saved at
`/tmp/T089-dirty-before.bin` and `/tmp/T089-dirty-before.map`:

| Resource | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Firmware binary bytes | 1,397,008 | 1,397,232 | +224 |
| `.dram0.data` bytes | 27,528 | 27,528 | 0 |
| `.dram0.bss` bytes | 45,536 | 45,536 | 0 |

New permanent backend state is seven bool row flags plus one bool separator flag
(8 bytes). Linked section sizes remain unchanged due to layout/alignment effects;
this is not a claim that the flags consume zero storage. No new heap allocation
or framebuffer. Existing optional history and its post-critical-startup ordering
are unchanged. No new hardware Memory figures are inferred from build sizes.

### Hardware/manual validation still required and known risks

Architect must verify on ADV/QMX that both UTC-second and RX-batch status flashes
are gone, that rows 1..6/separator and waterfall still update correctly, that W-off
restores current status, and that screen/submenu/console/editor transitions remain
clean. Continue original UAC/decode/Memory/exit checks. Host tests establish draw
ownership/call ordering, not physical LCD acceptance. No hardware was flashed or
new runtime measurements collected in this amendment. No known new software
limitation; prior serial flakiness remains documented despite passing this run.

### Commit reference

Additional amendment commit on `codex/T089-ft8-waterfall-overlay`, based on
`a01558eab47aeea4c2a67f9c0dc550ad03894c05`. Exact pushed SHA accompanies the
handoff; resolve with `git rev-parse codex/T089-ft8-waterfall-overlay`.
Stop at **REVIEW**. No PR and no merge to main.
