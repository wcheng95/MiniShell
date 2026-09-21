# T051 — ADV resident console scrollback

Status: COMPLETE

## Baseline

Start from current production main:

```text
main
6cb48d5667275f174635c32ac7453112d98ad8f6
```

Branch:

```text
codex/T051-adv-console-scrollback
```

No PR. No hardware testing by Codex.

## Objective

Give the Cardputer ADV resident `M$>` console a bounded scrollback history so
output that has already scrolled off the 20x7 display can be reviewed.

Target behavior:

```text
history depth     50 physical 20-column console rows
Fn + Up           scroll five rows older
Fn + Down         scroll five rows newer
live position     newest seven rows
```

This is private ADV resident-console behavior. Do not change the public Console,
Display, or Input APIs.

## Existing ownership

Keep the established output split:

```text
Shell built-ins
    -> minishell_platform_console_write()
       -> USB Serial/JTAG mirror
       -> ADV resident display console/history

Application Console.write()
    -> resident Console service
    -> minishell_platform_console_write()
       -> same history

System.write()
    -> diagnostic sink / USB-debug path only
    -> NOT added to user scrollback
```

Thus `status`, `apps`, `ls`, `cat`, normal command/app output, the `M$>`
prompt, and typed command echo are retained. Internal diagnostics remain outside
the user-facing history.

Linux is unchanged; the host terminal owns Linux scrollback.

## Physical key behavior

ADV already normalizes:

```text
physical Fn + ;   -> MINI_KEY_UP   with MINI_MOD_FN
physical Fn + .   -> MINI_KEY_DOWN with MINI_MOD_FN
```

In `minishell_platform_console_read_line()`, while the resident shell owns the
keyboard:

- `MINI_KEY_UP` with `MINI_MOD_FN` scrolls five history rows older;
- `MINI_KEY_DOWN` with `MINI_MOD_FN` scrolls five rows newer;
- consume these events; they must not alter the command input buffer or echo
  characters;
- standalone Fn and all existing Enter/Backspace/Delete/character behavior remain
  unchanged.

Do not change `adv_keyboard.cpp` mappings or the public Input API.

## History model

Use a fixed resident ring. No heap allocation.

Required capacity:

```text
50 rows x 20 characters = 1000 character bytes
```

Small fixed bookkeeping/alignment overhead is acceptable.

The 50 rows are **physical console rows**, not logical newline records. Therefore
a long line that wraps at column 20 consumes additional history rows exactly as
the current display console does.

The current partial row counts as the newest history row.

Preserve current console parsing semantics:

- printable ASCII is displayed;
- `\r` ignored;
- `\n` advances to a new row;
- `\b` erases within the current row exactly as today;
- column 20 wraps to a new row;
- no ANSI/terminal escape interpreter is introduced.

When row 51 is created, discard only the oldest row.

## Viewport semantics

The physical display still shows seven rows.

At the live tail:

- render the newest seven retained rows;
- before seven rows exist, retain the current top-aligned startup behavior.

Scrolling:

- Fn+Up increases the scrollback offset by 5 rows;
- Fn+Down decreases it by 5 rows;
- clamp at oldest/newest boundaries; no wraparound;
- the command edit buffer is untouched;
- history contents are untouched.

Return to live tail automatically on **any new resident-console output**. This
includes:

- shell prompt;
- command echo;
- Backspace echo;
- Enter/newline;
- built-in command output;
- application `Console.write()`.

This guarantees that if the user starts typing while reviewing old output, the
display immediately returns to the active prompt/command.

Fn+Down to offset zero also returns to the live viewport without generating
output.

## Full-screen Display handoff

History must survive temporary foreground Display ownership.

Current full-screen applications call Display clear/write/present and set
`s_console_mode=false`. T051 must change only the restoration behavior:

- Display operations do not erase resident console history;
- when the next resident-console output arrives, console mode is restored from
  the retained newest history rows;
- then the new output is appended normally;
- console attributes remain default white and the row separator is removed, as
  today.

Do not retain full-screen application pixels/colors in console history.

Example:

```text
M$> ft8
[MiniFT8 owns Display]
Ctrl+C
M$> _
```

The restored console should include the pre-launch shell lines that are still
within the last 50 physical rows, plus the new prompt.

## USB behavior

Do not replay scrollback over USB Serial/JTAG.

`minishell_platform_console_write()` continues mirroring each output byte once
to USB as today. Fn+Up/Down changes only the Cardputer TFT viewport.

USB keyboard/terminal input remains unchanged.

## Private interface

A small private ADV interface is acceptable, for example:

```c
void adv_display_console_scroll(int delta);
void adv_display_console_follow_tail(void);
```

Equivalent naming/shape is acceptable.

Keep all scrollback APIs private under `platform/adv`; do not add them to
`include/minishell/api.h`.

## Memory / concurrency

- fixed static storage only;
- no task;
- no dynamic memory;
- no filesystem persistence;
- no cross-app ownership;
- no history retained across reboot/deep sleep.

Console output and shell physical-key handling already execute in the foreground
resident context. Preserve existing output locking/mirroring behavior; do not
route scrollback through the public Display service.

Report exact resident static-SRAM delta. Expected order of magnitude is ~1 KiB.

## Tests

Add focused host-side ADV console/display tests.

### History and rendering

Prove:

1. startup/live output up to seven rows matches pre-T051 display behavior;
2. newline, CR, Backspace and 20-column wrapping match current semantics;
3. exactly 50 physical rows are retained;
4. creation of row 51 evicts the oldest only;
5. live viewport shows newest seven rows;
6. scroll older/newer moves exactly five rows;
7. oldest/newest boundaries clamp;
8. no history mutation occurs while scrolling;
9. new output from a scrolled position returns to live tail before append;
10. current partial prompt/command row survives scrolling;
11. long wrapped command/app output is reviewable.

### Display handoff

Prove:

- enter full-screen Display mode;
- mutate/clear/present full-screen content;
- console history remains intact;
- next `adv_display_console_write()` restores history then appends output;
- full-screen colors/separator do not contaminate console rows;
- Mini-CW/MiniFT8 display behavior remains unchanged.

### Shell input

Prove the console line reader consumes:

```text
MINI_KEY_UP   + MINI_MOD_FN
MINI_KEY_DOWN + MINI_MOD_FN
```

as scroll commands and does not modify the line buffer.

Prove ordinary characters, Enter, Backspace/Delete and USB stdin still follow
existing behavior.

If direct `adv_console.c` host compilation is impractical because of ESP-IDF
dependencies, factor only the small event-decision logic into a private pure
helper and test that helper. Do not weaken the behavioral requirement.

### Regression

Run:

- full Linux CTest;
- portable unit suite;
- focused ADV console/display/input/architecture tests;
- real ADV firmware build;
- `git diff --check`.

No Linux behavior change is expected.

## Documentation

Update `docs/api/console-api.md`:

- replace the old deferred “50 lines” note;
- document implemented ADV 50-row resident scrollback;
- document Fn+Up/Fn+Down behavior;
- state that history is private resident-console behavior and not part of the
  application Console API.

Update ADV user documentation if there is an existing shell/keyboard section.

## Hardware acceptance

On Cardputer ADV:

1. run `status`; verify newest seven rows display normally;
2. generate >7 rows, preferably `apps` or another multi-line command;
3. press Fn+Up repeatedly and inspect older lines in 5-row jumps;
4. press Fn+Down and return toward the prompt in 5-row jumps;
5. verify oldest/newest boundaries do not wrap;
6. while scrolled back, type one normal character; display returns to the live
   prompt and the character appears there;
7. launch/exit a full-screen app (MiniFT8 or Mini-CW); previous shell history is
   still reviewable afterward;
8. verify USB serial output is not duplicated by scrolling;
9. verify normal shell typing, Backspace, Enter and app launching remain normal.

Return exact implementation SHA, changed-file list, tests and resource deltas.

## Architect revision — 2026-09-21

Supersedes the original 100-row / one-row-step draft:

- retain **50 physical 20-column console rows**;
- Fn+Up moves **5 rows older** per press;
- Fn+Down moves **5 rows newer** per press;
- clamp at oldest/newest boundaries;
- live-tail behavior and all other T051 semantics remain unchanged.

The physical ADV console viewport is seven rows, so a five-row step leaves two
rows visible in common between adjacent views.


## Implementation handoff

### Summary / files changed

Implemented from task branch head `766894b8e8582f77668b609d1daecc17202bb297`
(production baseline `6cb48d5667275f174635c32ac7453112d98ad8f6`).
The architect revision controls: **50 physical rows, five-row steps**. Stale
one-row/100-row wording above has been aligned with that revision.

- `platform/adv/adv_display.cpp`: independent fixed 50x20 character ring,
  retained partial row/cursor, clamped viewport offset, live-tail projection,
  and restoration after application Display ownership. Existing console
  parsing and application Display rendering remain intact.
- `platform/adv/adv_internal.h`: private scroll entry point only.
- `platform/adv/adv_console.c`: consume Fn+Up/Down in the resident physical-key
  handler and scroll +5/-5 without editing or echoing the command.
- `tests/adv_console_scrollback_test.py` and `CMakeLists.txt`: registered host
  regression executes the actual renderer and actual console write/editor/
  event/polling-loop source with hardware and USB transport stubs. Checks
  startup alignment, parsing, immediate column-20 wrap, capacity/eviction,
  tail/partial-row retention, five-row steps/clamps, immutable history while
  scrolling, output returning live, long wrapped output, full-screen handoff,
  colors/separator reset, no USB replay, physical editing, and USB stdin.
- `tests/adv_display_color_test.py`: removed the stale blanket ban on the word
  `history` from ADV Display source; T051 now explicitly owns console history
  there. Application-specific vocabulary checks and all rendering assertions
  remain unchanged.
- `docs/api/console-api.md`, `platform/adv/README.md`: documented implemented
  private console history, controls, output routing and full-screen handoff.
- This task packet: review status and measured evidence.

### Preserved behavior / boundaries

No public API, application, Linux backend, keyboard mapping, Audio, USB Host,
filesystem or task changes. No new allocation, task or persistence. History
contains only resident console output, never app Display pixels or diagnostic
System output. Existing console USB/debug routing and locking remain unchanged;
scrolling only redraws the TFT. Fn alone, characters, Enter, Backspace/Delete
and USB stdin retain their existing behavior. App Display operations cannot
modify the separate history ring. No deviation from the architect revision.

### Validation commands / results

```sh
python3 tests/adv_console_scrollback_test.py
python3 tests/adv_display_color_test.py
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
cmake -S tests/unit -B /tmp/T051-unit
cmake --build /tmp/T051-unit -j8
ctest --test-dir /tmp/T051-unit --output-on-failure
ctest --test-dir build-linux -R 'adv_.*(console|display|input|keyboard)|architecture|boundary|ft8_ui|minicw' --output-on-failure
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
xtensa-esp32s3-elf-size -A platform/adv/build/minishell_adv.elf
git diff --check
```

- Both direct host regressions passed.
- Full Linux configure/build and CTest: **89/89 passed**, including the new
  scrollback test, Linux input, USB ownership, MiniFT8 and Mini-CW regressions.
- Portable units: **28/28 passed**.
- Focused console/display/input logic and architecture/boundary/application UI
  regressions: **21/21 passed**.
- Real ADV baseline and implementation firmware builds: passed, ESP-IDF v5.5.4.
  Existing dependency pedantic warnings remain; no build failure.
- `git diff --check`: passed.

### Resident resource evidence

Measured sequential real ADV builds, with the baseline build finished before
production edits (bytes):

| Resource | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Firmware BIN | 1,382,096 | 1,382,288 | +192 |
| `.flash.text` | 1,052,778 | 1,052,982 | +204 |
| `.flash.rodata` | 236,788 | 236,788 | 0 |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,016 | 27,016 | 0 |
| `.dram0.bss` | 40,336 | 41,344 | +1,008 |
| Sum of measured static internal SRAM sections | 131,311 | 132,319 | +1,008 |

The fixed ring is 1,000 character bytes; net bookkeeping adds eight bytes.
No dynamic memory or new worker/stack is introduced.

### Remaining validation / risks / commit reference

Supervisor review and the hardware acceptance checklist above remain pending.
No flashing, hardware testing or PR performed. Host tests stub the hardware
transport; physical key delivery and TFT/USB operation still require the
architect's acceptance. No known software limitation beyond the specified
bounded, volatile history.

Evidence is included in the single implementation commit on
`codex/T051-adv-console-scrollback`; the exact SHA is returned after push.


## Supervisor review

Reviewed implementation commit:

```text
ec14ff28be933cbeb920b0b36f1195b6f73fd5c4
```

No software blocker found.

The ADV renderer now keeps console history in a separate fixed 50x20 character
ring. The live framebuffer remains independent, so full-screen Display use cannot
overwrite retained shell rows. The next resident-console write resets scroll
offset to zero, reconstructs the newest viewport from history, and then renders
with default console attributes/separator state.

Ring behavior is correct at capacity: row 51 advances only the oldest index and
reuses that slot as the new tail. The current partial row is retained. Existing
20-column immediate wrap, CR/newline and in-row Backspace semantics are preserved.

Fn+Up/Down is consumed only by the resident ADV shell line reader when the
normalized special event also carries MINI_MOD_FN. Each press changes the viewport
by five physical rows, clamps at both boundaries, emits no echo/USB replay, and
does not modify the command edit buffer. Any ordinary shell echo/output returns
to the live tail.

The measured +1,008-byte static-SRAM delta matches the 1,000-byte history plus
small fixed bookkeeping. No heap, task, public API, Linux, application or
System-diagnostic routing changes were introduced.

T051 is ready for Cardputer ADV hardware acceptance.


## Final hardware acceptance — 2026-09-21

Cardputer ADV validation passed.

Accepted behavior:

- resident M$> console retains 50 physical 20-column rows;
- Fn+Up moves five rows older;
- Fn+Down moves five rows newer;
- viewport clamps at both ends;
- command editing remains intact while reviewing history;
- new shell output returns to the live tail;
- shell history survives full-screen application Display ownership.

Implementation:

```text
ec14ff28be933cbeb920b0b36f1195b6f73fd5c4
```

T051 is COMPLETE and this hardware-accepted state is the new MiniShell baseline.
