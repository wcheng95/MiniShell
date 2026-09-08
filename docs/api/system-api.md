# MiniShell System API

Status: **implemented and exercised on the Linux reference backend.**

## Purpose

System is the smallest MiniShell application service. It provides a guaranteed diagnostic text sink without exposing UART, USB, terminal, display, logging framework, or platform-specific output types.

```text
application
    |
system.write()
    |
MiniShell System service
    |
private backend-selected diagnostic sink
```

System must remain deliberately small. It is not a miscellaneous bucket.

## Service table

```c
typedef struct {
    uint32_t struct_size;
    void (*write)(const char *text);
} mini_system_api_t;
```

`api->system` is the minimum service expected by the current runtime/application model.

## `write()`

```c
void write(const char *text);
```

Semantics:

- NUL-terminated byte string;
- no newline added automatically;
- synchronous best-effort diagnostic output;
- caller retains ownership;
- MiniShell does not retain the string after return;
- no terminal geometry, cursor, color, layout, input, or display semantics are promised.

Typical use:

```c
api->system->write("decoder starting\n");
api->system->write("fatal: bad configuration\n");
```

## System output is not Display

```text
system.write()          diagnostic/runtime sink
display.text.write_at() application UI surface
```

A portable app must not use System as a hidden terminal API.

## Platform mapping

The actual sink is private and may differ:

```text
Linux/Mint      stdout/terminal diagnostic path
Tab5/NuttX      selected console/debug path
small MCU       UART/USB/debug transport
```

Apps do not inspect which backend is selected.

## What belongs elsewhere

```text
memory/resource info   Memory API
filesystem/storage     Filesystem API
UTC/location           Time/Location API
application UI         Display API
application input      Input API
sleep/delay            Time/Location API
power/system control   platform-specific/future dedicated contract
```

Platform name/board name should not become an application branching mechanism. Portable apps should discover service capabilities instead.

## API evolution

The System API is still under active development. There is no backward-compatibility promise yet. Any future addition must be broadly useful runtime behavior that does not fit another service.

## Current verification

Linux tests verify the service is present, diagnostic writes reach the backend, loaded apps can call it, and repeated app return leaves the shell healthy.

`hello` remains the minimal reference app for `mini_api_get()` plus `system.write()`.
