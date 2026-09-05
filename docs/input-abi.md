# MiniShell Input ABI v0

Status: **Task 1 design contract; provisional until implementation, unit tests, ELF integration, and hardware validation pass**

## 1. Purpose

The Input ABI gives applications normalized logical input events without exposing
USB terminal escape sequences, HID scan codes, GPIO details, keyboard matrices,
touch-controller specifics, or platform-driver types.

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

The initial V0 capability is logical key input. It is application-oriented and
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

The table is append-only after ABI stabilization. Unknown future capability bits
are ignored by older applications.

For the V0 key capability:

```text
MINI_INPUT_CAP_KEY set   <=> key != NULL
MINI_INPUT_CAP_KEY clear <=> key == NULL
```

## 3. Logical key events

V0 key input normalizes different physical sources into the same logical model.

Examples:

```text
USB terminal "A"        -> CHAR('A')
physical keyboard A     -> CHAR('A')
touch keyboard A        -> CHAR('A')

terminal arrow sequence -> SPECIAL(UP)
physical Up key         -> SPECIAL(UP)
```

Applications do not parse terminal control sequences or physical scan codes.

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

- `codepoint` is a Unicode scalar value from V0 onward;
- `key` is zero;
- `modifiers` contains any known Shift/Ctrl/Alt state;
- V0 guarantees that the platform can represent the 7-bit ASCII repertoire;
- a platform may deliver additional Unicode scalar values without changing the
  V0 event structure.

A Unicode scalar is in the range `U+0000..U+10FFFF` excluding surrogate values
`U+D800..U+DFFF`.

This distinction is important: **ASCII is the minimum guaranteed input
repertoire, not the maximum legal `codepoint` value.**

For `MINI_KEY_EVENT_SPECIAL`:

- `codepoint` is zero;
- `key` contains a stable MiniShell special-key value;
- `modifiers` contains any known modifiers.

Examples:

```text
'A'      -> CHAR, codepoint='A', key=0
Up       -> SPECIAL, codepoint=0, key=MINI_KEY_UP
Ctrl-S   -> CHAR, codepoint='s', modifiers=MINI_MOD_CTRL
```

## 5. Unknown future values

After stabilization, existing event types, modifier bits, and special-key numeric
meanings are never reused.

Older applications:

- ignore unknown modifier bits;
- must not crash or reinterpret unknown special-key values;
- may ignore an event type/value they do not understand.

This permits later key/function-key expansion without changing the V0 event
layout.

## 6. Initial special keys

The V0 logical key set should include at least:

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

Exact numeric assignments are made in the public header during implementation
and must remain stable after ABI stabilization.

Additional values may be appended later.

## 7. Key sub-API

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

## 8. `read()` timeout semantics

```c
mini_result_t read(
    mini_key_event_t *out_event,
    uint32_t timeout_ms);
```

Semantics:

- `out_event` is non-NULL;
- caller zero-initializes the known structure and sets `struct_size`;
- too-small event structure returns `MINI_ERR_INVALID`;
- `MINI_WAIT_NONE` performs a non-blocking poll;
- finite values wait up to that many milliseconds;
- `MINI_WAIT_FOREVER` blocks until an event is available;
- an empty non-blocking poll returns `MINI_ERR_NOT_READY`;
- an expired finite wait returns `MINI_ERR_TIMEOUT`;
- success returns `MINI_OK` and one normalized event;
- one call returns at most one event;
- events are delivered in logical queue order.

The timeout is measured using MiniShell's internal monotonic timing source. The
application does not need to call the Time/Location ABI to use Input timeouts.

## 9. Internal buffering

The implementation may buffer normalized input events internally:

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

Queue size, storage, ISR/task ownership, and backend details are private.

If a source produces multiple logical characters, the app receives them as
separate ordered events.

## 10. Foreground handoff and stale events

MiniShell V0 has one foreground application at a time.

```text
shell owns foreground input
        |
        v
launch app
        |
        v
app receives key events
        |
        v
app exits
        |
        v
MiniShell discards app-stale queued events
        |
        v
shell receives foreground input again
```

A newly launched app must not receive leftover logical key events queued for a
previous app instance.

Likewise, keystrokes logically consumed as part of exiting an app must not appear
unexpectedly at the shell prompt.

The exact internal queue-flush/routing mechanism is private; the observable rule
is that foreground ownership changes produce a clean input boundary.

No `input_acquire()` or `input_release()` calls are needed in V0. MiniShell owns
the physical input devices and controls routing.

## 11. Press/release semantics

V0 represents logical key activations/generated characters, not raw physical
press/release state.

It does not guarantee:

```text
KEY_DOWN
KEY_UP
scan code
hardware repeat metadata
physical key identity
```

Terminal-style sources generally cannot provide those semantics reliably.

A future `raw_keyboard` sub-API may expose physical state without changing the V0
logical key API.

## 12. Pointer and touch remain separate

Pointer/touch information must not be forced into `mini_key_event_t`.

Future growth should look like:

```text
input
|-- key          existing logical key events
|-- pointer      future x/y/buttons/wheel
|-- touch        future contacts/pressure/etc.
|-- buttons      future button family
`-- raw_keyboard future scan/press/release
```

A touchscreen platform may expose both touch and key, for example when an
on-screen keyboard converts touch interactions into logical key events.

## 13. Deferred capabilities

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

These can be added as independent capability families while leaving the V0 key
ABI unchanged.

## 14. Execution-context rule

Input ABI calls follow the common V0 execution-context contract in
`docs/abi-foundation.md`.

Portable apps must not call `read()` from an ISR unless a future extension
explicitly documents ISR-safe behavior.

## 15. Verification requirements

### 15.1 Unit tests — primary

The Input service must have comprehensive deterministic unit tests using a fake
monotonic clock and synthetic normalized-event source/queue. They should verify
at least:

1. character-event construction and delivery;
2. special-key construction and delivery;
3. modifiers and unknown future modifier bits;
4. valid Unicode scalar delivery and rejection/normalization policy for invalid
   internal scalar values;
5. queue ordering across many events;
6. one event returned per `read()`;
7. `MINI_WAIT_NONE` success and immediate `MINI_ERR_NOT_READY`;
8. finite timeout success before expiry;
9. exact finite-timeout expiry producing `MINI_ERR_TIMEOUT` without real sleeping;
10. `MINI_WAIT_FOREVER` wake-up when an event is injected;
11. timeout behavior across monotonic wrap/large values as applicable;
12. too-small event structures and `struct_size` prefix behavior;
13. capability/sub-API-pointer consistency;
14. foreground handoff flushing/discarding stale events;
15. shell-to-app and app-to-shell routing boundaries;
16. multiple source events normalized into the same logical representation;
17. repeated queue/handoff cycles without retained stale state.

The fake clock should allow tests to advance time instantly so timeout coverage
remains fast and deterministic.

### 15.2 Runtime-loaded ELF integration test

`abi_input.elf` should prove the real public binary path with a representative
subset:

1. discover Input/key capability and table prefix;
2. receive one printable character and one special key;
3. exercise a modifier chord when available;
4. verify nonblocking NOT_READY;
5. verify one finite timeout;
6. verify one queued ordering sequence;
7. return and confirm clean foreground handoff to the shell;
8. repeat launch/exit without stale events.

The ELF test does not need to reproduce the full timeout/state matrix from unit
tests.

### 15.3 Hardware/platform validation

On Tab5, validate real input sources independently: USB terminal normalization
first, then touchscreen/on-screen-keyboard routing when that frontend is added.
Physical source differences must still produce the same logical key events.

The ABI remains provisional until the unit suite, focused ELF integration test,
and required hardware validation all pass.
