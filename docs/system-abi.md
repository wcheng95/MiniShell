# MiniShell System ABI v0

Status: **Task 1 design contract; provisional until implementation, unit tests, ELF integration, and hardware validation pass**

## 1. Purpose

The System ABI is the smallest mandatory MiniShell runtime service. It provides a guaranteed application-visible diagnostic output path without exposing the platform console, UART, USB transport, display, or terminal implementation.

```text
application
    |
    | MiniShell System ABI
    v
MiniShell runtime
    |
    `-- diagnostic/system text sink
          |
          `-- platform-selected backend
              USB Serial/JTAG / UART / debug transport / other
```

The System ABI must remain deliberately small. It is not a miscellaneous bucket for functions that do not fit elsewhere.

## 2. Scope of v0

System ABI v0 contains only:

```text
write
```

The service is mandatory for a conforming MiniShell runtime:

```text
api->system != NULL
```

Other basic services may be absent on smaller platforms, but the System ABI is the minimum runtime foothold every application can rely on.

## 3. Service table

```c
typedef struct {
    uint32_t struct_size;

    void (*write)(const char *text);
} mini_system_api_t;
```

The table is append-only after ABI stabilization.

## 4. `write()`

```c
void write(const char *text);
```

Purpose:

> Provide a guaranteed minimal diagnostic text sink for an application.

Semantics:

- `text` is a NUL-terminated UTF-8 byte string;
- `text == NULL` is invalid application behavior;
- no newline is added automatically;
- the call is synchronous and best-effort;
- the caller retains ownership of the string;
- MiniShell does not retain the string after the call returns;
- no cursor position, screen geometry, color, ANSI processing, scrolling, terminal state, or input semantics are promised;
- the function does not grant ownership of a UART, USB device, display, or terminal.

Typical use:

```c
api->system->write("decoder starting\n");
api->system->write("fatal: invalid configuration\n");
```

## 5. Diagnostic output is not the Display ABI

`system.write()` is not an application user-interface surface.

```text
system.write()
    -> diagnostic/runtime text sink

display.text.write_at()
    -> application-visible text display surface
```

An application must not depend on `system.write()` for:

```text
cursor positioning
screen clearing
text layout
terminal dimensions
color
interactive UI behavior
```

Those belong to the Display and Input ABIs or to a future higher-level console/terminal abstraction.

## 6. Platform independence

The backend is platform-private.

Examples:

```text
M5Stack Tab5 / ESP32-P4
    system.write -> USB Serial/JTAG

Ox64 / BL808
    system.write -> UART or another resident debug transport

small MCU
    system.write -> whatever diagnostic sink the platform provides
```

Applications must not inspect or depend on which backend is used.

## 7. What does not belong in System ABI v0

The following remain outside System ABI v0:

```text
memory information       -> Memory ABI
filesystem information   -> Filesystem ABI
UTC/location             -> Time/Location ABI
user-interface output    -> Display ABI
user input               -> Input ABI
sleep/delay              -> Time/Location ABI
reset/reboot/power       -> future system-control or power ABI
platform/board identity  -> deferred; prefer capability discovery
```

Also deferred unless a real portable need appears:

```text
yield
request_exit
logging levels
build/version strings
environment variables
platform name
CPU name
board name
```

In particular, the System ABI should not encourage application logic such as:

```c
if (board_is_tab5) { ... }
```

Applications should instead query the capabilities of the services they need.

## 8. Extension rule

Future System ABI additions must be generally useful runtime operations that do not naturally belong to another service.

The table grows only by appending new fields:

```c
typedef struct {
    uint32_t struct_size;

    void (*write)(const char *text);    /* v0 */

    /* future fields appended here */
} mini_system_api_t;
```

Existing fields are never reordered, removed, repurposed, or given incompatible semantics after ABI stabilization.

## 9. Verification requirements

### 9.1 Unit tests — primary

The System service must have unit tests using a fake diagnostic sink. They should
verify at least:

1. exact byte/string forwarding to the backend;
2. no automatic newline insertion;
3. multiple independent calls preserve order;
4. ordinary UTF-8 byte sequences are forwarded unchanged;
5. the service table exposes the expected V0 prefix and `struct_size`;
6. backend replacement does not change public System semantics.

`text == NULL` is documented as invalid application behavior and does not require
the service to make arbitrary invalid-pointer use recoverable.

### 9.2 Runtime-loaded ELF integration test

`abi_system.elf` should remain small and validate the real binary boundary:

1. `api->system` is present;
2. `struct_size` covers the V0 `write` field;
3. representative `write()` calls reach the resident service;
4. the ELF returns normally to `M$>`;
5. repeated launch/write/exit cycles leave the shell healthy;
6. Task 0's `hello.elf` remains compatible.

### 9.3 Hardware validation

On Tab5, verify that System output reaches the configured USB Serial/JTAG
backend. Equivalent hardware validation applies to other platform backends.

The ABI remains provisional until its unit suite, focused ELF integration test,
and required hardware validation all pass.
