# MiniFT8-V3 UI

This document is the canonical MiniFT8-V3 UI definition. New V3 decisions override older MiniFT8-V2 behavior; where V3 has not defined something yet, MiniFT8-V2 is the default behavioral baseline.

## Decision status

### DECIDED

- First-release UI is text-only: **20 columns x 7 lines**.
- A **2-pixel gap/separator** is placed below the top line.
- First release has **no countdown bar** and **no graphical waterfall**.
- The 20-character top line is fully defined below, with the documented V -> 3 large-page exception.
- Top-level UIScreen switching uses case-insensitive reserved letters: `R`, `T`, `O`, `S`, `V`, `Q`.
- `Q` means Quit.
- Other character keys may be defined as shortcuts inside an individual UIScreen.
- Page Up/Down remains part of a UIScreen's top level; paging does not enter a submenu.
- Switching UIScreens always enters the destination UIScreen at its top level.
- Up/Down page navigation wraps around.
- Keep the Back/ESC behavior already implemented in `MiniShell/MiniFT8-V3` unchanged.
- UIScreen state and TX/RX operation are independent; TX/RX activity does not add special UIScreen or edit restrictions.
- `V -> 1 Memory` is a production read-only runtime memory/RX diagnostic page using all six content lines.

### OPEN

- The final role, content, or possible removal of the `S` UIScreen is undecided. `station.txt` can be edited from MiniShell before `ft8` starts, so some V2-style in-app settings may no longer need an `S` menu.
- Detailed contents and shortcuts for UIScreens/submenus not explicitly defined below remain to be reviewed against MiniFT8-V2.

### TODO

- Review `R` against MiniFT8-V2 and define its V3 pages/actions.
- Review `T` against MiniFT8-V2 and define its V3 pages/actions.
- Review `O` against MiniFT8-V2 and define its V3 pages/actions.
- Decide the V3 role of `S`.
- Define the remaining `V` entries after `V -> 1 Memory`.
- Define UIScreen-local shortcuts only where useful.

## Vocabulary

```text
Protocol       fixed application identity: FT8 for the `ft8` app
UIScreen       top-level UI location: R / T / O / S / V
Top level      inside a UIScreen, but not inside one of its submenus
Submenu        shallow group entered from a UIScreen
Page           visible slice of a longer list when paging is required
Station Profile
               operating-profile setting shown inside MiniFT8 where applicable
```

Page navigation does **not** count as entering a submenu. A UIScreen can therefore be on page 2/3 and still be at its top level.

Protocol selection is not MiniFT8 application state. Switching from FT8 to
another portable mode means leaving `ft8` and launching another MiniShell
application such as `minicw`, `js8chat`, `rtty`, or `sstv`. FT4 is
intentionally outside the MiniShell portable-radio scope.

## First-release screen layout

MiniFT8-V3 uses a fixed text layout:

```text
20 columns x 7 lines
```

The top line is followed by a 2-pixel visual gap/separator. The remaining six lines are UIScreen-owned content.

```text
line 0   fixed 20-character status line
         2-pixel gap/separator below
line 1   UIScreen content
line 2   UIScreen content
line 3   UIScreen content
line 4   UIScreen content
line 5   UIScreen content
line 6   UIScreen content
```

For the first release there is deliberately:

- no countdown bar;
- no graphical waterfall.

These can be reconsidered later without changing the basic UIScreen model.

## Top line

The 20-character top line is:

```text
[UIscreen:2:left][space][Band:2:zero-padded][space][UTC:HH:MM:SS][space][currentpage/totalpage:3][space][counter:1:0-E]
```

Character allocation:

```text
2 + 1 + 2 + 1 + 8 + 1 + 3 + 1 + 1 = 20
```

Example:

```text
RX 20 14:32:08 1/3 A
```

Fields:

```text
UIScreen   2 characters, left-aligned
Band       2 characters, zero-padded
UTC        HH:MM:SS
Page       current/total, 3 characters
Counter    one hexadecimal-like slot character: 0 through E
```

### V -> 3 large-page exception

The architect-approved exception for `V -> 3 QSO / Log` applies only when the
current-day QSO list has **10 or more pages** and the normal locked top line can
no longer represent the full page fraction inside 20 columns.

For 1-9 QSO pages, use the normal locked top line unchanged.

For 10-99 QSO pages, `V -> 3` removes UTC seconds but keeps UTC minutes
and the slot counter:

```text
V  20 HH:MM 10/12 A
```

This preserves the most useful timing context while allowing the complete
two-digit `current/total` page fraction to fit inside 20 columns. The rendered
line is space-padded to the fixed 20-column width.

At 100 or more QSO pages, the header stops exposing the exact page fraction
and shows a capped indicator instead:

```text
V  20 HH:MM 100+ A
```

The internal page can continue advancing past 100, but the top line remains
`100+`; it does not show values such as `101/102`. This keeps screen, band,
UTC minutes, and the slot counter visible within 20 columns.

This is a narrow `V -> 3` presentation exception. It does not change the locked
top-line contract for R, T, O, S, V top level, or other V submenus.

## Generic UIScreen behavior

### Top level

A UIScreen is at its **top level** when the user has entered that UIScreen but has not entered one of its submenus.

Page Up/Down navigation does not leave the top level.

At RX and TX top level, bare `;` selects the previous page and bare `.`
selects the next page, with wraparound, just like Up/PageUp and Down/PageDown.
No Fn is required. These character shortcuts do not apply to O/S/V or any
submenu, including V -> QSO. Desktop footers show `;/. page`; ADV keeps its
20x7 layout without a footer.

### UIScreen switching

At the top level of any UIScreen, these letter keys are reserved and case-insensitive:

```text
R / T / O / S / V    switch UIScreen
Q                     quit MiniFT8 and return to MiniShell
```

Other character keys may be assigned as shortcuts inside the current UIScreen.

Switching to another UIScreen always enters the destination UIScreen at its top level rather than restoring a previous submenu position.

### Page navigation

Use Up/Down to move between pages. Paging wraps around.

Every newly completed RX batch returns the visible RX screen to page 1 and
selects its first row, including a zero-message batch. Redrawing the same batch
generation preserves the user's page. A new RX batch while viewing TX or another
screen does not change that screen's page or selection; returning to RX uses the
existing page-1 screen-entry rule. The controller exposes factual batch generation
in `UiModel`; `ui_shell` owns the page/selection reset.

For a three-page UIScreen:

```text
Page Down: 1/3 -> 2/3 -> 3/3 -> 1/3
Page Up:   1/3 -> 3/3 -> 2/3 -> 1/3
```

### Back / ESC

Keep the Back/ESC behavior already implemented in `MiniShell/MiniFT8-V3`. No UI behavior change is required.

### Independence from TX/RX

UIScreen navigation and radio TX/RX operation are independent state dimensions.

Do not add special UIScreen-switching, paging, submenu, or edit restrictions merely because MiniFT8 is transmitting or receiving.

## Input boundary

Logical UI input is interpreted by MiniFT8 after MiniShell normalizes the physical source.

Physical keyboard, touch, buttons, BLE, or another input source must therefore map to the same logical input behavior before MiniFT8 handles it.

At the MiniFT8 level, the generic controls currently include:

```text
R / T / O / S / V    top-level UIScreen selection
Q                     quit at UIScreen top level
Up / Down             page navigation at top level; submenu-specific use where defined
Left / Right          UIScreen/submenu-specific use where defined
Enter                 activate selected item where applicable
Esc or `              Back/cancel behavior already implemented in MiniFT8-V3
other characters      UIScreen-local shortcuts where defined
```

## MiniFT8-V2 baseline rule

For each UIScreen, MiniFT8-V2 behavior and content are the starting point unless they conflict with an explicit MiniFT8-V3 decision in this document or with the MiniShell architecture.

This is a compatibility-of-behavior guideline, not a requirement to preserve V2 implementation structure.

## UIScreen notes

### R

Use MiniFT8-V2 RX behavior as the baseline. Detailed V3 content is TODO.

### T

Use MiniFT8-V2 TX behavior as the baseline. Detailed V3 content is TODO.

### O

`O -> 4 -> 1` selects the CQ type. Plain `CQ` is always first, followed by
the ordered modifiers configured in MiniFT8 `setting.txt`:

```text
cqtypes=POTA SOTA DX 250
cq_type=2
```

This list cycles `CQ`, `CQ POTA`, `CQ SOTA`, `CQ DX`, `CQ 250`; the example
selects `CQ SOTA`. Left selects the previous entry, Right and Enter select the
next, and navigation wraps. The current row renders `CQ Type: <choice>` and
the selected index is saved through the normal settings path.

The UI receives display text, index and option count; it does not interpret
modifier names or own the configured list. Beacon OFF/EVEN/ODD behavior is
unchanged. T082 is hardware accepted on ADV.

### S

**OPEN.**

MiniFT8-V2 used `S` as an in-application settings/menu screen. In V3, `station.txt` can be edited in MiniShell before launching `ft8`, so some or all of the old `S` responsibilities may belong outside the running application.

Keep `S` reserved until this is decided.

### V

`V` is read-only. Its first entry is now defined as a production feature:

```text
1 Memory >
2 GPS >
3 QSO / Log >
4 Performance >
5 System Info >
6 About >
```

The remaining entries are still subject to later V3 review.

#### V -> 1 Memory

`Memory` uses all six content lines and is intended for normal runtime observation on every MiniShell platform that provides the Memory API.

```text
Heap free: <value>
Largest: <value>
App alloc: <value>
Alloc count: <value>
Largest/free: <value>%
RX: ON|OFF
```

Values use compact byte units (`B`, `K`, `M`, `G`), with one decimal place for `K` and larger units. A field that is not available from the MiniShell Memory API is shown as `--`.

Field meanings:

- `Heap free` is the free-memory value reported by MiniShell. When MiniShell enforces an application memory limit, this is remaining application quota; otherwise it may be the backend's actual free heap.
- `Largest` is the largest contiguous free block reported by MiniShell when that measurement is available.
- `App alloc` is the total current allocation size tracked through the MiniShell Memory API for the running application. It does **not** claim to include task stack, static/global storage, backend allocations, or other memory outside the MiniShell Memory allocator.
- `Alloc count` is the number of current allocations tracked through the MiniShell Memory API for the application.
- `Largest/free` is `largest contiguous free block / free memory`, expressed as an integer percentage. It is an objective ratio and must not be labeled as a fragmentation percentage. If either source value is unavailable, or free memory is zero, display `--`.
- `RX` reports whether the MiniFT8 receive path is currently active. It is independent of which UIScreen is displayed.

MiniFT8 obtains these values only through the platform-independent MiniShell Memory API. No Linux-, ESP32-, or ADV-specific memory calls belong in the MiniFT8 application.

`app_controller_build_model()` snapshots Memory facts whenever it builds a complete `UiModel`, regardless of the visible screen. `V -> 1 Memory` displays that snapshot; its visibility does not control the query. The controller supplies facts, while `ui_shell` owns presentation, redraw decisions, and navigation.

## Rendering boundary

The UI remains behind the MiniFT8/MiniShell display boundary:

```text
MiniFT8 UI state
   -> UiModel
   -> renderer
   -> 20x7 UiFrame
   -> ft8_ui_adapter
   -> MiniShell Display API
```

Input is the reverse boundary:

```text
MiniShell input event
   -> ft8_ui_adapter
   -> UiInput
   -> MiniFT8 UI/controller
   -> AppAction
```

The application UI/controller should remain independently unit-testable without requiring a physical display or keyboard.


## Optional color status (T049)

MiniFT8 requests a white separator after the top row on every screen. It turns
red only while physical transmission is active (`app_controller_tx_active()`),
not for a queued intent or pending slot, and returns white when TX ends/fails.
The header text stays white. ADV supplies the existing y=19, height=2 region;
MiniFT8 expresses only a generic row boundary.

On RX, each complete displayed row, including its numeric selection prefix,
is red for factual `is_to_me`, otherwise green for factual `is_cq`, otherwise
white. To-me takes precedence if both flags are set. Categories follow the same
ordered message projection as text, including pagination. Retained rows keep
their colors during TX. All other screens use white text. No text, ordering,
selection, classification or RX display lifetime changes.

Color and separators are optional generic Display capabilities. Linux and other
plain providers retain the existing monochrome text; unsupported optional styling
falls back without failing MiniFT8 startup or rendering.
