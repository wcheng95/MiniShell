# T035 — Persistent WebFS SoftAP credentials

Status: COMPLETE

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

### Implementation summary

Implemented from `d04b7af8c484a4b17b3bbd7d278f395ee83211d3` on
`codex/T035-webfs-softap-settings`. No scope deviations.

WebFS reads `/flash/minishell/setting.txt` once per launch, before Wi-Fi startup,
through `mini_api_get()->fs`. The read-only loader accepts at most 1,024 file
bytes, handles partial reads, probes EOF at the exact limit, and closes every
acquired handle exactly once. Open/read/close errors, excess length, or invalid
settings yield a cleared credential pair and select the existing generated
fallback. It never creates or changes the settings file.

The parser operates on explicit byte ranges: LF/CRLF, comments/blanks, exact
case-sensitive keys, ignored unknown keys, first-`=` literal values, duplicate
rejection, printable ASCII, SSID 1..32 bytes and PW 8..63 bytes. It does not trim
values or accept a partial configured pair. Wi-Fi receives only an optional
validated pair; configured credentials bypass random generation. The fallback
retains RF entropy setup, `MiniShell-XXXX` and eight unbiased uppercase letters.

The existing short-credential display is retained. Longer configured values wrap
without truncation across up to six text rows; the seventh row retains the AP
address and Q/Esc hint. No configured password is emitted to logs or the console.
No live reload: edits become effective only at the next launch.

### Files changed

- `platform/adv/adv_webfs_settings.[ch]`: bounded parser and MiniShell FS loader.
- `platform/adv/adv_webfs.c`: one launch-time load, optional credential handoff,
  and full-length credential display.
- `platform/adv/adv_webfs_wifi.[ch]`: validated optional pair, larger session
  credential arrays, unchanged generated fallback and SDK lifetime policy.
- `platform/adv/main/CMakeLists.txt`: compile the settings module for ADV.
- `tests/adv_webfs_settings_test.c`: parser and fault-injected loader tests.
- `tests/adv_webfs_wifi_settings_test.py`: execute production credential-selection,
  SDK-config and display excerpts; check startup ordering and ownership boundaries.
- `CMakeLists.txt`: register both new host tests.
- This task packet: evidence and REVIEW status.

### Invariants preserved

Public API v3 is unchanged. No generic Config API, NVS persistence, STA mode,
settings writer/editor, or settings watcher. The Wi-Fi backend neither opens files
nor parses settings. AP-only WPA2, one station, RAM-only Wi-Fi storage,
192.168.4.1, Wi-Fi memory configuration, foreground lifecycle and Q/Esc cleanup
remain unchanged. T034 HTTP/mutation code and browser page are unchanged, as are
FT8 production code/profile, CPU1 USB Host ownership, LEVEL1 interrupt policy,
and FIFO 91/18/91. Foreground and HTTP task stack sizes are unchanged.

### Memory / firmware evidence

Built the exact starting baseline before edits and saved its ELF/BIN, then built
T035 with the installed ESP-IDF v5.5.4 toolchain. Compared ELF sections using
`xtensa-esp32s3-elf-size -A` and BIN sizes with `wc -c`.

| Measurement (bytes) | Baseline | T035 | Delta |
| --- | ---: | ---: | ---: |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,000 | 27,000 | 0 |
| `.dram0.bss` | 38,864 | 38,864 | 0 |
| Firmware BIN | 1,374,432 | 1,375,552 | +1,120 |

Total static internal-SRAM delta: **0 bytes**. IRAM vectors/end padding, RTC
sections and DRAM heap-start address (`1070219088`) are unchanged. Final BIN:
`0x14fd40`; app partition free: `0x4a02c0` bytes (78%).

Compiled size probes with real ADV flags and inspected them with
`xtensa-esp32s3-elf-nm -S`:

- `webfs_wifi_t`: 32 -> 108 bytes (+76), foreground session stack state.
- Validated credential pair: 97 bytes, foreground stack state.
- Settings file buffer: 1,024 bytes on the loader stack, plus a one-byte EOF probe.
  No settings heap allocation or permanent static buffer.
- HTTP context stays 4,616 bytes; shared buffers stay 4,608 bytes; transfer buffer
  stays 2,048 bytes. Nine URI handlers and two sockets remain unchanged.
- Foreground stack remains 16 KiB; HTTP stack remains 6 KiB.
- Compiler `-fstack-usage`: loader 1,072 bytes, parser 64 bytes, WebFS entry 336
  bytes. These are individual compile-time frames, not runtime high-water marks.

### Local tests run

Final gates passed:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 73/73 passed
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R adv_webfs
# 6/6 passed, including all T033/T034 regressions and embedded JS/browser tests

cmake -S tests/unit -B /tmp/T035-build-unit
cmake --build /tmp/T035-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T035-build-unit --output-on-failure
# 15/15 passed

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
# all passed

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# passed, real ESP32-S3 firmware; measurements above

git diff --check
# passed
```

Additional memory-safety check passed:

```bash
cc -std=c11 -g -O1 -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -Iinclude -Iplatform/adv \
  tests/adv_webfs_settings_test.c platform/adv/adv_webfs_settings.c \
  -o /tmp/T035-settings-sanitize
ASAN_OPTIONS=detect_leaks=0 /tmp/T035-settings-sanitize
```

Parser/loader coverage includes all 20 packet cases, all disallowed byte values
inside recognized values, embedded NUL, literal spaces and extra equals signs,
case/whitespace-sensitive keys, no final newline, every SSID/PW length through
one beyond the maximum, read chunks from 1..32 bytes, failure at every partial
read including EOF, failed exact-limit probe, close failure, impossible backend
read counts, repeated loads, and cleared output on every failure.

Wi-Fi/display tests execute production excerpts with SDK/display fakes, checking
configured bytes unchanged through SDK configuration, zero fallback RNG calls
when configured, new fallback generation each launch, unchanged uppercase
alphabet/length, maximum-length screen contents, and the short display boundary.
Source checks verify one load before startup, MiniShell-only file access,
AP/WPA2/RAM-only/one-station configuration and absence of credential logging.

### Manual/hardware validation still required

No hardware testing or flashing performed. After supervisor review, perform the
packet's configured-credential display/reconnect/reboot checks, next-launch edit
behavior, missing/invalid-file fallback, T034 transfer smoke, and same-boot FT8/QMX
startup. Runtime heap/stack and actual phone reconnection remain unmeasured here.

### Known limitations / risks

- Files larger than 1,024 bytes deliberately fall back, even if their first two
  lines contain valid credentials. Keep this resident settings file bounded.
- File close failure conservatively selects fallback, as do open/read errors.
- Unknown keys (including differently cased or whitespace-surrounded names) are
  ignored. Recognized values remain literal, including leading/trailing spaces.
- Persistent credentials are plain text in the operator-owned file, as specified;
  no additional persistence, authentication or security mode was introduced.

### Commit

One implementation commit on `codex/T035-webfs-softap-settings`, parent
`d04b7af8c484a4b17b3bbd7d278f395ee83211d3`, titled
`Load persistent WebFS SoftAP credentials through MiniShell FS`.
Exact pushed SHA is returned in the handoff. No PR.

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

## Supervisor review — implementation

Reviewed implementation commit:

```text
77e446a53e41231dd073b00c018f71f84e03639e
```

Result: **PASS — ready for ADV hardware validation.**

The implementation matches the T035 contract:

- `/flash/minishell/setting.txt` is opened/read/closed exclusively through the public MiniShell Filesystem API;
- settings are loaded once per WebFS launch, before Wi-Fi/HTTP startup;
- loader is bounded to 1,024 bytes and distinguishes exact-limit EOF from an oversized file;
- partial reads, read errors and close errors are handled conservatively;
- every acquired file handle is closed exactly once;
- parser accepts LF/CRLF, comments/blanks, unknown keys and first-`=` literal values while rejecting duplicate recognized keys;
- SSID is validated as 1..32 printable ASCII bytes and PW as 8..63 printable ASCII bytes with no silent truncation;
- configured credentials are all-or-nothing; every missing/invalid/error case selects the existing generated credential pair;
- configured credentials bypass random fallback generation entirely;
- generated fallback remains `MiniShell-XXXX` plus eight uppercase letters;
- Wi-Fi remains AP-only, WPA2, one station, 192.168.4.1, RAM storage and no NVS;
- Wi-Fi backend receives already validated credentials and has no filesystem or settings-parser responsibility;
- configured password is not added to console/system/debug logs;
- active credentials remain visible only on the intended local ADV connection screen;
- no live reload, STA mode, generic Config API, T034 mutation change, FT8 change, or USB ownership change.

Maximum credential geometry was also checked: 32-byte SSID plus 63-byte password fits the ESP-IDF AP configuration fields without truncation, and the ADV display uses at most six credential rows plus row 6 for the address/Q-Esc hint.

Validation is sufficient for hardware testing: Linux **73/73**, portable **15/15**, WebFS **6/6**, architecture checks, real ADV build and diff check pass.

Memory impact is appropriately bounded:

- firmware: **+1,120 B**;
- permanent static SRAM: **0 B**;
- `webfs_wifi_t`: +76 B foreground/session stack state;
- parsed credential pair: 97 B foreground stack state;
- settings read buffer: 1,024 B temporary loader stack;
- foreground stack remains 16 KiB; HTTP stack remains 6 KiB.

Hardware acceptance now only needs configured credential reuse/reconnect, fallback behavior, one T034 file operation, and same-boot FT8/QMX smoke.
## Architect test result

PASS on Cardputer ADV, 2026-09-20.

Hardware validation confirms:

- WebFS reads the configured SSID/PW from `/flash/minishell/setting.txt`;
- the configured credentials are displayed correctly;
- repeated WebFS launches reuse the same credentials;
- iPhone reconnects without requiring a newly generated password;
- T034 browser/file operations remain functional;
- invalid/unavailable settings fall back to generated `MiniShell-XXXX` plus an
  eight-uppercase-letter password;
- restoring the valid settings file restores the configured credentials on the
  next launch;
- WebFS exits cleanly;
- FT8/QMX starts normally afterward in the same boot.

T035 is COMPLETE.
