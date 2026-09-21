# T042 — Mini-CW Keyer-mode MiniShell foundation

Status: TESTING

## Objective

Create the first real Mini-CW application port under MiniShell.

Reference source and behavior:

```text
repository: wcheng95/Mini-CW
commit:     3bfbf169b7c2d49a1be3e9a4c80f945edb32033e
```

Read:

```text
docs/MiniCW/migration.md
AGENTS.md
include/minishell/api.h
```

T042 is **not** a rewrite of the existing `apps/keyer`.

The goal is to preserve Mini-CW service/state behavior while replacing direct
platform ownership.

## Scope

Implement a new application:

```text
apps/minicw/
```

and an ADV external ELF target:

```text
platform/adv/elf_apps/minicw/
```

T042 supports only Mini-CW **Keyer mode** and the minimum UI/runtime needed to
exercise it.

Audio is intentionally silent in T042. T043 owns the known-good Mini-CW audio
migration.

## Source migration rule

Use the pinned Mini-CW source as the reference.

Preserve names/structure where practical:

- `app_core`
- `keyer_service`
- `keyer_decoder`
- relevant `ui_service` / `ui_screen` behavior

Do not mechanically copy Cardputer/ESP-IDF code.

When a Mini-CW module mixes domain logic and platform access, split a private
port seam rather than changing domain semantics.

Document meaningful deviations from the pinned source in this packet.

## MiniShell service mapping

### Time

Replace:

- `esp_timer`
- FreeRTOS delay/ticks used only as time/polling

with MiniShell Time/Location:

- `monotonic_us()`
- `sleep_ms()`

Do not change key timing constants.

### KeyIn / KeyOut

Replace raw Mini-CW GPIO access with MiniShell Digital I/O.

Use the existing ADV deployment lines unless the pinned Mini-CW reference proves
a different required mapping:

```text
G13 KeyIn tip
G15 KeyIn ring
G3  KeyOut tip
G6  KeyOut ring
```

Preserve Mini-CW KeyIn/KeyOut semantics and shutdown release safety.

### Display

Adapt Mini-CW Keyer-mode frame/state to MiniShell text Display.

T042 may use the existing 20x7 MiniShell Display abstraction; do not call
M5Cardputer/M5Unified from the application.

Preserve Mini-CW Keyer-mode user-visible content/flow as closely as the 20x7 API
allows. Record any rendering difference caused solely by the abstraction.

### Input

Map MiniShell logical key events to Mini-CW UI events.

Do not poll raw Cardputer keyboard hardware.

### Files

T042 may use compiled/default configuration only.

Do not port persistence yet unless a tiny read-only compatibility shim is required
to make Keyer mode usable. T044 owns full storage migration.

### Audio

T042 must not use the current MiniShell Keyer sidetone implementation as a
substitute for Mini-CW audio.

Keyer audio requests may be routed to a silent private adapter/no-op while
preserving the domain calls/state required for T043.

Do not add a new public Audio API in T042.

## Application behavior

Provide a `minicw` entry that starts directly in Mini-CW Keyer mode.

Required T042 behavior:

- initialize Mini-CW Keyer-mode state;
- show Keyer UI;
- read paddle/straight-key through MiniShell Digital I/O;
- preserve Mini-CW keyer timing/decoder behavior;
- drive KeyOut through MiniShell Digital I/O;
- update decoded text/UI;
- process the subset of keyboard/UI controls required for Keyer mode;
- exit cleanly with Ctrl+C;
- release KeyOut and all MiniShell resources.

M1-M5 configuration may use pinned-reference defaults in T042 if persistence is
not yet ported.

Automatic message scheduling should remain present if it can be preserved without
audio. The hardware test is silent; T043 adds sound.

## Architecture constraints

External `minicw.elf` must not import or include application dependencies on:

- ESP-IDF
- FreeRTOS
- M5Cardputer / M5Unified
- driver/gpio
- driver/uart
- FATFS / dirent platform filesystem
- TinyUSB
- Mini-CW `board_cardputer_adv`

Use MiniShell APIs only.

The preferred external resident import remains:

```text
mini_api_get
```

Compiler helpers may be linked into the ELF as with Keyer.

No changes to existing `apps/keyer`, MiniFT8, or their behavior.

## Portable tests

Add host tests for the imported/adapted Mini-CW Keyer behavior.

At minimum prove:

- Mini-CW keyer decoder vectors/gesture behavior preserved;
- Iambic A/B/Bug behavior preserved where covered by the reference;
- physical preemption/cancel behavior preserved;
- KeyOut mode semantics and final release;
- UI input mapping for Keyer mode;
- decoded-history updates;
- monotonic timing comes through the injected MiniShell seam;
- no platform headers in application dependency boundary.

Where Mini-CW already has useful deterministic tests/vectors, port/reuse their
behavior rather than inventing new expected results.

## Build gates

Run:

- full Linux MiniShell CTest;
- portable/unit suites;
- architecture/dependency/platform boundary checks for `minicw`;
- real ADV firmware build;
- clean `platform/adv/elf_apps/minicw` external ELF build;
- `readelf -rW` import inspection;
- `git diff --check`.

Record exact ELF size/sections/imports.

## Hardware acceptance

After supervisor review, install `minicw.elf` on ADV and verify:

1. MiniShell launches `minicw`.
2. Mini-CW Keyer-mode screen appears.
3. Paddle/straight-key input is responsive.
4. decoded text updates.
5. physical KeyOut behavior matches the pinned Mini-CW reference.
6. automatic M1/M2 selection works silently if implemented in T042.
7. Ctrl+C exits and both KeyOut lines are released.
8. repeated launch/exit does not leak observable resources.

No audio-quality judgment is part of T042.

## Non-goals

Do not:

- port Mini-CW audio yet;
- solve the existing MiniShell Keyer pop;
- port trainer modes;
- port GPS;
- port full persistence;
- add battery/deep-sleep API;
- add USB MSC;
- modify existing Keyer or FT8;
- redesign Mini-CW behavior.

## Branch

Use:

```text
codex/T042-minicw-foundation
```

No PR and no hardware testing by Codex.

Set T042 to REVIEW, push, and return the exact SHA with implementation summary,
tests, resource evidence and deviations from the pinned Mini-CW source.

## Implementation handoff

Implemented on `codex/T042-minicw-foundation` from
`1448d6d7cc4ceec18ee3072df979c4255ea864e1`. Commit reference: the single
implementation commit containing this handoff (exact SHA returned with push).

### Implementation summary and files changed

- Added `apps/minicw/`: pinned Mini-CW `keyer_service` and unchanged
  `keyer_decoder`, Keyer-only `app_core`/`ui_service`, text `ui_screen`, private
  MiniShell port, silent audio seam, local allocation-free runtime helpers and
  source/build documentation. Source provenance and controls are in
  `apps/minicw/README.md`.
- Added `platform/adv/elf_apps/minicw/`: external ELF build, pinned elf_loader
  1.3.3 dependency, local libgcc division helper linkage and data-section padding
  for the existing section loader. No resident loader changes.
- Added `tests/minicw_domain_test.c`, `tests/minicw_runtime_test.c`, shared
  `tests/minicw_tests.cmake`, and `tests/minicw_elf_inspect.py`. Registered the
  application and tests in root CMake, portable units and architecture rules.
- Updated this task packet only for the implementation handoff/status.

### Behavior and invariants preserved

The Keyer decoder source/header match the pinned Mini-CW source byte-for-byte:

```text
keyer_decoder.c SHA256 00bfe7991c43a1aea4a8bbb6b8fdd48f31da19807ab72ad585f237914e986ac3
keyer_decoder.h SHA256 fbfafcfb545a87a7d1ab86ac4c5cc4722301e74bfd25467848aa1a4562e91441
```

Preserved Mini-CW's four KeyIn modes, five KeyOut modes (including paddle outputs
and SK-M), adaptive straight-key timing, Iambic/Bug state machines, consumed
physical cancel press, bounded automatic FIFO, TxDelay, M1 repeat, Tune timeout,
settings editors, five-line decoded history, memory overlay and visible TX tail.
In particular, the pinned source intentionally applies squeeze-release's extra
element to its **Iambic A** selection rather than B; the port preserves this.
No behavior was taken from `apps/keyer`.

The pinned, tracked Mini-CW `sdkconfig` specifies `CONFIG_FREERTOS_HZ=100`.
Private ticks therefore retain 10 ms quantization, integer truncation/minimum
one-tick delay and 32-bit wrap arithmetic. Time comes exclusively from MiniShell
`monotonic_us()`; the pinned 5 ms polling request rounds to one 10 ms tick and
is represented by `sleep_ms(10)`. WPM/gap constants are unchanged.

G13/G15 input pull-ups and G3/G6 active-low open-drain outputs use Digital I/O
handles only. Ctrl+C exits every view, including active Tune and delayed TX.
Normal and error cleanup explicitly release both output wires, including SK-M's
ring, before closing handles. Partial acquisition unwinds previously opened
handles. Repeated launches reset all application state to reference defaults.

Public API v3, resident code, existing `apps/keyer/**`, `apps/ft8/**`, USB,
WebFS, Audio providers and firmware configuration are unchanged. There are no
application tasks/threads, direct platform calls, application heap allocations,
file access or speaker/Audio acquisition.

### Deliberate T042 differences from the pinned source

- Keyer mode only; compiled defaults and in-session edits. No persistence/logs,
  OP-table loading, trainer, GPS, clock editing, USB drive or power modes. Opt's
  mode selector is inactive. Storage/config formats are not introduced.
- Audio is silent. The private seam preserves finite-tone/hold/busy bookkeeping
  and the pinned Morse table; it does not synthesize PCM or call MiniShell Audio.
- Text is mapped to 20x7 normal attributes. The source's white/green/cyan colors
  and pixel separator have no public text-Display equivalent and are omitted.
  Unchanged rows need not be written/presented again.
- Input consumes logical MiniShell events. Raw keyboard release/held state is
  unavailable: cursor repeats use delivered events, and the source's one-second
  Backspace-hold clear gesture is not synthesized. Backspace editing, Ctrl menu,
  Alt overlay, logical arrows/Fn arrows, Enter, Escape/backtick, Tune and mute
  controls remain. Ctrl+C is the MiniShell application-exit addition.
- Application startup explicitly resets globals for repeatable launches, and
  failure cleanup is added around MiniShell resources. The tiny local C runtime
  avoids resident libc imports; compiler division support is linked locally.

### Tests run and results

All gates passed locally; no hardware testing was performed.

```sh
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
# PASS: 78/78

cmake -S tests/unit -B /tmp/T042-unit
cmake --build /tmp/T042-unit -j8
ctest --test-dir /tmp/T042-unit --output-on-failure
# PASS: 20/20

ctest --test-dir build-linux -R minicw --output-on-failure
# PASS: 4/4 (domain, runtime, dependency, platform)
ctest --test-dir build-linux -R 'architecture|boundary' --output-on-failure
# PASS: 11/11
python3 tests/app_dependency_boundary.py . minicw
python3 tests/app_platform_boundary.py . minicw
# PASS; all Mini-CW modules also have no-heap rules

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: real ESP32-S3 ADV firmware
idf.py -C platform/adv/elf_apps/minicw fullclean
idf.py -C platform/adv/elf_apps/minicw elf
# PASS: clean external ELF, 1073 build steps
xtensa-esp32s3-elf-readelf -rW platform/adv/elf_apps/minicw/build/minicw.app.elf
python3 tests/minicw_elf_inspect.py platform/adv/elf_apps/minicw/build/minicw.app.elf
# PASS: mini_api_get is the sole resident import; 507 mapped relocations;
# packed sections retain alignment and entry is in .text
xtensa-esp32s3-elf-size -A platform/adv/elf_apps/minicw/build/minicw.app.elf
xtensa-esp32s3-elf-size -A platform/adv/build/minishell_adv.elf

git diff --check
# PASS
```

The upstream ELF packaging strips `.dynamic`; `readelf` emits its corresponding
missing-section diagnostic while still printing relocations. The independent
ELF32 inspector reads section/symbol/relocation tables directly and passes.
The component registry required network access for the first external-target
configure; the pinned dependency resolved successfully.

Focused coverage includes all reference Morse characters and special gestures;
Iambic A/B squeeze release; Bug hold; straight tip/ring; swapped paddle inputs;
all KeyOut modes; final release; physical cancel consumed until release; active
character protection against Backspace; FIFO bounds; wraparound timing; Tune
latch/preemption; exact header, physical decoded-history display and preservation
under Alt; M1 selection/repeat/cancel; WPM and message editing; mute/Tune UI;
MiniShell monotonic/sleep use; Ctrl+C during Tune and TxDelay; repeated launches;
partial Digital I/O acquisition and read/write/Display/sleep error cleanup.

### Resource evidence

Toolchain: ESP-IDF v5.5.4 environment, Xtensa GCC 14.2.0.
Artifact: `platform/adv/elf_apps/minicw/build/minicw.app.elf`, deployed later as
`minicw.elf` by the architect after review.

| External ELF measurement | Bytes |
| --- | ---: |
| File size | 34,724 |
| `.text` | 23,432 |
| `.rodata` | 1,272 |
| `.data` (includes relocated constant pointer tables and padding) | 1,176 |
| `.bss` | 2,732 |
| `.eh_frame` | 44 |
| `.got` | 4 |
| `.hash` / `.dynsym` / `.dynstr` | 40 / 80 / 38 |
| `.rela.dyn` / `.rela.plt` | 6,084 / 12 |
| Sum reported by `size -A` | 34,914 |

The existing section loader requests 23,432 executable bytes plus 5,180 data
bytes, **28,612 bytes total**, excluding allocator/loader bookkeeping, temporary
ELF file storage and the existing foreground stack. This is code/section-derived
accounting, not an on-device heap measurement. The app creates no task or queue,
uses no application heap allocation and owns four Digital I/O handles while
running. The existing ADV 16 KiB foreground stack is unchanged.

Both baseline and final real firmware builds produced identical section sizes
and the identical BIN SHA256
`acc30c2169628de0ee3a53f501302db1d3d7d7e3a49533a2d6dcafcdfb996b5e`:

| Resident measurement | Baseline | Final | Delta |
| --- | ---: | ---: | ---: |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,000 | 27,000 | 0 |
| `.dram0.bss` | 38,864 | 38,864 | 0 |
| Static internal SRAM change | — | — | **0** |
| Firmware BIN | 1,375,776 | 1,375,776 | **0** |

### Hardware/manual validation still required and known risks

All eight hardware acceptance steps above remain for the architect after
supervisor review, including silent physical timing/KeyOut, M1/M2 selection,
Ctrl+C release and repeated-launch resource observations. No launch or timing
claim here substitutes for ADV hardware acceptance. Actual dynamic heap/stack
high-water measurements remain unmeasured. Audio quality is deliberately outside
T042. The logical-input and monochrome-rendering differences are documented
above; there are no other intended scope or ownership deviations.


## Supervisor review

Reviewed implementation commit:

```text
592addd4c97fe560bedc0adca0a89499e2118aa6
```

Result: **PASS — ready for silent ADV hardware validation.**

### Architecture/scope review

T042 is a genuine Mini-CW Keyer-mode port, not a reimplementation using
`apps/keyer`.

The production additions are isolated under:

```text
apps/minicw/
platform/adv/elf_apps/minicw/
```

plus build/tests. Existing Keyer, MiniFT8, resident ADV providers and public
MiniShell API are unchanged.

Although this branch predates the later roadmap edit that explicitly removes
trainer/power/USB scope, the implementation already satisfies that revised
Keyer-only direction: no trainer, GPS, storage, battery/sleep, USB-MSC or power
implementation is imported.

### MiniShell boundary review

All hardware ownership is concentrated in the private Mini-CW port:

- Time/Location -> monotonic time + 10 ms sleep;
- Digital I/O -> G13/G15 inputs and G3/G6 open-drain outputs;
- Input -> logical MiniShell key events;
- Display -> 20x7 MiniShell text Display.

The external application has no ESP-IDF, FreeRTOS, M5*, raw GPIO/UART, FATFS,
TinyUSB or board dependencies.

The external ELF's sole resident import is `mini_api_get`.

### Timing/domain review

The pinned Mini-CW decoder is preserved byte-for-byte.

The Keyer timing port deliberately preserves the pinned 100 Hz FreeRTOS timing
model rather than silently increasing resolution:

```text
Mini-CW tick = floor(monotonic_us / 10,000)
poll sleep   = 10 ms
```

This keeps the reference's integer truncation, minimum-one-tick delays and
32-bit wrap behavior.

The silent Audio seam is also correctly stateful. Finite dit/dah requests retain
a deadline and `audio_service_is_busy()` remains true until that deadline;
hold/release is tracked separately. Thus T042 does not make automatic/keyer
state advance instantaneously merely because speaker output is disabled.

### Resource/error lifecycle review

The port owns four Digital-I/O handles only.

On normal exit or error:

- both KeyOut wires are explicitly driven released;
- all acquired handles are closed;
- partial acquisition unwinds only acquired handles;
- repeated launches reset application state.

No application heap, task or queue is introduced.

### UI/Input deviations

The documented rendering/input differences are consequences of the current
MiniShell abstraction rather than product redesign:

- 20x7 monochrome text instead of Mini-CW color/pixel separator;
- logical key events instead of raw press/release state;
- no synthesized one-second Backspace-hold gesture;
- Opt's multi-mode selector is inactive because T042 starts directly in Keyer
  mode.

Keyer settings/menu, M1-M5 overlay, Tune, decoded history and automatic-TX state
remain present for the T042 Keyer-mode foundation.

### Software/build evidence

Reported final gates:

```text
Linux CTest              78/78
portable units           20/20
focused minicw            4/4
architecture/boundary    PASS
real ADV firmware build  PASS
clean minicw ELF build   PASS
ELF inspection           PASS
git diff --check         PASS
```

External ELF:

```text
file size                34,724 bytes
.text                    23,432
.rodata                   1,272
.data                     1,176
.bss                      2,732
mapped runtime sections  28,612 bytes
resident import           mini_api_get only
```

Resident firmware BIN and static SRAM are byte-for-byte/size identical to the
baseline.

### Hardware gate

T042 is intentionally silent. Validate only the platform/domain foundation:

1. MiniShell launches `minicw`.
2. Mini-CW Keyer screen appears.
3. paddle/straight-key input responds with reference timing;
4. decoded text updates;
5. KeyOut modes/physical behavior are correct;
6. M1/M2 automatic scheduling/UI works silently;
7. Ctrl+C releases both KeyOut lines;
8. repeated launch/exit is clean.

Audio quality is not part of T042. T043 will migrate the actual known-good
Mini-CW continuous-audio architecture and will require clean paddle + M1 as its
hardware acceptance gate.

Do not merge T042 to `main` until ADV hardware acceptance.

