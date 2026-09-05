# MiniShell Application ABI

Status: **initial design contract, not frozen**

This document defines the intended boundary between MiniShell and a normal native
application. Task 0 must validate the actual ELF loader and toolchain behavior
before ABI v1 is declared stable.

## 1. Goals

The ABI should:

- let an app be built separately from MiniShell
- let an app be loaded and executed without rebooting
- let MiniShell remain resident while the app runs
- expose useful runtime services without exposing ESP-IDF
- keep application source portable across MiniShell platforms where practical
- allow MiniShell and apps to optimize independently
- remain small enough to understand and version

The ABI is not intended to provide process isolation or security boundaries.

## 2. Initial Native App Format

V1 native applications use ELF.

ELF provides the loader with code, data, symbol, relocation, and entry-point
information needed for runtime loading. ELF is a loading/container choice; the
MiniShell service contract should not depend on ELF-specific concepts unless the
loader requires them.

Each CPU architecture requires a compatible binary. For example, an ESP32-P4
RISC-V build and a future ESP32-S3 Xtensa build are separate ELF files even when
they are built from the same application source.

## 3. Entry Point

Initial conceptual entry point:

```c
int mini_main(const mini_api_t *api, int argc, char **argv);
```

Meaning:

- `api` points to the MiniShell runtime API table
- `argc` and `argv` contain shell command arguments
- the return value is the application's exit status

Example shell command:

```text
$> minift8 --band 20m
```

Conceptually produces:

```text
argc = 3
argv[0] = "minift8"
argv[1] = "--band"
argv[2] = "20m"
```

The exact exported symbol name and calling convention will be confirmed during
Task 0.

## 4. Versioned API Table

The preferred starting model is a versioned function table owned by MiniShell.

Conceptual form:

```c
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;

    const mini_system_api_t *system;
    const mini_fs_api_t     *fs;
    const mini_time_api_t   *time;
    const mini_display_api_t *display;
    const mini_input_api_t  *input;
    const mini_audio_api_t  *audio;
} mini_api_t;
```

This is illustrative, not frozen source code.

A table makes runtime dependency direction explicit:

```text
app.elf
   |
   | function-table calls
   v
MiniShell resident services
```

It also avoids making normal applications resolve ESP-IDF or hardware-driver
symbols directly.

## 5. ABI Versioning

Every app must be able to determine whether the resident MiniShell provides a
compatible ABI.

Initial requirements:

- MiniShell exposes an ABI version
- API structures expose their size where useful
- additions should prefer backward-compatible extension
- incompatible changes require a new major ABI version
- an app that cannot run safely must fail before entering its main logic

Do not freeze ABI v1 until the ELF proof-of-concept has demonstrated the actual
calling mechanism on Tab5.

## 6. MiniShell-Owned Types

Public headers define MiniShell types.

Examples:

```c
typedef int32_t mini_result_t;
typedef uint32_t mini_file_t;
typedef uint32_t mini_time_ms_t;
```

Public API signatures must not require normal apps to include ESP-IDF types such
as `esp_err_t`, FreeRTOS task handles, ESP-IDF USB structures, or M5Stack driver
objects.

Platform-specific extension APIs may intentionally expose platform concepts, but
those are outside the portable ABI.

## 7. Initial Service Priorities

Do not attempt to define a complete OS API before real apps need it.

Task 0 needs only enough API to prove runtime binding. A minimal system service
could be sufficient, for example:

```c
api->system->write("Hello from app\n");
```

or an equivalent console/display function.

The likely growth order is:

1. system / console
2. filesystem
3. memory/status
4. time/RTC
5. display
6. input
7. audio
8. USB
9. networking
10. power/platform extensions

The order may change based on the first real application.

## 8. Filesystem Contract

Normal applications see MiniShell paths such as:

```text
/flash/...
/sd/...
```

They use MiniShell filesystem services.

They do not normally:

- initialize SD hardware
- mount FATFS
- initialize the SPI/SDMMC bus
- unmount system storage
- take ownership of the underlying driver

The purpose is to make storage a validated resident system service rather than
application bring-up work.

## 9. Hardware Access Levels

### Portable

Uses only standard MiniShell ABI calls.

### Platform-aware

Uses standard ABI plus documented optional platform extensions.

### Bare hardware

Directly uses MCU registers, SDK calls, or peripheral drivers.

MiniShell does not prevent bare-hardware access. If such an app changes hardware
state expected by MiniShell, it must restore that state before returning or
accept that the system may fail.

## 10. Foreground App Lifecycle

V1 supports one foreground native app at a time.

Normal lifecycle:

```text
resolve
  -> validate
  -> load
  -> bind/pass API
  -> enter mini_main()
  -> run
  -> return exit code
  -> MiniShell cleanup
  -> unload
  -> shell prompt
```

An application should prefer returning from `mini_main()` rather than resetting
or powering down the MCU.

A future explicit `request_exit()` mechanism may be useful for deep call stacks,
but Task 0 should begin with normal function return if practical.

## 11. Resource Ownership During an App

MiniShell remains the owner of system services while an app is active.

An app may acquire logical resources such as:

- open files
- display foreground ownership
- audio stream/session
- input subscription
- timers
- network handles

MiniShell should eventually track resources created through its own APIs so they
can be released during normal app teardown.

This bookkeeping is cooperative cleanup, not protection from arbitrary memory
corruption.

## 12. Foreground Display/Input Model

A foreground application may temporarily control the user-facing display and
input through MiniShell services, but MiniShell remains the hardware owner.

Conceptually:

```text
shell owns foreground
    |
launch app
    v
app owns foreground session through API
    |
app exits
    v
MiniShell restores shell foreground
```

This allows a full-screen application such as MiniFT8 without requiring the app
to initialize the display or keyboard hardware.

## 13. Direct Access Escape Hatch

MiniShell intentionally does not forbid direct hardware access.

A developer may choose it when:

- the standard API cannot express a timing-critical operation
- a specialized peripheral is not yet represented
- experimentation requires direct register access
- performance justifies bypassing a service layer

That application becomes platform-specific and assumes responsibility for
preserving or restoring system state.

The existence of the escape hatch must not be used as an excuse to let ordinary
apps casually bypass the service architecture.

## 14. Non-Goals for ABI V1

ABI V1 does not need:

- POSIX compatibility
- `fork()` / `exec()` semantics
- processes
- users or permissions
- virtual memory
- memory protection
- arbitrary shared-library compatibility
- background applications
- full FreeRTOS API exposure
- full ESP-IDF API exposure
- binary compatibility across CPU architectures

## 15. Task 0 ABI Test

The first ABI test is intentionally tiny.

`hello.elf` must:

1. be built separately from MiniShell
2. be stored on microSD
3. be launched by typing `hello`
4. receive or bind the MiniShell API
5. call at least one resident MiniShell service
6. return an exit code
7. be unloaded
8. leave MiniShell able to accept another command without reboot

Only after this works should we expand the ABI surface.
