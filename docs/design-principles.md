# MiniShell Design Principles

## Purpose

MiniShell exists to reduce repeated platform work in MCU applications.
Applications should focus on domain logic instead of repeatedly bringing up and
debugging RTC, storage, display, input, audio, USB, networking, and other common
hardware.

The project intentionally adopts a subset of useful operating-system ideas while
remaining an MCU application environment rather than a protected multi-process
OS.

## 1. One hardware owner

Shared hardware is owned by MiniShell.

Normal applications do not initialize or reconfigure shared peripherals. They
request services from MiniShell.

```text
BAD

App A ----> SD driver
App B ----> SD driver
Shell ----> SD driver

GOOD

App A --\
App B ----> Filesystem ABI ----> MiniShell ----> SD/FATFS
Shell --/
```

The same ownership rule applies to display, input devices, audio, USB,
networking, RTC, timers, power management, buses, and other shared resources.

A trusted application may request a global state change through a supported ABI
operation such as setting UTC or the configured default location. MiniShell still
owns the underlying RTC, persistence mechanism, or other hardware.

## 2. Protection by convention

MiniShell does not provide process isolation or memory protection.

Applications and MiniShell execute in the same MCU address space. A defective app
can corrupt memory, reconfigure hardware, or crash the entire system.

That is accepted by design.

The contract is:

- MiniShell owns shared hardware.
- Portable applications use MiniShell services.
- Applications release resources and return cleanly.
- MiniShell performs cooperative cleanup of remaining MiniShell-managed app
  resources where practical.
- Direct hardware access is allowed only when the developer intentionally leaves
  the portable contract.
- The developer owns the consequences of violating MiniShell assumptions.

MiniShell provides structure and convenience, not protection from arbitrary app
code.

## 3. Runtime API instead of repeated platform integration

Traditional MCU applications commonly compile platform libraries directly into
each firmware image. MiniShell keeps common service implementations resident and
makes them available to runtime-loaded applications.

An app includes MiniShell public headers to know the ABI, but the service
implementation belongs to MiniShell.

```text
application source
      |
      | MiniShell ABI declarations
      v
application.elf
      |
      | mini_api_get() + service-table calls
      v
resident MiniShell
```

This lets MiniShell and apps evolve independently while retaining a stable
contract.

## 4. Keep platform SDKs below the boundary

The ESP32-P4 reference implementation may use ESP-IDF extensively. That is
desirable: MiniShell should reuse mature MCU support rather than rewrite drivers
and libraries unnecessarily.

The public ABI must not expose platform-private types.

Bad public API:

```c
esp_err_t mini_file_open(...);
TaskHandle_t mini_task_create(...);
```

Preferred public API:

```c
mini_result_t ...;
mini_file_t ...;
```

The platform layer translates MiniShell concepts into platform SDK concepts.

## 5. Three application levels

### Portable application

Uses only the standard MiniShell ABI.

Goal: source can be rebuilt for any MiniShell platform that implements the
required services/capabilities.

### Platform-aware application

Uses the standard ABI plus documented platform-specific extensions.

Goal: exploit useful hardware while keeping most logic portable.

### Bare-hardware application

Directly accesses MCU peripherals, registers, or platform SDK APIs.

Goal: maximum control where necessary. Portability and system-state safety become
the application developer's responsibility.

## 6. Diagnostics are a system feature

MiniShell should make service/platform state inspectable before a user app runs.

Examples:

```text
M$> status
M$> mem
M$> storage status
M$> ls /sd
M$> date
M$> location
M$> rtc status
M$> usb status
```

If a service works from the shell or focused ABI test, a later application should
not need to rediscover or reinitialize the hardware.

## 7. Runtime-loadable apps

V1 uses native ELF applications.

An app should feel like a shell command:

```text
M$> minift8 --band 20m
[application runs]
[application returns]
M$>
```

The current native model is ordinary `main(argc, argv)` plus runtime API
acquisition through `mini_api_get()`.

ELF is a loading/container choice, not the conceptual service architecture.

## 8. Top-down modular design

Architecture is defined from responsibilities downward:

```text
application
    |
MiniShell ABI
    |
resident services
    |
platform implementation
    |
SDK / RTOS / bare-metal support
    |
hardware
```

Each hardware resource has one clear owner. Each module has one clear
responsibility.

## 9. Small stable ABI

The ABI should expose useful, portable capabilities without mirroring every
function of the underlying SDK.

Task 1 foundational services are:

```text
system
memory
filesystem
time/location
display
input
```

Likely later services include:

```text
audio
USB
network
power/system control
```

`console` is a higher-level composition, not a foundational ABI.

New surface area should be added only when a real application or system need
justifies it.

## 10. Design for compatible growth

ABI evolution should prefer:

```text
append-only tables
struct_size
capability bits
optional sub-APIs
stable numeric meanings
explicit ownership and lifetime
fixed-width public types
```

Do not change an established function's meaning to add a new feature.

Do not embed one extensible public struct by value inside another when future
growth would shift established field offsets.

## 11. Synchronous and understandable first

Task 1 APIs are synchronous unless explicitly documented otherwise.

V0 does not promise general ISR safety, reentrancy, or multi-thread safety.
Portable apps should serialize use of shared logical resources.

More complex asynchronous/concurrent behavior should be added only when a real
need justifies the additional contract.

## 12. First make one platform work well

Tab5 / ESP32-P4 is the reference platform for V1.

Portability should shape boundaries, but the project should not build several
incomplete ports merely to prove abstraction. Once the Tab5 foundation is stable,
a second architecture can validate the design without forcing premature
lowest-common-denominator choices.
