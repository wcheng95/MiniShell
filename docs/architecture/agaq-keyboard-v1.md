# AGAQ Keyboard — V1.0 Design Specification

Status: **Design only; not approved for implementation**  
Date: 2026-10-22  
Scope: Five-button operation and optional AGAQ text entry on compact MiniShell platforms.

## Goal and boundaries

Provide a universal, keyboardless UI that preserves MiniShell's existing application conventions. Five logical buttons operate the UI; AGAQ is an optional on-screen QWERTY text-entry mode using those same buttons. No code or deployment is committed by this design.

Keep **operation** and **text editing** distinct. Avoid double clicks, triple clicks, prediction, multi-tap typing, or feature growth without demonstrated need. Follow Modular / Ownership / Decouple / KISS.

## Input model

Logical buttons: **UP, DOWN, LEFT, RIGHT, OK**.

The physical/touch input adapter reports the button identity and press/release timing. A gesture layer recognizes:

- **Click** (short press and release).
- **Long hold** (more than 1 second).
- **Release**, needed to terminate any hold-driven behavior.

Gesture meaning belongs to the active UI mode, **not** to the hardware adapter:

| Gesture | Five-button operation | AGAQ text editing |
| --- | --- | --- |
| Click | Primary function for the current context | Move selection or activate selected virtual key |
| Long hold (>1 s) | Secondary function determined by current context | Auto-repeat as appropriate for the selected button/action |
| Release | End hold state | Stop any repeating behavior |

No double/triple click semantics. Details such as click-versus-hold dispatch, repeat delay/rate, hold effects on OK, and any secondary-operation repeat are **OPEN**; do not silently impose one universal interpretation. Prevent accidental click plus secondary-action double dispatch.

## Display composition

Target touchscreen concept: application at top, virtual keyboard/idle panel in middle, five touch-button regions at bottom.

| Vertical region | Y extent | Height | Responsibility |
| --- | --- | --- | --- |
| Application | 0–134 | 135 px | MiniShell/application display |
| AGAQ / idle panel | 135–199 | 65 px | Five-row virtual keyboard only while editing; otherwise blank or optional slogan |
| Five touch controls | 200–239 | 40 px | UP, DOWN, LEFT, RIGHT, OK |

On a **240 x 240** screen, five equal touch zones are **48 x 40 px** each. On a **320 x 240** screen they are **64 x 40 px** each. Visible button art may be smaller than its hit box.

Preserve the existing ADV application font convention of **12 pixels per character**: 240 px fits 20 characters per row; 320 px fits 26 complete characters per row with 8 px remaining. The AGAQ panel may use a smaller font, nominally **8 x 13 pixels**. Five rows fit in 65 px; 240 px width fits 30 unspaced glyph columns, or 15 with one 8-pixel cell of spacing. Thus up to 150 or 75 character slots respectively; **these are display capacities, not a frozen key count/layout**. At 320 px the corresponding capacities are 40 or 20 columns per row.

When AGAQ is inactive, the middle panel may be blank or display an idle slogan such as **“Life is Good. Keep It Simple, Smart!”**; do not repurpose it by default for extra controls. The five operation buttons remain accessible.

The above geometry describes the square/touchscreen concept, **not** a mandate to extend the ADV's native 240 x 135 display or change its existing UI.

## AGAQ editing behavior

- Familiar QWERTY-style virtual keys arranged in approximately five rows.
- Multiple **Shift** keys distributed across the grid for convenient access.
- Include numbers, common punctuation, Space, Backspace, and Enter; exact positions remain open.
- Five logical buttons move a visible selection and activate the selected virtual key.
- Long holds in AGAQ enable repeat; behavior must terminate on release.
- Standard application text editing is the goal; AGAQ itself is not a touchscreen typing keyboard. On touch platforms, the **bottom five large touch zones** emulate buttons rather than requiring a tap on tiny character cells.
- Final key map, shift state (one-shot vs locked), selection wrap rules, cursor editing, accept/cancel behavior, and text-result interface remain **OPEN**.

## Ownership and portability

```text
Application / MiniShell
    |
Five-button operation OR AGAQ text-entry mode
    |
Logical button and display interfaces
    |
Platform adapter (touch/GPIO/keyboard; display driver)
```

- The platform adapter owns touch coordinate mapping and display hardware access, not menu rules or QWERTY semantics.
- The UI layer owns operation-mode behavior; the AGAQ layer owns text-entry layout and behavior.
- No separate implementations of AGAQ per board.
- Platform candidates discussed: Cardputer ADV, M5CoreS3, LILYGO T-Watch variant (exact model TBD), and Linux/Raspberry Pi. These are **possibilities**, not approved/implemented backends.
- Reuse existing MiniShell Input/Display APIs where practical; do not preemptively freeze a new backend ABI.

## Open decisions / non-goals

1. Exact five-row QWERTY grid, duplicate Shift placement, key set and spacing.
2. Shift one-shot/lock behavior.
3. Repeat delay, interval, acceleration if any, and key-specific hold interpretation.
4. Selection wrapping and vertical navigation between unequal rows.
5. Editing contract: insert/delete/cursor, confirm/cancel, buffer ownership.
6. Exact board model, orientations, font metrics, touch calibration, and integration schedule.

**No implementation task, platform port, code modification, or schedule is authorized by this document.**
