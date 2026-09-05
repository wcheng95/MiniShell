# MiniShell Time/Location ABI v0

Status: **Task 1 design contract; provisional until implementation and hardware ABI tests pass**

## 1. Purpose

The Time/Location ABI provides applications with a stable platform-neutral interface for:

```text
monotonic time
sleep/delay
UTC wall-clock time
configured/default geographic location
live geographic location
a coherent time/location snapshot
```

The service owns the platform details behind these functions, including hardware timers, RTC devices, GPS-derived state, NTP-derived state, correction policy, and persistence of configured location.

Applications do not access RTC registers, GPS drivers, timer peripherals, NTP libraries, or platform-private configuration storage directly when using this ABI.

```text
GPS / RTC / NTP / manual setting / platform timer
                  |
                  v
       MiniShell Time/Location service
                  |
                  v
             application ABI
```

## 2. Core boundary

The service groups time and location because they are often produced, corrected, and consumed together, while keeping monotonic time logically independent from UTC and geographic position.

```text
time/location
├── monotonic time
├── sleep
├── UTC
├── default/configured location
├── live location
└── snapshot
```

Monotonic time is never corrected when UTC changes.

UTC may be corrected by an RTC, GPS, NTP, shell command, or trusted application request.

Location may come from a persistent configured default, a live source such as GPS, or both.

## 3. Service presence and capabilities

If the service is absent:

```c
api->time_location == NULL
```

If present, v0 requires monotonic time, sleep, and snapshot support.

Optional features use capability bits:

```c
#define MINI_TIMELOC_CAP_UTC               (1ull << 0)
#define MINI_TIMELOC_CAP_LOCATION          (1ull << 1)
#define MINI_TIMELOC_CAP_SET_UTC           (1ull << 2)
#define MINI_TIMELOC_CAP_DEFAULT_LOCATION  (1ull << 3)
```

Future capability bits may describe accuracy, altitude, heading, speed, timezone support, alarms, higher-resolution timing, or other additions. Existing meanings are never reused after ABI stabilization.

## 4. Monotonic time

```c
uint64_t (*monotonic_us)(void);
```

Semantics:

- unit is microseconds;
- zero point is unspecified and is normally related to boot or timer initialization;
- the value must not go backward during normal execution;
- UTC corrections, RTC writes, GPS synchronization, NTP synchronization, or manual clock setting must not change the monotonic timeline;
- implementations should use a free-running hardware timer where practical;
- the ABI must not require a periodic software interrupt or software counter update solely to maintain microsecond monotonic time.

The intended implementation pattern is on-demand reading of a hardware counter rather than continuously incrementing a software variable.

Monotonic time is the reference for elapsed-time measurement, timeouts, application scheduling, and freshness calculations.

## 5. Sleep

```c
mini_result_t (*sleep_ms)(uint32_t milliseconds);
```

Semantics:

- delays/suspends the current foreground application for approximately the requested duration;
- synchronous in v0;
- not a hard real-time guarantee;
- `sleep_ms(0)` is a successful no-op or cooperative yield;
- future precise timers, alarms, or sleep-until operations must be added without changing this v0 function.

## 6. UTC representation

UTC uses an integer Unix/POSIX-style representation rather than a broken-down calendar structure.

```c
typedef struct {
    uint32_t struct_size;
    int64_t  unix_seconds;
    uint32_t nanoseconds;
} mini_utc_time_t;
```

`unix_seconds` is seconds since:

```text
1970-01-01 00:00:00 UTC
```

`nanoseconds` is the fractional part in the range:

```text
0 .. 999,999,999
```

The field does not imply nanosecond accuracy. For example, a system whose effective clock resolution is one microsecond may return fractional values in multiples of 1000 ns.

Leap seconds are not represented as a distinct `23:59:60` civil-time value. Local timezone and broken-down calendar conversion are not part of the fundamental v0 representation.

## 7. UTC runtime model

MiniShell should normally represent running UTC using a UTC anchor plus a monotonic anchor.

Conceptually:

```text
trusted UTC sample
      |
      v
utc_anchor
mono_anchor
      |
      | hardware monotonic timer advances independently
      v
utc_get() = utc_anchor + (monotonic_now - mono_anchor)
```

This avoids periodic software timekeeping overhead.

If a hardware RTC exists:

- the hardware RTC keeps ticking autonomously;
- MiniShell Time/Location exclusively owns the RTC driver;
- MiniShell may read the RTC at boot to establish UTC;
- MiniShell may correct/write the RTC when a trusted UTC source materially changes system UTC;
- applications request UTC through the ABI rather than manipulating the RTC directly.

## 8. UTC access and setting

```c
mini_result_t (*utc_get)(mini_utc_time_t *out_time);

mini_result_t (*utc_set)(const mini_utc_time_t *time);
```

`utc_get()` semantics:

```text
MINI_OK              valid UTC returned
MINI_ERR_NOT_READY   UTC capability exists but valid UTC is not yet established
MINI_ERR_UNSUPPORTED UTC is not supported
```

`utc_set()` is available only when `MINI_TIMELOC_CAP_SET_UTC` is present.

It means:

> Request that MiniShell set/correct system UTC.

The application does not select or manipulate an RTC device directly. MiniShell updates its UTC/monotonic anchor and, when appropriate, its owned hardware RTC.

Applications using `utc_set()` are trusted. Setting UTC changes global system state and may affect all applications and persistent RTC state. It never changes `monotonic_us()`.

A shell command such as `date` may use the same service function.

## 9. Geographic coordinates

V0 uses WGS-84 latitude/longitude with fixed-point integer representation.

```c
typedef struct {
    uint32_t struct_size;

    int32_t latitude_e7;
    int32_t longitude_e7;

    uint32_t source;
    uint32_t reserved0;

    uint64_t updated_monotonic_us;
} mini_location_t;
```

Scaling:

```text
latitude_e7  = latitude degrees  * 10^7
longitude_e7 = longitude degrees * 10^7
```

Ranges:

```text
latitude_e7   -900000000 ..  +900000000
longitude_e7 -1800000000 .. +1800000000
```

The integer representation is stable, compact, architecture-neutral, and avoids making floating-point representation part of the ABI contract.

## 10. Location sources

V0 distinguishes at least:

```c
#define MINI_LOCATION_SOURCE_DEFAULT  1u
#define MINI_LOCATION_SOURCE_LIVE     2u
```

A configured/default location is persistent system configuration, for example a user's normal operating location such as Los Angeles, California.

A live location is supplied dynamically by a source such as GPS.

For `MINI_LOCATION_SOURCE_LIVE`, `updated_monotonic_us` identifies when MiniShell last updated the live location.

For `MINI_LOCATION_SOURCE_DEFAULT`, `updated_monotonic_us` is zero because a persistent configured location is not a live fix and its age across reboot is not meaningfully represented by the current monotonic timeline.

Future source values may be appended. Existing numeric meanings are never reused.

## 11. Default/configured location

```c
mini_result_t (*location_default_get)(mini_location_t *out_location);

mini_result_t (*location_default_set)(const mini_location_t *location);
```

These operations are available when `MINI_TIMELOC_CAP_DEFAULT_LOCATION` is present.

`location_default_set()` means:

> Set the persistent fallback geographic location owned and stored by MiniShell.

The ABI stores coordinates, not a human-readable place name. A shell or UI may convert a place name to coordinates before calling this function.

MiniShell owns persistence. The application does not depend on whether the value is stored in flash, NVS, a configuration file, or another platform-private mechanism.

Like `utc_set()`, changing the default location modifies global system state and is trusted application behavior.

## 12. Effective location

```c
mini_result_t (*location_get)(mini_location_t *out_location);
```

`location_get()` returns the currently effective application location.

Priority is:

```text
usable live location
        |
        v
     return LIVE

otherwise

configured default location
        |
        v
    return DEFAULT
```

Semantics:

```text
MINI_OK              effective location returned
MINI_ERR_NOT_READY   location capability exists but neither live nor default location is available
MINI_ERR_UNSUPPORTED location is not supported
```

Loss of a live source does not necessarily erase the most recent live fix immediately. The service may continue returning the latest known live location, with `updated_monotonic_us` allowing the application to judge freshness.

Freshness policy belongs to the application. MiniShell provides the timestamp; it does not impose one universal expiration threshold.

## 13. Snapshot

A snapshot provides a coherent read of current Time/Location service state.

It does **not** promise that UTC and location originated from the same GPS observation or the same physical source.

The structure is deliberately flat rather than embedding extensible public structs by value.

```c
#define MINI_TIMELOC_SNAPSHOT_UTC_VALID       (1u << 0)
#define MINI_TIMELOC_SNAPSHOT_LOCATION_VALID  (1u << 1)

typedef struct {
    uint32_t struct_size;
    uint32_t valid_fields;

    uint64_t monotonic_us;

    int64_t  utc_unix_seconds;
    uint32_t utc_nanoseconds;
    uint32_t reserved0;

    int32_t latitude_e7;
    int32_t longitude_e7;
    uint32_t location_source;
    uint32_t reserved1;

    uint64_t location_updated_monotonic_us;
} mini_time_location_snapshot_t;
```

The structure is flat because embedding `mini_utc_time_t` or `mini_location_t` by value would make future growth of those nested extensible structures change offsets inside the snapshot and break binary compatibility.

## 14. Snapshot semantics

```c
mini_result_t (*snapshot_get)(
    mini_time_location_snapshot_t *out_snapshot);
```

`monotonic_us` represents the snapshot instant.

If UTC is valid, the returned UTC should correspond as closely as practical to that same monotonic instant.

If location is valid, the location fields contain the currently effective location while `location_updated_monotonic_us` preserves freshness information for a live source.

`snapshot_get()` returns `MINI_OK` when the Time/Location service itself is operating. `valid_fields` tells the caller whether UTC and location fields are currently meaningful.

## 15. Service table

The intended v0 table is:

```c
typedef struct {
    uint32_t struct_size;
    uint64_t capabilities;

    uint64_t (*monotonic_us)(void);

    mini_result_t (*sleep_ms)(
        uint32_t milliseconds);

    mini_result_t (*utc_get)(
        mini_utc_time_t *out_time);

    mini_result_t (*utc_set)(
        const mini_utc_time_t *time);

    mini_result_t (*location_get)(
        mini_location_t *out_location);

    mini_result_t (*location_default_get)(
        mini_location_t *out_location);

    mini_result_t (*location_default_set)(
        const mini_location_t *location);

    mini_result_t (*snapshot_get)(
        mini_time_location_snapshot_t *out_snapshot);
} mini_time_location_api_t;
```

After ABI stabilization, existing fields/functions are never reordered, removed, or incompatibly repurposed. Future operations are appended.

## 16. Ownership boundary

MiniShell owns:

```text
free-running monotonic timer abstraction
UTC anchor/correction state
hardware RTC driver, if present
GPS-derived live location state, if present
NTP-derived correction state, if present
configured/default location persistence
source-selection policy
```

Applications consume or request changes through the ABI.

Direct hardware access remains possible only as the project's general trusted escape hatch, in which case the application owns the consequences and may violate MiniShell's assumptions.

## 17. What v0 deliberately excludes

V0 does not yet standardize:

```text
timezone databases
local civil time
calendar formatting/parsing
UTC accuracy/uncertainty metadata
RTC identity
GPS identity
NTP identity
altitude
horizontal accuracy
speed
heading
alarms
periodic timers
high-resolution sleep-until
raw GNSS data
location names/geocoding
```

These can be added later through append-only fields, capability bits, or new optional sub-APIs when a real portable requirement appears.

## 18. ABI test requirements

`abi_time_location.elf` should validate at least:

1. repeated `monotonic_us()` calls never go backward;
2. elapsed monotonic time approximately tracks a `sleep_ms()` interval;
3. `sleep_ms(0)` succeeds;
4. UTC unsupported/not-ready/valid behavior is distinguishable;
5. when UTC is valid, successive reads advance using monotonic time without continuous software servicing;
6. `utc_set()` changes UTC when the capability is present;
7. `utc_set()` does not disturb monotonic time;
8. hardware RTC persistence is validated on the Tab5 reference platform if its RTC is used;
9. configured/default location can be set, read back, and survives restart when supported;
10. `location_get()` returns the default when no usable live location exists;
11. when a live source exists, `location_get()` identifies it as live and supplies freshness information;
12. snapshot validity bits correctly describe available UTC/location fields;
13. snapshot monotonic time and UTC are internally coherent;
14. unsupported operations fail cleanly;
15. repeated app launch/exit cycles leave the service healthy.

The ABI remains provisional until the public interface, resident implementation, focused ELF test, and real Tab5 hardware behavior agree.
