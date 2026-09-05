# MiniShell Design Principles

## Purpose

MiniShell exists to reduce repeated platform work in MCU applications.
Applications should be able to focus on their domain logic instead of repeatedly
bringing up and debugging RTC, USB, FATFS, SD, display, keyboard, audio, Wi-Fi,
and other common hardware.

The project intentionally adopts a subset of useful operating-system ideas while
remaining an MCU environment rather than attempting to reproduce a protected
multi-process OS.

## 1. One Hardware Owner

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
App B ----> storage API ----> MiniShell ----> SD/FATFS
Shell --/
```

This applies equally to display, keyboard/input, audio, USB, networking, RTC,
power management, buses, and other shared resources.

## 2. Protection by Convention

MiniShell does not provide process isolation or memory protection.

Applications and MiniShell execute in the same MCU environment. A defective app
can corrupt memory, reconfigure hardware, or crash the whole system.

That is accepted by design.

The contract is simple:

- MiniShell owns shared hardware.
- Normal applications use MiniShell services.
- Applications release resources and return cleanly.
- Direct hardware access is allowed only when the developer intentionally leaves
  the portable contract.
- If an application violates the contract, the developer is responsible for the
  consequences.

MiniShell provides structure and convenience, not protection from the developer.

## 3. Runtime API Instead of Repeated Platform Integration

Traditional MCU applications commonly compile platform libraries directly into
each firmware image. MiniShell instead keeps service implementations resident
and makes them available to applications at runtime.

An application may still include MiniShell headers at build time to know ABI
structures and function signatures, but the service implementation belongs to
MiniShell.

```text
application source
      |
      | MiniShell ABI declarations
      v
application.elf
      |
      | runtime service calls
      v
resident MiniShell
```

This allows MiniShell and applications to optimize independently while retaining
a stable contract.

## 4. Keep ESP-IDF Below the Boundary

The first implementation may use ESP-IDF extensively for the ESP32-P4 platform.
That is desirable: MiniShell should reuse mature MCU support rather than rewrite
USB, FATFS, SD, networking, timers, and drivers.

The public MiniShell ABI must not expose ESP-IDF-specific types.

Bad public API:

```c
esp_err_t mini_file_open(...);
TaskHandle_t mini_task_create(...);
```

Preferred public API:

```c
mini_result_t mini_file_open(...);
mini_task_t mini_task_create(...);
```

The platform implementation translates MiniShell concepts into ESP-IDF concepts.

## 5. Three Application Levels

MiniShell recognizes three useful application styles.

### Portable application

Uses only the standard MiniShell ABI.

Goal: source can be rebuilt for any platform implementing the required ABI.

### Platform-aware application

Uses the standard ABI plus documented platform-specific extensions.

Goal: exploit useful hardware features while keeping most application logic
portable.

### Bare-hardware application

Directly accesses MCU peripherals, registers, or platform SDK APIs.

Goal: maximum control where needed. Portability and system safety become the
application developer's responsibility.

## 6. Diagnostics Are a System Feature

MiniShell should make platform state inspectable before an application runs.

Examples:

```text
$> status
$> storage status
$> ls /flash
$> ls /sd
$> cat /sd/test.txt
$> rtc status
$> usb status
$> audio status
$> mem
```

If SD access works from the shell before an application starts, a well-behaved
application should not need to rediscover or reinitialize the SD hardware.

This narrows debugging boundaries dramatically.

## 7. Runtime-Loadable Apps

V1 uses native ELF applications.

An application should feel like a shell command:

```text
$> minift8 --band 20m
[application runs]
[application returns]
$>
```

ELF is an implementation choice for loading native code. It is not part of the
conceptual MiniShell service model. Other runtimes may be added later.

## 8. Top-Down Modular Design

Architecture is defined from responsibilities downward:

```text
application
    |
MiniShell ABI
    |
services
    |
platform abstraction
    |
SDK / drivers
    |
hardware
```

A module should have one clear responsibility and one clear owner for each
hardware resource.

## 9. Small Stable ABI

The ABI should expose useful capabilities without mirroring every function of the
underlying SDK.

Prefer small service groups such as:

- system
- memory
- filesystem/storage
- time
- display
- input
- audio
- USB
- network

New API surface should be added only when a real application needs it.

## 10. First Make One Platform Work Well

Tab5 / ESP32-P4 is the reference platform for V1.

Portability should influence boundary design, but the project should not build
multiple incomplete ports merely to prove abstraction. Once the Tab5 framework
is stable, a second architecture such as ESP32-S3 can validate the boundary.
