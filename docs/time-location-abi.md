# MiniShell Time/Location ABI v0

Status: **Task 1 design contract; provisional until implementation and hardware ABI tests pass**

## 1. Purpose

The Time/Location ABI provides applications with a stable platform-neutral
interface for:

```text
monotonic time
sleep/delay
UTC wall-clock time
configured/default geographic location
live geographic location
a coherent time/location snapshot
```

The service owns the platform details behind these functions, including hardware
timers, RTC devices, GPS-derived state, NTP-derived state, UTC correction policy,
and persistence of configured location.

Applications do not access RTC registers, GPS drivers, timer peripherals, NTP
libraries, or platform-private configuration storage directly when using this
ABI.

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

The service groups time and location because they are often produced, corrected,
and consumed together while keeping monotonic time logically independent from
UTC and geographic position.

```text
time/location
|-- monotonic time
|-- sleep
|-- UTC
|-- configured/default location
|-- live location
`-- snapshot
```

Monotonic time is never corrected when UTC changes.

UTC may be established/corrected by RTC, GPS, NTP, shell command, or a trusted
application request.

Location may come from persistent configured state, a live source such as GPS, or
both.

## 3. Service presence and capabilities

If the service is absent:

```c
api->time_location == NULL
```

If present, V0 requires monotonic time, sleep, and snapshot support.

Optional features use capability bits:

```c
#define MINI_TIMELOC_CAP_UTC                   (1ull << 0)
#define MINI_TIMELOC_CAP_LOCATION              (1ull << 1)
#define MINI_TIMELOC_CAP_SET_UTC               (1ull << 2)
#define MINI_TIMELOC_CAP_DEFAULT_LOCATION      (1ull << 3)
#define MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION  (1ull << 4)
```

`DEFAULT_LOCATION` means persistent configured location can be read. The separate
`SET_DEFAULT_LOCATION` bit means trusted applications are allowed to change it.

Future capability bits may describe accuracy, altitude, heading, speed, timezone
support, alarms, higher-resolution timing, or other additions. Existing numeric
meanings are never reused after ABI stabilization.

## 4. Monotonic time

```c
uint64_t (*monotonic_us)(void);
```

Semantics:

- unit is microseconds;
- zero point is unspecified and normally related to boot/timer initialization;
- the value must not go backward during normal execution;
- UTC corrections, RTC writes, GPS/NTP synchronization, or manual UTC setting do
  not change the monotonic timeline;
- implementations should use a free-running hardware counter where practical;
- V0 must not require a high-frequency periodic software tick solely to maintain
  microsecond monotonic time.

A platform may perform rare software bookkeeping when necessary to extend a
narrower hardware counter across wraparound. That does not violate the rule above;
the intent is to avoid continuous microsecond/millisecond software timekeeping
when hardware can count autonomously.

Monotonic time is the reference for elapsed-time measurement, timeouts,
application scheduling, and freshness calculations.

## 5. Sleep

```c
mini_result_t (*sleep_ms)(uint32_t milliseconds);
```

Semantics:

- delays/suspends the current foreground application for approximately the
  requested duration;
- synchronous in V0;
- not a hard real-time guarantee;
- `sleep_ms(0)` is a successful no-op or cooperative yield;
- future precise timers, alarms, or sleep-until operations are added without
  changing this V0 function.

## 6. UTC representation

UTC uses integer Unix/POSIX-style representation rather than a broken-down
calendar structure.

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

`nanoseconds` is the fractional part:

```text
0 .. 999,999,999
```

The field does not imply nanosecond accuracy. A system whose effective runtime
resolution is one microsecond may return fractional values only in multiples of
1000 ns.

Leap seconds are not represented as a distinct `23:59:60` civil-time value.
Local timezone and broken-down calendar conversion are outside the fundamental
V0 representation.

## 7. UTC runtime model

MiniShell should normally represent running UTC using a UTC anchor plus a
monotonic anchor.

```text
trusted UTC sample
      |
      v
utc_anchor
mono_anchor
      |
      | free-running monotonic counter advances
      v
utc_get() = utc_anchor + (monotonic_now - mono_anchor)
```

This avoids periodic software UTC bookkeeping.

If a hardware RTC exists:

- the RTC keeps ticking autonomously;
- MiniShell Time/Location exclusively owns the RTC driver;
- MiniShell may read RTC at boot to establish UTC;
- MiniShell may correct/write RTC when a trusted UTC source materially changes
  system UTC;
- applications use the ABI rather than manipulating RTC hardware directly.

## 8. UTC access and setting

```c
mini_result_t (*utc_get)(mini_utc_time_t *out_time);
mini_result_t (*utc_set)(const mini_utc_time_t *time);
```

`utc_get()` semantics:

```text
MINI_OK              valid UTC returned
MINI_ERR_NOT_READY   UTC is supported but not yet established
MINI_ERR_UNSUPPORTED UTC capability is absent
```

`utc_set()` is supported only when `MINI_TIMELOC_CAP_SET_UTC` is present. If the
capability is absent, calling it returns `MINI_ERR_UNSUPPORTED`.

`utc_set()` means:

> Request that MiniShell set/correct global system UTC.

MiniShell updates its UTC/monotonic anchor and, when appropriate, its owned
hardware RTC.

Applications using `utc_set()` are trusted. Changing UTC may affect all
applications and persistent RTC state. It never changes `monotonic_us()`.

A shell command such as `date` may use the same operation.

## 9. Geographic coordinate value

V0 uses WGS-84 latitude/longitude with fixed-point integer representation.

For configured/default location input/output, use a value-only structure:

```c
typedef struct {
    uint32_t struct_size;
    int32_t latitude_e7;
    int32_t longitude_e7;
} mini_geo_point_t;
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

This structure deliberately contains only coordinates. Runtime source/freshness
metadata is not accepted as configuration input.

## 10. Effective/live location structure

`location_get()` returns coordinates plus runtime metadata:

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

V0 sources:

```c
#define MINI_LOCATION_SOURCE_DEFAULT  1u
#define MINI_LOCATION_SOURCE_LIVE     2u
```

For LIVE, `updated_monotonic_us` identifies when MiniShell last updated the live
fix.

For DEFAULT, `updated_monotonic_us` is zero because configured persistent
location is not a live observation and cannot use the current boot's monotonic
clock as a meaningful age.

Future source values may be appended. Existing values are never reused.

## 11. Configured/default location

```c
mini_result_t (*location_default_get)(
    mini_geo_point_t *out_location);

mini_result_t (*location_default_set)(
    const mini_geo_point_t *location);
```

`location_default_get()` requires
`MINI_TIMELOC_CAP_DEFAULT_LOCATION`; otherwise it returns
`MINI_ERR_UNSUPPORTED`.

`location_default_set()` requires both default-location support and
`MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION`; otherwise it returns
`MINI_ERR_UNSUPPORTED`.

Setting default location means:

> Store the persistent fallback coordinates owned by MiniShell.

The ABI stores coordinates, not a human-readable place name. A shell/UI layer may
translate a name such as `Los Angeles, CA` into coordinates before calling this
operation.

MiniShell owns persistence. The application does not depend on whether storage is
NVS, flash file, another filesystem, or another platform-private mechanism.

Changing configured location modifies global system state and is trusted app
behavior.

## 12. Effective location

```c
mini_result_t (*location_get)(mini_location_t *out_location);
```

`location_get()` returns the currently effective application location.

Priority is conceptually:

```text
retained/current live location state
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
MINI_ERR_NOT_READY   location supported but no effective location exists
MINI_ERR_UNSUPPORTED location capability absent
```

Loss of a live source does not necessarily erase the most recent live fix.
MiniShell may retain it and expose its age through `updated_monotonic_us`.

There is no universal age threshold in V0. Freshness policy belongs to the
application. A later application may consider a ten-minute-old location useful
while another may reject it.

## 13. Snapshot

A snapshot provides a coherent read of current Time/Location service state.

It does **not** promise that UTC and location originated from the same GPS
observation or physical source.

The structure is deliberately flat rather than embedding extensible public
structures by value.

```c
#define MINI_TIMELOC_SNAPSHOT_UTC_VALID       (1ull << 0)
#define MINI_TIMELOC_SNAPSHOT_LOCATION_VALID  (1ull << 1)

typedef struct {
    uint32_t struct_size;
    uint32_t reserved_header;
    uint64_t valid_fields;

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

`valid_fields` is 64-bit so future snapshot validity bits can grow without
replacing the field.

The structure is flat because embedding `mini_utc_time_t` or `mini_location_t` by
value would make future growth of those inner structures shift later offsets in
the snapshot.

## 14. Snapshot semantics

```c
mini_result_t (*snapshot_get)(
    mini_time_location_snapshot_t *out_snapshot);
```

`monotonic_us` represents the snapshot instant.

If UTC is valid, returned UTC should correspond as closely as practical to that
same monotonic instant.

If location is valid, the location fields contain the effective location while
`location_updated_monotonic_us` preserves freshness information for a live
source.

`snapshot_get()` returns `MINI_OK` when the Time/Location service itself is
operating. `valid_fields` tells the caller whether UTC/location fields are
meaningful.

## 15. Service table

The intended V0 table is:

```c
typedef struct {
    uint32_t struct_size;
    uint64_t capabilities;

    uint64_t (*monotonic_us)(void);

    mini_result_t (*sleep_ms)(uint32_t milliseconds);

    mini_result_t (*utc_get)(mini_utc_time_t *out_time);
    mini_result_t (*utc_set)(const mini_utc_time_t *time);

    mini_result_t (*location_get)(mini_location_t *out_location);

    mini_result_t (*location_default_get)(
        mini_geo_point_t *out_location);

    mini_result_t (*location_default_set)(
        const mini_geo_point_t *location);

    mini_result_t (*snapshot_get)(
        mini_time_location_snapshot_t *out_snapshot);
} mini_time_location_api_t;
```

The V0 table fields above are present when the service exists. Optional flat
operations return `MINI_ERR_UNSUPPORTED` when their capability is absent.

After ABI stabilization, existing fields/functions are never reordered, removed,
or incompatibly repurposed. Future operations are appended.

## 16. Ownership boundary

MiniShell owns:

```text
free-running monotonic timer abstraction
UTC anchor/correction state
hardware RTC driver, if present
GPS/live location state, if present
NTP-derived correction state, if present
configured/default location persistence
source-selection policy
```

Applications consume state or request trusted changes through the ABI.

Direct hardware access remains possible only through MiniShell's general escape
hatch, in which case the application owns the consequences and may violate
MiniShell assumptions.

## 17. What V0 deliberately excludes

V0 does not standardize:

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

These may be added later through append-only fields, capability bits, or new
optional sub-APIs when a real portable need appears.

## 18. ABI test requirements

`abi_time_location.elf` should validate at least:

1. repeated `monotonic_us()` calls never go backward;
2. elapsed monotonic time approximately tracks a `sleep_ms()` interval;
3. `sleep_ms(0)` succeeds;
4. UTC unsupported/not-ready/valid behavior is distinguishable;
5. valid UTC advances from monotonic elapsed time without continuous software
   servicing;
6. `utc_set()` changes UTC when the capability is present;
7. `utc_set()` does not disturb monotonic time;
8. hardware RTC persistence is validated on Tab5 if its RTC is used;
9. configured/default location can be read and, when writable, set and persisted;
10. `location_get()` returns DEFAULT when no retained/current live location exists;
11. live location identifies source and exposes freshness information;
12. snapshot validity bits correctly describe available UTC/location fields;
13. snapshot monotonic time and UTC are internally coherent;
14. unsupported optional operations return `MINI_ERR_UNSUPPORTED`;
15. repeated app launch/exit cycles leave the service healthy.

The ABI remains provisional until the public interface, resident implementation,
focused ELF test, and real Tab5 behavior agree.
