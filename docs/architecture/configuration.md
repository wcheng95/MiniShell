# MiniShell Configuration Ownership and Namespace

Status: **Canonical architecture rule**  
Date: 2026-09-22

## Purpose

Persistent configuration follows semantic ownership. MiniShell owns resident
runtime/settings state; each application owns its own settings. The filesystem
layout must reflect the code that actually implements that ownership.

## 1. Current MiniShell-owned namespace

MiniShell currently has **no generic** `/flash/config.txt`.

Implemented resident MiniShell persistence is namespaced under:

```text
/flash/minishell/
```

Current files are:

```text
/flash/minishell/setting.txt
    operator-facing resident settings
    currently WebFS SoftAP credentials

/flash/minishell/alias.txt
    resident shell command aliases

/flash/minishell/location.txt
    persistent default geographic location

/flash/minishell/gps_baud.txt
    resident GPS baud/autodetection state
```

Temporary transactional files such as `location.tmp`, `gps_baud.tmp`, or
feature-specific temporary files are implementation details and are not separate
configuration contracts.

### WebFS settings

The current `/flash/minishell/setting.txt` contract is:

```text
SSID=<stable SoftAP name>
PW=<stable WPA2 passphrase>
```

WebFS reads this file when launched. Missing or invalid settings fall back to
generated credentials.

The path is MiniShell-owned because WebFS is a resident ADV system utility, not
a portable domain application.

### Shell aliases

`/flash/minishell/alias.txt` is independently owned by the resident shell.
Each definition uses the first `=` as the separator:

```text
name=replacement
```

Built-ins retain precedence, expansion is one level, duplicate names use the
last definition, and edits are observed on the next lookup.

## 2. Application-owned configuration

The normal application settings namespace is:

```text
/flash/<app>/setting.txt
```

The application owns:

- parsing and validating its settings;
- defaults and migration;
- interpreting application-domain meaning;
- deciding when settings change;
- passing resulting state through its own architecture.

MiniShell Filesystem still owns file handles, path semantics, storage lifecycle,
and backend implementation. File ownership here means ownership of the contents'
meaning and policy.

Current example:

```text
/flash/minicw/setting.txt
```

Mini-CW owns CW-specific settings such as KeyIn/KeyOut mode, WPM, tone, volume,
TX delay, and message memories. MiniShell sees only generic Filesystem, Audio,
Input, Display, Time/Location, Memory, and Digital I/O operations.

## 3. MiniFT8 current exception

MiniFT8 retains its established configuration path:

```text
/flash/ft8/station.txt
```

Its contents remain MiniFT8-owned. A future rename to
`/flash/ft8/setting.txt` would be a separate migration and is not implied by
this architecture rule.

## 4. Hardware-specific application settings are allowed

Application portability does not require one universal deployment file.

For example, an application may persist deployment-specific GPIO choices or
radio endpoint selections while remaining portable at the API boundary:

```text
application setting
        |
        v
application controller/domain
        |
        v
MiniShell generic service
        |
        v
platform implementation
```

MiniShell may know a GPIO number, UART, I2C bus, Audio endpoint, or other generic
resource. It must not acquire application-domain meaning such as dit/dah, FT8
QSO policy, or Morse timing.

## 5. No side talk

Application settings do not become a shared global configuration service.

Structured applications keep configuration flow explicit through their owning
controller/domain modules. Sibling modules do not independently reach into
persistent settings merely because the Filesystem API is available.

## 6. No generic public Config API

This namespace does not imply a public MiniShell Config service.

Resident MiniShell features may read their own files under
`/flash/minishell/`. Applications read their own files through the existing
Filesystem API.

A generic Config service should be added only if a demonstrated cross-application
requirement later justifies one.

## 7. Boundary summary

```text
MiniShell resident settings/state
    /flash/minishell/setting.txt
    /flash/minishell/alias.txt
    /flash/minishell/location.txt
    /flash/minishell/gps_baud.txt

Application settings
    /flash/<app>/setting.txt

MiniFT8 current compatibility path
    /flash/ft8/station.txt

MiniShell services
    generic resource operations only
    no application-domain interpretation
```

This rule is independent of application packaging. An ADV application may run
compiled-in or from `/flash/apps/<app>.elf` / `/sd/apps/<app>.elf` while its
persistent settings remain in its own application namespace.
