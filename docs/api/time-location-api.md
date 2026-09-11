# MiniShell Time/Location API

Status: **implemented on Linux and Cardputer ADV.**

## Purpose

Time/Location gives applications one platform-neutral owner for:

```text
monotonic time
sleep/delay
UTC wall-clock time
configured/default location
live location state
coherent snapshots
```

Applications do not access RTC registers, GPS drivers, NTP libraries, host clocks, or persistence mechanisms directly when using this API.

## Service model

```text
RTC / host clock / GPS / NTP / manual correction
                   |
                   v
        MiniShell Time/Location
                   |
                   v
              application
```

Monotonic time is independent of UTC correction.

## Capabilities

```c
MINI_TIMELOC_CAP_UTC
MINI_TIMELOC_CAP_LOCATION
MINI_TIMELOC_CAP_SET_UTC
MINI_TIMELOC_CAP_DEFAULT_LOCATION
MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION
```

Applications check capability bits rather than platform identity.

## Monotonic time

```c
uint64_t (*monotonic_us)(void);
```

- unit: microseconds;
- zero point unspecified;
- must not go backward during normal runtime;
- never changed by `utc_set()`;
- used for elapsed time, timeouts, scheduling, and freshness.

Linux maps this to `CLOCK_MONOTONIC` below MiniShell.

## Sleep

```c
mini_result_t (*sleep_ms)(uint32_t milliseconds);
```

Synchronous approximate sleep/delay. It is not a hard real-time guarantee. A zero duration is a successful no-op/yield-style request.

## UTC representation

```c
typedef struct {
    uint32_t struct_size;
    int64_t unix_seconds;
    uint32_t nanoseconds;
} mini_utc_time_t;
```

UTC uses Unix/POSIX-style seconds since 1970-01-01 00:00:00 UTC plus a fractional nanosecond field. The field does not imply nanosecond accuracy. Leap seconds are not represented as `23:59:60`.

## UTC runtime model

MiniShell maintains UTC from an anchor and the monotonic clock:

```text
trusted UTC sample
    -> utc_anchor + mono_anchor
    -> monotonic clock advances
    -> utc_get = anchor + elapsed monotonic time
```

This avoids continuously reading or rewriting a wall clock.

### Linux behavior

At MiniShell startup, Linux system UTC is assumed correct and is used to establish the initial anchor.

`date`/`utc_set()` may re-anchor MiniShell UTC for the current process when a few seconds of correction are needed. This does **not** change Linux system time and does **not** persist an offset. Restarting MiniShell reads Linux UTC again.

### Cardputer ADV behavior

Cardputer ADV uses the backend-owned DS3231 RTC at I2C address `0x68` when present. The shared ADV I2C bus currently defaults to:

```text
SDA  GPIO8
SCL  GPIO9
```

The pin pair is owned by `adv_i2c`, not by the RTC driver, because the RTC and other ADV I2C devices share one bus. `adv_i2c_configure_pins()` provides the configuration seam; the current platform bring-up uses the G8/G9 defaults.

At startup, a valid RTC establishes the UTC anchor. `date`/`utc_set()` writes the RTC and then re-anchors the in-memory MiniShell clock. The RTC is second-resolution, so persisted fractional nanoseconds are not retained across reboot.

The current MiniShell DS3231 backend accepts calendar years 2000 through 2099. If the RTC is absent or detected but not yet valid, including an asserted oscillator-stop flag, MiniShell starts from the deterministic fallback `2026-09-01 06:00:00 UTC` and advances from there using the monotonic clock. `date` can then initialize the RTC and clear the oscillator-stop flag. Actual RTC I/O failures remain errors rather than being silently replaced by fallback time.

RTC ownership is independent of `/flash`; loss of the internal filesystem does not disable UTC. Persistent default location still depends on `/flash`.

### Embedded behavior

A backend that owns a writable RTC may use the same `utc_set()` request to update its persistent RTC as part of its normal policy.

Therefore `MINI_TIMELOC_CAP_SET_UTC` means MiniShell UTC is settable; durability is a backend/platform policy, not a promise that every target persists the change.

## UTC access

```c
utc_get
utc_set
```

Typical results:

```text
MINI_OK              valid operation
MINI_ERR_NOT_READY   supported state not yet established
MINI_ERR_UNSUPPORTED capability absent
MINI_ERR_INVALID     malformed input
```

`nanoseconds` must be below one billion.

## Location representation

Configured/default location uses fixed-point WGS-84 coordinates:

```c
typedef struct {
    uint32_t struct_size;
    int32_t latitude_e7;
    int32_t longitude_e7;
} mini_geo_point_t;
```

Scaling is degrees × 10^7.

Runtime effective location also carries source and `updated_monotonic_us`. Current source values are DEFAULT and LIVE.

## Default location

```c
location_default_get
location_default_set
location_default_clear
```

Configured location is MiniShell-owned persistent state. On Linux it is stored privately under MiniShell state; the application does not know or depend on the storage mechanism.

The API stores coordinates, not human-readable names. A shell/UI may translate `Los Angeles, CA` to coordinates outside this low-level API.

## Effective location

```c
location_get
```

Conceptually:

```text
usable retained/live location -> LIVE
otherwise configured fallback -> DEFAULT
otherwise                    -> NOT_READY
```

V1 does not impose one universal freshness age; applications can judge `updated_monotonic_us` according to their needs.

## Snapshot

`snapshot_get()` returns a flat snapshot containing the monotonic instant plus optional UTC and effective location, with source/freshness and `valid_fields`.

## Ownership

Time/Location is the sole application-facing owner of:

```text
monotonic abstraction
UTC anchor/correction state
RTC access where present
live location state
configured location persistence
source-selection policy
```

Apps request state/changes through this service instead of touching a platform clock/RTC/GPS directly.

## API evolution

`struct_size`, fixed-width types, capability bits, and validity fields remain useful design mechanisms, but the API is not currently frozen for backward compatibility. Applications are rebuilt when this contract changes.

## Current verification

Host service tests cover:

- monotonic advancement and sleep;
- UTC availability;
- UTC correction, persistence-provider calls, and progression;
- persistence-write failure behavior;
- default-location set/get/clear persistence behavior;
- public service discovery.

ADV firmware CI builds the platform RTC integration together with the shared I2C owner. Hardware verification should confirm RTC bootstrap, persistent `date` update, unset-RTC fallback, and reboot retention on the actual Cardputer/RTC module.

`date` is a portable application using the same UTC operations.

## Deferred functionality

No current API requirement for:

```text
time zones/local civil time
calendar formatting/parsing inside the service
accuracy/uncertainty metadata
altitude/speed/heading
alarms/periodic timers
raw GNSS data
location names/geocoding
```

These should be added only when real application behavior requires a generally useful contract.
