# T035 — Persistent WebFS SoftAP credentials

Status: READY

## Architect intent

Allow WebFS to use stable SoftAP credentials stored at:

```text
/flash/minishell/setting.txt
```

so a phone/tablet can remember the MiniShell WebFS network and the user does not
need to enter a new password on every WebFS launch.

The architect has already created this runtime file on ADV with:

```text
SSID=<user value>
PW=<user value>
```

T035 must consume this file; it must not create, overwrite, migrate, or otherwise
modify it.

## Objective

At each `webfs` launch:

1. read `/flash/minishell/setting.txt` through the public MiniShell Filesystem API;
2. parse and validate `SSID=` and `PW=`;
3. if **both** are present and valid, use them unchanged for the SoftAP;
4. otherwise use the existing generated `MiniShell-XXXX` + random eight-uppercase-
   letter password fallback pair.

Stable credentials are therefore opt-in and recoverable: a bad settings file must
never make WebFS inaccessible.

## Current context

Task planning baseline:

```text
main = 7d5b05233024627bc5c1248666ef7c9c1516212b
```

T033/T034 are COMPLETE and hardware validated:

- on-demand foreground SoftAP;
- one associated station;
- generated fallback SSID/password;
- read-only browse/download;
- safe upload/replace, mkdir, same-directory regular-file rename, file delete,
  empty-directory delete;
- WebFS/FT8 same-boot coexistence;
- CPU1-owned USB Host lifetime for QMX/FT8;
- no public Network/HTTP API.

Canonical ownership now documents:

```text
/flash/config.txt
    low-level MiniShell resident/platform configuration

/flash/minishell/alias.txt
    resident shell aliases

/flash/minishell/setting.txt
    operator-facing settings for resident MiniShell utilities
```

Relevant documents:

```text
docs/architecture/configuration.md
docs/project/webfs.md
docs/project/codex/T033-adv-webfs-readonly.md
docs/project/codex/T034-webfs-mutations.md
```

## Architectural constraints

### Ownership

WebFS owns the meaning and validation of `SSID` and `PW`.

Do not add a generic public Config service.

All file access to the settings file must use:

```c
mini_api_get()->fs
```

Do not use POSIX/FATFS/VFS file access from WebFS settings code.

The ESP-IDF Wi-Fi layer must receive already validated credentials; it should not
parse MiniShell files or know settings-file policy.

### Read timing

Read settings once per `webfs` launch, before starting the SoftAP.

If the user modifies `/flash/minishell/setting.txt` while WebFS is running,
the active AP stays unchanged. New values take effect on the **next** WebFS launch.

No live settings watcher.

### Settings-file syntax

File:

```text
/flash/minishell/setting.txt
```

Recognized keys are case-sensitive:

```text
SSID=
PW=
```

Parsing rules:

- line-oriented text;
- accept LF and CRLF;
- blank lines ignored;
- lines whose first non-space character is `#` are comments and ignored;
- first `=` is the separator;
- key must be exactly `SSID` or `PW` with no surrounding whitespace;
- everything after the first `=` up to CR/LF is the literal value;
- unknown keys are ignored for forward compatibility;
- duplicate `SSID` or duplicate `PW` makes the settings invalid;
- values are not whitespace-trimmed.

This lets a password contain `=` or spaces while keeping parsing deterministic.

### Credential validation

Configured credentials are used only when **both** values are present and valid.

`SSID`:

- 1..32 bytes;
- printable ASCII only (`0x20..0x7e`);
- no control characters;
- value is used exactly as written.

`PW`:

- 8..63 bytes;
- printable ASCII only (`0x20..0x7e`);
- no control characters;
- value is used exactly as written.

Do not silently truncate either value.

Do not combine one configured value with one generated value. The pair is
all-or-nothing.

### Fallback behavior

Use the existing generated pair when:

- settings file does not exist;
- settings file cannot be opened/read completely;
- settings file is too large for the bounded parser;
- either recognized key is missing;
- duplicate recognized key;
- SSID/PW validation failure.

Fallback remains:

```text
SSID: MiniShell-XXXX
PW:   8 uppercase letters from the existing unambiguous alphabet
```

A malformed settings file must not prevent WebFS from starting.

### Diagnostics / secrecy

It is acceptable to emit a concise diagnostic such as:

```text
webfs: settings unavailable; using generated credentials
```

or:

```text
webfs: invalid settings; using generated credentials
```

Do **not** print/log the configured password in console/system/debug logs.

The existing ADV display may continue showing the active password because that is
part of the user-facing WebFS connection screen. No new password logging.

### Memory

No unbounded allocation.

Use a small fixed/bounded settings buffer or streaming line parser. A whole-file
buffer is acceptable only if its size is explicitly bounded to **1024 bytes or
less**.

No permanent static SRAM increase is expected.

Growing `webfs_wifi_t` to hold maximum SSID/password values is acceptable; it is
foreground stack/session state, not permanent resident storage.

Do not increase foreground or HTTP task stack sizes.

### Wi-Fi backend boundary

Refactor `webfs_wifi_start()` as needed so the caller can supply validated
optional credentials.

The Wi-Fi backend should:

- use configured credentials when supplied;
- otherwise generate the existing fallback pair;
- preserve AP-only mode;
- preserve WPA2-PSK;
- preserve one-station maximum;
- preserve 192.168.4.1;
- preserve RAM-only Wi-Fi storage;
- preserve T033/T034 Wi-Fi memory configuration.

No NVS credential persistence. The text file is the sole persistence source.

## Expected implementation shape

One reasonable layout:

```text
platform/adv/
    adv_webfs.c
        load settings before Wi-Fi startup
        choose configured vs fallback path
        preserve display/lifecycle

    adv_webfs_settings.c/.h
        pure/bounded parser
        MiniShell FS loader
        credential validation

    adv_webfs_wifi.c/.h
        accept optional validated SSID/PW
        fallback generation remains here or in a small helper
```

Exact split is flexible, but keep settings parsing out of shell/core and out of
the ESP-IDF Wi-Fi policy code.

## Non-goals

Do not include:

- STA/home-network mode;
- multiple Wi-Fi profiles;
- NVS credential persistence;
- captive portal;
- mDNS;
- HTTP authentication;
- HTTPS;
- WebFS browser settings editor;
- generic MiniShell config API;
- changes to `/flash/config.txt`;
- changes to aliases;
- text-editor work;
- FT8/USB ownership changes;
- T034 mutation changes.

## Acceptance criteria

- [ ] Valid `SSID` + `PW` from `/flash/minishell/setting.txt` are used unchanged.
- [ ] WebFS uses the same configured credentials across repeated launches/reboots.
- [ ] iPhone can reconnect to the same remembered SoftAP without receiving a new password.
- [ ] Settings are read through MiniShell Filesystem only.
- [ ] Missing settings file falls back to generated credentials.
- [ ] Read failure falls back to generated credentials.
- [ ] Missing SSID falls back to generated credentials.
- [ ] Missing PW falls back to generated credentials.
- [ ] Invalid SSID falls back to generated credentials.
- [ ] Invalid PW falls back to generated credentials.
- [ ] Duplicate SSID/PW falls back to generated credentials.
- [ ] Unknown keys/comments/blanks do not invalidate otherwise valid settings.
- [ ] First `=` separator preserves additional `=` in values.
- [ ] CRLF and LF both work.
- [ ] Oversized settings file is rejected/falls back without unbounded memory.
- [ ] No configured password is written to console/system/debug logs.
- [ ] Existing active-credential display remains correct.
- [ ] Editing setting.txt during a WebFS session takes effect only next launch.
- [ ] Generated fallback remains eight uppercase letters.
- [ ] T033/T034 browse/download/mutation behavior remains unchanged.
- [ ] FT8/QMX still starts after WebFS in the same boot.
- [ ] No public API/version change.
- [ ] No task stack increase.

## Automated tests

Add a host-testable parser/loader unit covering at least:

1. valid two-line LF file;
2. valid CRLF file;
3. reversed key order;
4. comments/blanks/unknown keys;
5. value containing `=`;
6. SSID length 1 and 32;
7. SSID length 0 and 33 rejected;
8. PW length 8 and 63;
9. PW length 7 and 64 rejected;
10. non-printable/control bytes rejected;
11. duplicate SSID;
12. duplicate PW;
13. missing SSID;
14. missing PW;
15. file not found;
16. read error;
17. oversized file;
18. partial MiniShell reads;
19. exact bounded-buffer edge;
20. loader closes every acquired file handle exactly once.

Add/adjust Wi-Fi source-boundary tests to assert:

- configured credentials bypass random fallback generation;
- fallback generation behavior remains unchanged;
- no NVS persistence introduced;
- max connection remains one;
- AP mode and WPA2 remain unchanged.

Keep all T033/T034 WebFS tests.

Run:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T035-build-unit
cmake --build /tmp/T035-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T035-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

Also rerun focused WebFS tests and JavaScript syntax/browser tests from T034.

Record:

- firmware-size delta;
- static SRAM delta;
- any `webfs_wifi_t` size increase;
- settings buffer size;
- stack-size changes (none expected).

## Manual / hardware validation

The architect already has:

```text
/flash/minishell/setting.txt
SSID=<chosen SSID>
PW=<chosen password>
```

After supervisor review:

1. Boot ADV.
2. Run `webfs`.
3. Confirm displayed SSID/PW match the configured values.
4. Connect iPhone once and save/remember the network.
5. Quit WebFS.
6. Run `webfs` again.
7. Confirm the same SSID/PW are reused and the iPhone does not need a newly
   generated password.
8. Browse/download/upload one file to confirm T034 remains intact.
9. Quit WebFS and run `ft8`; QMX/FT8 must start normally in the same boot.
10. Temporarily rename or invalidate `setting.txt`, launch WebFS, and confirm
    generated fallback credentials still work.
11. Restore the valid settings file and confirm configured credentials return on
    the next launch.

## Codex implementation notes

Codex fills this section before handoff.

### Implementation summary

### Files changed

### Invariants preserved

### Memory / firmware evidence

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Review the actual diff and evidence. Special attention:

- settings read only through MiniShell FS;
- bounded parser;
- no password logging;
- configured pair all-or-nothing;
- malformed/missing file never prevents WebFS startup;
- fallback credential generation unchanged;
- no NVS persistence;
- no STA-mode creep;
- T034 mutation and CPU1 USB/FT8 behavior unchanged.

No PR required.

## Architect test result

Pending.
