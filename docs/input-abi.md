# MiniShell Input ABI

Status: **implemented and exercised on the Linux reference backend.**

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

CHAR events carry a Unicode scalar value and no special-key value. SPECIAL events carry a stable MiniShell key value and zero codepoint.

Modifiers currently include:

```text
SHIFT
CTRL
ALT
```

Initial special keys include arrows, Enter, Backspace, Delete, Escape, Tab, Home, End, Page Up/Down, and Insert.

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

```text
shell
  -> clean handoff
  -> app
  -> clean handoff
  -> shell
```

Applications do not acquire/release physical keyboards or terminal devices themselves.

## Linux mapping

The Linux backend:

- waits on terminal input with `poll/read`;
- converts printable bytes/control chords into CHAR events;
- converts tested ANSI cursor/navigation sequences into SPECIAL events;
- leaves termios and terminal byte parsing below MiniShell.

Portable apps such as `nano` therefore respond to `MINI_KEY_LEFT`, `MINI_MOD_CTRL`, etc., not escape bytes.

## Known internal robustness debt

The current Linux escape parser processes one read buffer at a time. A CSI sequence split across two separate `read()` calls can be misinterpreted. This is a Linux backend implementation issue, not an ABI flaw. It is tracked in `docs/consistency-check.md` and should become a small stateful parser before terminal input becomes more demanding.

## Compatibility

Input tables/events use `struct_size`, fixed-width values, stable key meanings, capability bits, and append-only growth rules. Unknown future modifiers/key values should be ignored rather than reinterpreted.

## Current verification

Linux tests cover:

- service/capability discovery;
- printable character delivery;
- special/navigation key delivery;
- Ctrl modifier behavior;
- nonblocking/timeout behavior;
- queue ordering/foreground handoff through a real PTY;
- nano interaction through the public Input ABI.

## Deferred input families

Keep separate from logical key input until required:

```text
raw scan codes / key up/down
pointer/mouse
raw touch contacts
gestures
joysticks/encoders
clipboard/IME
multiple input streams/device enumeration
```

Do not force these into `mini_key_event_t`; add independent capability families when justified.
