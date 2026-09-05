# Task 3 - MiniShell Power/System — ACTIVE

## Goal

MiniShell should own device power state and hardware-change detection because
these are platform/runtime concerns rather than ordinary application utilities.

Task 3 begins after completion of Task 2 resident file transfer.

The initial desired capabilities are:

```text
battery state
suspend / low-power operation
poweroff / shutdown
future hardware-change detection, especially USB attach/detach
```

## User-facing commands

Keep the shell surface minimal.

### `status`

Extend the existing resident `status` command instead of adding a separate
`battery` command.

Where supported, status should include at least:

```text
battery  : 73%
charging : yes
```

If a platform cannot provide a meaningful battery estimate, report the field as
unavailable rather than inventing a value.

Battery information should come from a resident power/platform owner. A future
application-facing Power ABI may expose the same normalized information to apps.

### `suspend`

Add a resident `suspend` command for a wakeable low-power state.

This must not be named `sleep`, because on Linux `sleep` normally means delay the
calling command for a time interval. Borrowing that name for device suspend would
be misleading.

The exact wake sources are platform-specific and should be defined when the Tab5
implementation is designed. MiniShell remains responsible for preparing shared
hardware before entering the low-power state and restoring normal ownership after
wake.

### `poweroff`

Add a resident `poweroff` command for an actual device shutdown when the platform
supports it.

This is intentionally distinct from `suspend`.

On the Tab5 reference hardware, charging is expected during normal powered and
initialized operation; full poweroff is a distinct state and should not be used
as the ordinary USB-powered idle mode. When continued charging and later wake are
desired, `suspend` is the preferred low-power action.

## Resident ownership

Power state is resident MiniShell responsibility:

```text
shell / application request
        |
        v
resident power/system owner
        |
        v
platform power backend
        |
        v
charger / power-management IC / MCU sleep state / wake sources
```

No ordinary ELF should directly own charger configuration or global device
shutdown state.

## Future Power ABI

Do not overload `system.write()` or the Time/Location ABI with power functions.
When an application genuinely needs battery or power-control access, add a
separate append-only Power ABI.

The first useful normalized concepts are likely:

```text
battery percentage, when known
charging state, when known
possibly external-power state, when reliably known
request suspend
request poweroff
```

Exact public structures/capabilities should be designed at implementation time,
with unit tests before the ABI is considered established.

Task 3 does not require an application-facing Power ABI merely to implement the
resident `status`, `suspend`, and `poweroff` commands. Add the ABI only when an
application requirement justifies it.

## Tab5 reference-platform notes

The Tab5 power hardware includes charging/power-monitoring facilities. MiniShell
should normalize only values it can support reliably; platform/library types must
not cross the public ABI boundary.

MiniShell should leave charging enabled during ordinary powered operation unless
a later explicit battery-management feature provides a reason to change that
policy.

## Future hardware-change detection

Hardware-change detection is planned later in Task 3, with USB attach/detach as
the first important case.

The initial ownership model should be:

```text
USB hardware / host controller
        |
        v
resident USB manager
        |
        +--> maintain canonical attached-device state
        +--> diagnostics / status
        `--> later app-facing notification/query API if required
```

Do not introduce a generic event ABI merely to anticipate hotplug. First build a
resident USB manager that can detect attach/detach and maintain current state.
Then let a real application requirement determine whether apps need:

```text
query-only access
polling/generation counters
an event queue
callbacks or another notification model
```

This keeps the architecture top-down and avoids designing an event framework
before its semantics are known.

## Development order

Task 3 should proceed in this order:

```text
battery information in status
        |
        v
suspend
        |
        v
poweroff
        |
        v
later: resident USB manager + hardware-change detection
```

This work is resident MiniShell/platform development and is independent of the
ordinary ELF command roadmap (`nano`, `cp`, `mv`, and so on).
