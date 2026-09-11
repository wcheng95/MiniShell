# MiniShell Configuration Ownership and Namespace

Status: **Canonical architecture rule**  
Date: 2026-09-10

## Purpose

Define who owns configuration meaning and where persistent configuration belongs without making MiniShell aware of application-domain concepts.

The core rule is:

```text
/flash/config.txt
    MiniShell-owned resident/platform configuration

/flash/<app>/setting.txt
    application-owned configuration and deployment settings
```

Configuration ownership follows semantic ownership, not merely the hardware resource eventually used.

## 1. MiniShell-owned configuration

`/flash/config.txt` is reserved for configuration that MiniShell itself needs to operate or adapt the platform.

Examples include:

```text
debug UART GPIO assignment
RTC GPIO/I2C assignment
GPS GPIO/UART assignment
GPS baud or auto-detection policy
resident resource sharing/detection/arbitration
other platform/runtime settings owned by MiniShell
```

If RTC and GPS share a physical connection, MiniShell owns the detection/arbitration because RTC/GPS are resident MiniShell resources.

The exact syntax of `/flash/config.txt` is not frozen by this architecture rule.

MiniShell configuration must not contain application-domain names or meaning such as:

```text
keyer_dit
keyer_dah
paddle
CW KeyOut
FT8 station behavior
application WPM
application tone
```

MiniShell may know a GPIO number, UART, I2C bus, audio endpoint, or other generic platform resource. It must not know why a domain application uses that resource.

## 2. Application-owned configuration

The canonical persistent application-settings path is:

```text
/flash/<app>/setting.txt
```

Examples:

```text
/flash/keyer/setting.txt
/flash/ft8/setting.txt
```

The application owns:

- parsing and validating its settings;
- defaults and application-level migration;
- interpreting the meaning of each setting;
- deciding when settings change;
- passing resulting configuration to its own modules through its normal application architecture.

MiniShell Filesystem still owns file handles, path semantics, storage lifecycle, and backend implementation. File ownership here means ownership of the **meaning and policy of the contents**, not ownership of the filesystem implementation.

## 3. Hardware-specific application settings are allowed

Application source/API portability does **not** require one universal settings file for every platform.

A portable application binary may use deployment-specific settings. For example, Keyer may contain:

```text
Dit GPIO       13
Dah GPIO       15
KeyOut GPIO    3
KeyOut GPIO 2  6
```

Those values describe what this Keyer deployment requires. They remain Keyer-owned settings because Keyer understands their application meaning.

The flow is:

```text
/flash/keyer/setting.txt
        |
        v
Keyer config_service
        |
        v
Keyer app_controller
        |
        v
KeyIn / KeyOut edge module
        |
        v
MiniShell Digital I/O
        |
        v
generic platform GPIO implementation
```

MiniShell receives only generic operations such as configuring, reading, or writing a digital line. It does not know that GPIO 13 is `dit`, GPIO 15 is `dah`, or GPIO 3 keys a transmitter.

Therefore:

> Application code can remain portable while application deployment settings are hardware-specific.

## 4. No side talk still applies

Application configuration does not become a shared service that every sibling module may query.

For Keyer:

```text
config_service ----X----> keyout
config_service ----X----> keyin

config_service
      |
      v
app_controller
   /       \
  v         v
keyin     keyout
```

`app_controller` reads/co-ordinates application configuration and passes the required values/state to the owning modules.

The same rule applies to other structured MiniShell applications.

## 5. No generic public MiniShell Config service is implied

This naming/ownership rule does not create a generic public Config API.

MiniShell may read `/flash/config.txt` internally because MiniShell owns that file's semantics. Applications may read their own settings through the existing MiniShell Filesystem API and implement application-local configuration modules.

Add a public Config service only if a separate, demonstrated cross-application requirement later justifies one.

## 6. MiniFT8 transition

MiniFT8 currently uses:

```text
/flash/ft8/station.txt
```

The canonical application-settings namespace is now:

```text
/flash/ft8/setting.txt
```

C4 does **not** rename the existing MiniFT8 file or change FT8 behavior. Moving `station.txt` to `setting.txt` is a separate MiniFT8 migration task and should include any compatibility/migration behavior needed at that time.

Until that migration occurs, `station.txt` is a documented legacy/current implementation path, not a different ownership model: MiniFT8 still owns its contents.

## 7. Keyer consequence

Keyer will use:

```text
/flash/keyer/setting.txt
```

from its first implementation rather than inheriting Mini-CW's old `/fatfs/setting.txt` path.

The settings may include KeyIn/KeyOut GPIO assignments, KeyIn/KeyOut modes, WPM, tone, volume, iambic behavior, Audio endpoint selection, TX delay, and other Keyer-domain behavior.

MiniShell must not add Keyer-specific entries to `/flash/config.txt` to support those settings.

## 8. Boundary summary

```text
MiniShell config
    /flash/config.txt
    what MiniShell itself needs to operate/adapt the platform

Application settings
    /flash/<app>/setting.txt
    what the application wants and how that deployment is configured

MiniShell services
    generic resource operations only
    no application-domain interpretation
```

This rule is independent of application packaging. An external ADV application may run from `/flash/apps/<app>.elf` or `/sd/apps/<app>.elf` while its canonical persistent settings remain under `/flash/<app>/setting.txt`.
