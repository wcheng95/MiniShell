# MiniShell Input API

Status: **implemented and exercised on the Linux reference backend; ADV hardware mapping is being added in A2.**

## Purpose

Input gives applications normalized logical events without exposing terminal escape sequences, HID scan codes, keyboard matrices, GPIO details, touch-controller objects, or platform drivers.

```text
physical/host input
    |
private platform normalization
    |
MiniShell Input service
    |
logical events
    |
application
```

The current capability is logical key input.

## Top-level capability

```c
#define MINI_INPUT_CAP_KEY (1ull << 0)
```

When set, `api->input->key` provides the key sub-API.

## Key events

```c
#define MINI_KEY_EVENT_CHAR     1u
#define MINI_KEY_EVENT_SPECIAL  2u
```

Event structure:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t type;
    uint32_t codepoint;
    uint32_t key;
    uint32_t modifiers;
} mini_key_event_t;
```

CHAR events carry a Unicode scalar value and no special-key value. SPECIAL events carry a MiniShell key value and zero codepoint.

Modifiers include SHIFT, CTRL, ALT, FN, and OPT. The modifier mask describes the modifier state associated with an event/chord.

Special keys include arrows, Enter, Backspace, Delete, Escape, Tab, Home, End, Page Up/Down, Insert, and standalone SHIFT/CTRL/ALT/FN/OPT key presses. The standalone modifier key values are intentional: devices such as Cardputer can use Ctrl, Fn, or Opt as actions by themselves, not only as modifiers attached to another key.

The current logical-key API is press-oriented; it does not expose raw key-release events. A future need for full key-up/key-down or scan-code streams should be a separate capability rather than leaking hardware state into this API.

ASCII is the minimum guaranteed character repertoire; the event format can carry additional Unicode scalar values.

## Key read API

```c
mini_result_t read(mini_key_event_t *out_event,
                   uint32_t timeout_ms);
```

Timeout values:

```c
MINI_WAIT_NONE
finite milliseconds
MINI_WAIT_FOREVER
```

Typical results:

```text
MINI_OK              one event returned
MINI_ERR_NOT_READY   empty nonblocking poll
MINI_ERR_TIMEOUT     finite wait expired
MINI_ERR_INVALID     malformed/too-small event structure
```

One call returns at most one logical event.

## Queue ownership

The portable Input service owns the logical event queue. Platform code may produce/normalize events, but applications read only from the MiniShell queue.

```text
terminal/keyboard/touch source
    -> platform normalization
    -> MiniShell queue
    -> key.read()
```

This gives one application-visible owner regardless of how many physical sources a future platform supports.

## Foreground handoff

V1 has one foreground app. MiniShell flushes logical and backend input at app begin/end so stale keys do not cross shell/app ownership boundaries.

Applications do not acquire/release physical keyboards or terminal devices themselves.

## Linux mapping

The Linux backend:

- waits on terminal input with `poll/read`;
- converts printable bytes/control chords into CHAR events;
- converts tested ANSI cursor/navigation sequences into SPECIAL events;
- leaves termios and terminal byte parsing below MiniShell.

A terminal cannot necessarily report a bare physical modifier press, so standalone modifier SPECIAL events are backend-dependent physical capabilities even though their logical representation is portable.

Portable apps such as `nano` therefore respond to `MINI_KEY_LEFT`, `MINI_MOD_CTRL`, etc., not escape bytes.

## ADV mapping

The Cardputer ADV backend owns the TCA8418 keyboard matrix and normalizes its physical events below MiniShell. The mapping preserves the proven MiniFT8-V2 ADV row/column remap and key layout.

Current ADV conventions include:

```text
Fn + ;          Up
Fn + ,          Left
Fn + .          Down
Fn + /          Right
Fn + `          Escape
Fn + Backspace  Delete
```

Shift/Ctrl/Alt/Fn/Opt state is attached to generated events. Pressing one of those modifier keys by itself also yields the corresponding SPECIAL event.

## API evolution

`struct_size`, fixed-width values, capabilities, and explicit key meanings remain useful design mechanisms, but MiniShell does not currently promise backward compatibility. Values must still be changed deliberately because applications and tests depend on their semantics.

## Current verification

Linux tests cover:

- service/capability discovery;
- printable character delivery;
- special/navigation key delivery;
- Ctrl modifier behavior;
- nonblocking/timeout behavior;
- queue ordering/foreground handoff through a real PTY;
- nano interaction through the public Input API.

ADV A2 adds hardware verification for the TCA8418 mapping and shell/app handoff.

## Deferred input families

Keep separate from logical key input until required:

```text
raw scan codes / full key up/down
pointer/mouse
raw touch contacts
gestures
joysticks/encoders
clipboard/IME
multiple input streams/device enumeration
```

Do not force these into `mini_key_event_t`; add independent capability families when justified.
