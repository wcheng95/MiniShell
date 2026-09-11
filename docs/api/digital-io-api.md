# MiniShell Digital I/O API

Status: **K2 public contract**  
API generation: **v3**

## Purpose

Digital I/O provides applications with generic logical access to discrete platform lines without exposing platform GPIO drivers or application-domain meaning.

MiniShell knows only:

```text
numeric line ID
mode
logical level 0/1
open/read/write/close
```

MiniShell does not know concepts such as `dit`, `dah`, paddle, PTT, KeyOut, relay, or LED. Those meanings belong to the application.

## Public API

The service is available through:

```c
api->digital_io
```

V1 operations are:

```text
open
read
write
close
```

Supported capability/mode pairs are:

```text
MINI_DIGITAL_IO_CAP_INPUT
    MINI_DIGITAL_IO_MODE_INPUT

MINI_DIGITAL_IO_CAP_INPUT_PULLUP
    MINI_DIGITAL_IO_MODE_INPUT_PULLUP

MINI_DIGITAL_IO_CAP_OUTPUT
    MINI_DIGITAL_IO_MODE_OUTPUT

MINI_DIGITAL_IO_CAP_OUTPUT_OPEN_DRAIN
    MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN
```

No interrupt/callback API is part of V1. Applications that need edge detection initially poll using MiniShell monotonic time/sleep facilities.

## Open

An application supplies:

```c
mini_digital_io_config_t config = {
    .struct_size = sizeof(config),
    .line_id = line_id,
    .mode = mode,
    .initial_level = 1u,
};

mini_digital_io_t line = MINI_DIGITAL_IO_INVALID;
mini_result_t result = api->digital_io->open(&config, &line);
```

`line_id` is a generic numeric platform line identifier. The application may obtain that value from application-owned deployment settings.

`initial_level` must be 0 or 1. It is meaningful for output modes and lets the backend establish a safe output state as part of opening/configuring the line rather than requiring a later write. Input modes ignore its electrical meaning, but the field must still contain a valid logical level.

An application may not open the same line twice concurrently through the service. A duplicate open returns `MINI_ERR_EXISTS`.

The backend may reject a line with `MINI_ERR_ACCESS` when the line is not exposed for application use, is owned by resident MiniShell/platform hardware, or is unsuitable for the requested mode.

## Read

```c
uint32_t level;
mini_result_t result = api->digital_io->read(line, &level);
```

On success, `level` is exactly 0 or 1.

Reading is allowed for any successfully opened V1 line. For an output, this provides the backend's current observable/logical line state where supported.

## Write

```c
mini_result_t result = api->digital_io->write(line, level);
```

`level` must be 0 or 1.

Writing is valid only for:

```text
MINI_DIGITAL_IO_MODE_OUTPUT
MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN
```

Attempting to write an input returns `MINI_ERR_ACCESS`.

For open-drain mode, logical 0 means the backend drives the line low and logical 1 means the backend releases/high state according to the platform's open-drain implementation. The application remains responsible for any external pull-up required by its hardware design.

## Close and lifecycle

```c
api->digital_io->close(line);
```

Handles are opaque. Using a closed or invalid handle returns `MINI_ERR_BAD_HANDLE`.

Digital I/O handles are foreground-application resources. MiniShell automatically closes any lines still open when the application exits, including application-forgotten cleanup paths that still return through MiniShell lifecycle handling.

This cleanup rule is important for hardware safety and resource ownership, but applications should still explicitly close lines when normal control flow no longer needs them.

## Ownership

The service owns public-handle lifetime and exclusivity. The platform backend owns physical-line realization and determines which physical lines are application-accessible.

Application-domain interpretation remains above MiniShell:

```text
Keyer setting: Dit GPIO = 13
        |
        v
Keyer config_service
        |
        v
Keyer app_controller
        |
        v
KeyIn edge module
        |
        v
MiniShell Digital I/O
    line_id = 13
    input + pull-up
        |
        v
platform generic digital-line provider
```

MiniShell never learns that line 13 is a dit input.

## Linux reference provider

Linux provides a deterministic virtual Digital I/O provider for application development and service testing. It does not access host GPIO hardware.

Its V1 reference behavior is:

```text
plain input       initial/read state 0
pull-up input     initial/read state 1
output            starts at requested initial_level
open-drain output starts at requested initial_level
output writes     update subsequent reads
```

This provider exists to validate portable application logic and MiniShell semantics, not to emulate electrical details.

## Cardputer ADV provider

ADV maps numeric line IDs to ESP32-S3 GPIO numbers privately below MiniShell, but it does **not** expose arbitrary ESP32-S3 GPIOs to applications.

V1 uses an explicit allow-list of external application-facing lines:

```text
G1
G2
G3
G4
G5
G6
G13
G15
```

These are the non-shared signals available on the Cardputer ADV HY2.0-4P and EXT connectors. Shared I2C/keyboard and SD pins are not exposed through Digital I/O, and neither are built-in LCD, audio, IR, battery, or other internal lines.

This allow-list is deliberate: resident MiniShell hardware ownership must not be defeatable by an external application guessing a GPIO number.

The planned first Keyer assignments G13/G15 for inputs and G3/G6 for KeyOut all remain within the allowed set. Their Keyer meaning remains application configuration rather than MiniShell policy.

## K2 verification

The Linux integration probe verifies:

```text
all four V1 capabilities
input-pullup read
input write rejection
duplicate-open rejection
output initial level
output write/read
open-drain open/write
bad-handle behavior
automatic app-exit cleanup
```

The cleanup check deliberately leaves one line open, exits the probe, then runs the probe again. The second run must successfully reopen the same line.

ADV CI verifies that the same public service and the ESP32-S3 GPIO provider compile into the maintained Cardputer ADV firmware. Actual paddle and radio-keying electrical validation belongs to Keyer K4, where real application-owned GPIO assignments are exercised.
