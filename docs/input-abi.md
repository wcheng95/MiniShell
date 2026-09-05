# MiniShell Input ABI v0

Status: **Task 1 design contract; provisional until implementation and hardware ABI tests pass**

## 1. Purpose

The Input ABI gives applications normalized logical input events without exposing
USB terminal escape sequences, HID scan codes, GPIO details, keyboard matrices,
touch-controller specifics, or platform driver types.

```text
physical input
    |
    +-- USB serial terminal
    +-- physical keyboard
    +-- touchscreen keyboard
    +-- buttons mapped as logical keys
    |
    v
MiniShell input service
    |
    | normalized logical events
    v
application
```

The initial v0 capability is logical key input. It is application-oriented and
must not be confused with a raw physical keyboard interface.

## 2. Top-level Input API

```c
#define MINI_INPUT_CAP_KEY  (1ull << 0)

typedef struct {
    uint32_t struct_size;
    uint64_t capabilities;

    const mini_key_input_api_t *key;

    /* future append-only fields may include:
       const mini_pointer_input_api_t *pointer;
       const mini_touch_input_api_t   *touch;
       const mini_button_input_api_t  *buttons;
       const mini_raw_keyboard_api_t  *raw_keyboard;
    */
} mini_input_api_t;
```

The top-level table is append-only after ABI stabilization. Unknown capability
bits are ignored by older applications.

## 3. Logical key events

V0 key input normalizes different physical sources into the same logical event
model.

Examples:

```text
USB terminal "A"       -> CHAR('A')
physical keyboard A    -> CHAR('A')
touch keyboard A       -> CHAR('A')

terminal arrow sequence -> SPECIAL(UP)
physical Up key         -> SPECIAL(UP)
```

Applications do not parse terminal control sequences or physical key scan codes.

## 4. Event types and modifiers

```c
#define MINI_KEY_EVENT_CHAR     1u
#define MINI_KEY_EVENT_SPECIAL  2u

#define MINI_MOD_SHIFT  (1u << 0)
#define MINI_MOD_CTRL   (1u << 1)
#define MINI_MOD_ALT    (1u << 2)
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

For `MINI_KEY_EVENT_CHAR`:

- `codepoint` contains the logical character value;
- `key` is zero;
- `modifiers` contains any known Shift/Ctrl/Alt state;
- v0 guarantees 7-bit ASCII characters;
- the 32-bit `codepoint` field leaves room for a future Unicode-capable extension.

For `MINI_KEY_EVENT_SPECIAL`:

- `codepoint` is zero;
- `key` contains a stable MiniShell special-key code;
- `modifiers` contains any known modifiers.

Example logical representations:

```text
'A'      -> CHAR, codepoint='A', key=0
Up       -> SPECIAL, codepoint=0, key=MINI_KEY_UP
Ctrl-S   -> CHAR, codepoint='s', modifiers=MINI_MOD_CTRL
```

Exact numeric assignments for special keys are made in the public header during
implementation and must not be reused after ABI stabilization.

## 5. Initial special keys

The v0 logical key set should include at least:

```text
UP
DOWN
LEFT
RIGHT

ENTER
BACKSPACE
DELETE
ESCAPE
TAB

HOME
END
PAGE_UP
PAGE_DOWN
INSERT
```

Additional special keys may be added later without changing `mini_key_event_t`.

## 6. Key sub-API

```c
#define MINI_WAIT_NONE     0u
#define MINI_WAIT_FOREVER  0xFFFFFFFFu

typedef struct {
    uint32_t struct_size;

    mini_result_t (*read)(
        mini_key_event_t *out_event,
        uint32_t timeout_ms);
} mini_key_input_api_t;
```

The single `read()` primitive deliberately supports polling, finite waits, and
indefinite blocking.

## 7. `read()` timeout semantics

```c
mini_result_t read(mini_key_event_t *out_event, uint32_t timeout_ms);
```

Semantics:

- `out_event` must be non-NULL;
- caller zero-initializes the event and sets `struct_size` before calling;
- `MINI_WAIT_NONE` performs a non-blocking poll;
- a finite value waits up to that many milliseconds;
- `MINI_WAIT_FOREVER` blocks until an event is available;
- an immediately empty non-blocking poll returns `MINI_ERR_NOT_READY`;
- an expired finite wait returns `MINI_ERR_TIMEOUT`;
- success returns `MINI_OK` and one normalized event;
- one call returns at most one event;
- events are delivered in logical queue order.

The timeout is measured using MiniShell's internal monotonic timing source. The
application does not need to call the Time/Location ABI to use Input timeouts.

`MINI_ERR_TIMEOUT` should be added to the shared result-code set because it has a
portable meaning useful beyond Input as future timed operations are introduced.

## 8. Internal buffering

The platform implementation may buffer logical input events internally:

```text
hardware / terminal
    |
    v
platform driver
    |
    v
normalization
    |
    v
MiniShell event queue
    |
    v
key->read()
```

Queue size, storage method, ISR/task ownership, and backend implementation are
private to MiniShell.

If a terminal or keyboard produces multiple characters, applications receive
those as separate logical events in order.

## 9. Press/release semantics

V0 represents logical key activations and generated characters, not raw physical
press/release state.

It does not guarantee:

```text
KEY_DOWN
KEY_UP
scan code
hardware repeat metadata
physical key identity
```

This is necessary because terminal-style inputs generally cannot provide those
semantics reliably.

A future `raw_keyboard` sub-API may expose physical state without changing the v0
logical key API.

## 10. Pointer and touch remain separate

Pointer and touch information must not be forced into `mini_key_event_t`.

Future growth should look like:

```text
input
|-- key          existing logical key events
|-- pointer      future x/y/buttons/wheel events
|-- touch        future contacts/pressure/etc.
|-- buttons      future logical/physical button family
`-- raw_keyboard future scan/press/release interface
```

A touchscreen platform may legitimately expose both `touch` and `key`, for
example when an on-screen keyboard converts touch interactions into logical key
events.

## 11. Foreground routing

MiniShell v0 has one foreground application at a time. The foreground app
receives the primary logical input stream while it runs.

```text
shell owns foreground input
        |
        v
launch app
        |
        v
app receives Input ABI events
        |
        v
app exits
        |
        v
shell receives foreground input again
```

No `input_acquire()` or `input_release()` calls are required in v0. MiniShell owns
the physical input devices and controls routing.

## 12. Deferred capabilities

V0 deliberately excludes:

```text
raw HID
scan codes
key-release events
key-repeat metadata
mouse/pointer events
touch contacts
gestures
joysticks
encoders
clipboard
IME
multiple input streams
input-device enumeration
```

These can be added as independent capability families while leaving the v0 key
ABI unchanged.

## 13. ABI test requirements

`abi_input.elf` should validate at least:

1. receive a printable ASCII character;
2. receive each core special-key family;
3. receive a modifier chord such as Ctrl-S when the platform can generate it;
4. verify `MINI_WAIT_NONE` returns immediately with `MINI_ERR_NOT_READY` when no
   event is queued;
5. verify a finite timeout returns `MINI_ERR_TIMEOUT` after the requested wait;
6. verify `MINI_WAIT_FOREVER` blocks until an event arrives;
7. verify queued events arrive in order;
8. repeat load/run/exit cycles without stale events leaking unexpectedly between
   app instances;
9. verify the shell regains foreground input after the test app returns.

The ABI remains provisional until these behaviors are validated through the real
runtime ABI on the Tab5 reference platform.
