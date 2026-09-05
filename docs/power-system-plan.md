# Task 3 - MiniShell Power/System — COMPLETE

## Goal

MiniShell owns device power state because battery charging, low-power state, and
shutdown are platform/runtime concerns rather than ordinary application utilities.

Task 3 delivers:

```text
battery state
suspend / low-power operation
poweroff / shutdown
```

USB hardware-change detection, especially USB attach/detach, is deliberately
deferred to a later milestone. It is not required for Task-3 completion.

No application-facing Power ABI is required for the resident V1 implementation.

## Architecture

Task 3 keeps policy and hardware ownership separated:

```text
shell
  |
  v
core/minishell_power
  |
  | private normalized callback port
  v
platform/minishell_platform_tab5/power_backend
  |
  +-- INA226 battery telemetry
  +-- Tab5 charger-control expander
  +-- ESP32-P4 deep sleep
  `-- Tab5 poweroff pulse
```

`core/minishell_power` contains no ESP-IDF or M5Stack types. Board-specific power
knowledge remains below the platform boundary.

## User-facing commands

### `status`

The existing resident `status` command includes normalized power fields:

```text
battery  : 73%
charging : yes
```

Each field can independently report `unavailable` if the backend cannot provide a
reliable value.

The Tab5 backend enables charging during MiniShell startup, reads pack voltage via
INA226, and reads the charger-status signal from the board's second IO expander.
The percentage is a simple voltage-derived estimate suitable for V1 status, not a
coulomb-counted state-of-charge measurement.

Real Tab5 hardware validation: **PASS**.

### `suspend`

`suspend` is resident because global device power state is owned by MiniShell.

ESP32-P4 V1 behavior is deliberately defined as:

```text
M$> suspend
suspend: entering deep sleep; wake restarts MiniShell
```

MiniShell enters deep sleep with no software wake source configured. The CPU and
normal runtime state do not resume in place. An external reset/power-cycle wake
path restarts MiniShell from boot.

This is a low-power, charger-enabled idle state, but it is not laptop-style
suspend/resume. A later Tab5 wake-source implementation can improve the wake
experience without changing the resident ownership rule.

The command is named `suspend`, not `sleep`, because Linux `sleep` conventionally
means delaying a command for a time interval.

Real Tab5 hardware validation: **PASS**.

### `poweroff`

`poweroff` requests actual board shutdown using the Tab5 power-control pulse.
If the external power situation prevents the board from fully turning off, the
backend falls back to deep sleep rather than returning to a partially shut-down
shell.

This is intentionally distinct from `suspend`.

Real Tab5 hardware validation: **PASS**.

## Tab5 V1 backend

The implementation uses these board-level resources privately:

```text
second PI4IOE5V6408 IO expander
    P7  charge enable        -> driven high at MiniShell boot
    P6  charging status      -> input
    P5  quick-charge enable  -> active-low, driven low
    P4  poweroff pulse       -> output

INA226 @ 0x41
    bus-voltage register     -> battery percentage estimate
```

Charging initialization is intentionally early in platform startup, before SD
mounting. Failure of the power backend is non-fatal: MiniShell still boots as a
diagnostic/recovery environment and `status` reports unavailable power fields.

The backend intentionally avoids the umbrella Tab5 BSP header because that header
pulls display dependencies into the noglib build. A narrow private BSP power shim
exposes only the I2C and IO-expander functions required by MiniShell.

## Resident module interface

The private resident interface normalizes only what the shell needs:

```text
get_status
    battery_percent_valid
    battery_percent
    charging_valid
    charging

suspend
poweroff
```

This is not part of `include/minishell/api.h`; normal ELF applications cannot see
or depend on these private callbacks.

## Testing

Task 3 adds a separate host unit group:

```text
resident_power_unit
```

It is intentionally not named `abi_power_unit`, because no public Power ABI has
been introduced.

The host test covers:

- unconfigured/unavailable behavior;
- normalized battery and charging status;
- clamping malformed backend percentages to 100%;
- suspend callback routing;
- poweroff callback routing;
- reset of the resident port configuration.

Real-hardware validation has proven the three user-visible power paths:

```text
status battery/charging telemetry    PASS
suspend deep-sleep path              PASS
restart after suspend                PASS
poweroff                             PASS
```

## Future Power ABI

Do not overload `system.write()` or the Time/Location ABI with power functions.
When an application genuinely needs battery or power-control access, add a
separate append-only Power ABI with its own contract and tests.

Likely concepts include:

```text
battery percentage, when known
charging state, when known
possibly external-power state, when reliably known
request suspend
request poweroff
```

Task 3 does not create that ABI speculatively.

## Deferred hardware-change detection

Hardware-change detection remains later work, with USB attach/detach as the first
important case.

The intended ownership model remains:

```text
USB hardware / host controller
        |
        v
resident USB manager
        |
        +--> maintain canonical attached-device state
        +--> diagnostics / status
        `--> later app-facing query/notification API if required
```

Do not introduce a generic event ABI merely to anticipate hotplug. First build a
resident USB manager and let a real application requirement determine whether
applications need queries, generation counters, an event queue, callbacks, or
another notification model.

## Completion

Task 3 is complete.

```text
resident minishell_power core       COMPLETE
resident_power_unit                 IMPLEMENTED
Tab5 charger enable                 PASS
battery/charging status             PASS
suspend deep-sleep path             PASS
poweroff pulse + fallback           PASS
USB hardware-change detection       DEFERRED TO LATER MILESTONE
```
