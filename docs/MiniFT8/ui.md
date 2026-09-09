# MiniFT8-V3 UI

This document is the canonical MiniFT8-V3 UI definition. New V3 decisions override older MiniFT8-V2 behavior; where V3 has not defined something yet, MiniFT8-V2 is the default behavioral baseline.

## Decision status

### DECIDED

- First-release UI is text-only: **20 columns x 7 lines**.
- A **2-pixel gap/separator** is placed below the top line.
- First release has **no countdown bar** and **no graphical waterfall**.
- The 20-character top line is fully defined below.
- Top-level UIScreen switching uses case-insensitive reserved letters: `R`, `T`, `O`, `S`, `V`, `Q`.
- `Q` means Quit.
- Other character keys may be defined as shortcuts inside an individual UIScreen.
- Page Up/Down remains part of a UIScreen's top level; paging does not enter a submenu.
- Switching UIScreens always enters the destination UIScreen at its top level.
- Up/Down page navigation wraps around.
- Keep the Back/ESC behavior already implemented in `MiniShell/MiniFT8-V3` unchanged.
- UIScreen state and TX/RX operation are independent; TX/RX activity does not add special UIScreen or edit restrictions.

### OPEN

- The final role, content, or possible removal of the `S` UIScreen is undecided. `station.txt` can be edited from MiniShell before `ft8` starts, so some V2-style in-app settings may no longer need an `S` menu.
- Detailed contents and shortcuts for each UIScreen remain to be reviewed against MiniFT8-V2.

### TODO

- Review `R` against MiniFT8-V2 and define its V3 pages/actions.
- Review `T` against MiniFT8-V2 and define its V3 pages/actions.
- Review `O` against MiniFT8-V2 and define its V3 pages/actions.
- Decide the V3 role of `S`.
- Review `V` against MiniFT8-V2 and define its V3 pages/actions.
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

Protocol selection is not MiniFT8 application state. Switching from FT8 to another protocol means leaving `ft8` and launching another MiniShell application such as future `ft4`, `cw`, `rtty`, or `js8`.

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

## Generic UIScreen behavior

### Top level

A UIScreen is at its **top level** when the user has entered that UIScreen but has not entered one of its submenus.

Page Up/Down navigation does not leave the top level.

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

Use MiniFT8-V2 O-screen behavior as the baseline. Detailed V3 content is TODO.

### S

**OPEN.**

MiniFT8-V2 used `S` as an in-application settings/menu screen. In V3, `station.txt` can be edited in MiniShell before launching `ft8`, so some or all of the old `S` responsibilities may belong outside the running application.

Keep `S` reserved until this is decided.

### V

Use MiniFT8-V2 V-screen behavior as the baseline. Detailed V3 content is TODO.

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
