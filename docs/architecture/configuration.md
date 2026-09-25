# MiniShell Configuration Ownership and Namespace

Status: **Canonical architecture rule**  
Date: 2026-09-22

## Purpose

Persistent configuration follows semantic ownership. MiniShell owns resident
runtime/settings state; each application owns its own settings. The filesystem
layout must reflect the code that actually implements that ownership.

## 1. Current MiniShell-owned namespace

The documented resident configuration namespace is:

```text
/flash/minishell/setting.txt
    resident MiniShell settings

/flash/minishell/alias.txt
    resident shell command aliases
```

`setting.txt` is the single operator-facing resident settings file. WebFS
SoftAP credentials, boot commands and display brightness use it today. Future resident settings that become
operator-configurable, including GPS baud policy if exposed, should be added to
this file rather than creating additional public configuration files.

Backend-private persistence or cache files are implementation details and are
not part of the configuration contract.

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

### Boot settings

The same file also accepts these case-sensitive resident keys:

```text
SSID=<stable SoftAP name>
PW=<stable WPA2 passphrase>
brightness=100
startup=ft8;b
```

MiniShell reads at most 1,024 bytes once per boot/session after platform/services
initialization. A missing, unreadable, incomplete or larger file leaves boot
settings at their defaults. No valid prefix of a failed/oversized read runs.
Records accept LF or CRLF (also bare CR); keys have no surrounding whitespace.
Unknown keys and comment/blank lines are ignored. Duplicate `brightness` and
`startup` records use the last valid definition; an empty `startup=` clears an
earlier startup value. These rules do not change WebFS's credential validation
or duplicate-key fallback rules.

`brightness` is decimal digits only, 1 through 100 inclusive. Missing or invalid
values leave the platform default unchanged (or preserve an earlier valid
record). ADV applies the percentage once before startup commands, rounded to
its native 0..255 range; 100 maps to 255. Linux ignores this private platform
operation. It is not a public Display capability or an interactive command.

`startup` is split only on literal semicolons. Non-empty segments execute in
order through the normal shell dispatcher, with normal one-level live alias
expansion and synchronous foreground app execution. For `startup=ft8;b`, `b`
runs after FT8 returns, followed by the ordinary `M$>` prompt. Failures retain
normal shell diagnostics and do not stop later segments. Startup `exit` requests
are ignored; interactive `exit` keeps its existing behavior.

The existing 255-byte shell command limit applies to each startup segment;
overlong segments print `shell: command too long` and are skipped without
truncation. A startup record containing NUL is invalid. No quoting, escaping,
pipes, variables or additional scripting syntax is interpreted. Interactive
lines do not gain semicolon parsing. Startup does not repeat on app return;
changes to these two settings take effect on the next boot/session. WebFS
credentials continue to be loaded separately at each WebFS launch.

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
MiniShell resident settings
    /flash/minishell/setting.txt

MiniShell resident shell aliases
    /flash/minishell/alias.txt

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
