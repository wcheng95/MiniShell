# T033 — ADV WebFS read-only SoftAP proof

Status: IMPLEMENTING — USB INTERRUPT REGRESSION

## Architect intent

Add the first phase of WebFS to Cardputer ADV so routine `/flash` and `/sd`
file access can be done from a phone/tablet/laptop browser without USB media
handoff or cable unplug/replug.

The accepted V1 architecture is:

```text
WebFS V1 = on-demand foreground SoftAP mode
```

T033 is deliberately read-only. It must prove Wi-Fi/HTTP lifecycle, bounded
memory, browser usability, MiniShell Filesystem ownership, and clean repeated
start/stop before file mutation is added in T034.

## Objective

Add an ADV-only command:

```text
M$> webfs
```

that starts a private WPA2 SoftAP and lightweight HTTP server, shows connection
information on the ADV display, serves a self-contained file-browser page, lets
the browser list `/flash` and `/sd` and download files, and returns cleanly to
MiniShell on Q/Esc.

## Current context

Task creation baseline:

```text
main = 6e6af91b22754d156602df4c6d54050745474dc2
```

Relevant current architecture:

- MiniShell public API v3 already provides all read operations T033 needs:
  `stat`, `open/read/close`, `dir_open/read/close`, and `space`.
- MiniShell Filesystem owns logical namespace, normalization, handles, cleanup,
  quota policy, and backend dispatch.
- ADV mounts internal FATFS wear-levelled flash at `/flash` and removable SDSPI
  FATFS at `/sd`.
- `usbmsc` is an ADV-specific system utility that temporarily hands raw storage
  to USB. WebFS must **not** use that handoff; its purpose is to avoid normal
  cable/USB-role file-transfer workflow.
- Foreground applications are bracketed by
  `minishell_services_app_begin()/app_end()`. Since WebFS is foreground and its
  HTTP server is stopped before return, normal application-scoped FS handles are
  sufficient.
- ADV application tasks have a 16 KiB stack and the board has no PSRAM.
- Current ADV build uses ESP-IDF 5.5.x. ESP-IDF provides SoftAP through
  `esp_wifi` and a lightweight `esp_http_server` component with chunked
  responses/file-serving patterns.

Canonical feature plan:

```text
docs/project/webfs.md
```

## Architectural constraints

### Placement

WebFS is an **ADV-specific MiniShell system utility**.

It may be registered in `platform/adv/adv_apps.c` like `usbmsc`, but its
implementation belongs under `platform/adv/`, not under portable `apps/`.

Linux gets no fake `webfs` command in T033.

### Filesystem boundary

All logical file/directory access must use:

```c
const mini_api_t *api = mini_api_get();
api->fs->...
```

Do not call POSIX/FATFS/VFS open/read/stat/opendir/etc. for WebFS file content.

The only platform APIs WebFS owns directly are Wi-Fi/network/HTTP lifecycle and
platform-specific runtime diagnostics needed to measure that lifecycle.

No public MiniShell API changes. Do not bump `MINISHELL_API_VERSION`.

### Foreground lifecycle

While `webfs` runs:

- it is the foreground application/utility;
- FT8 and other foreground applications are not running;
- `/flash` and `/sd` remain mounted normally;
- USB console/USB host ownership is unchanged;
- do not call `adv_console_suspend_for_usb()`;
- do not call `adv_filesystem_handoff_begin/end()`;
- do not start TinyUSB MSC.

On exit:

1. stop accepting HTTP work;
2. stop the HTTP server and wait until handlers are gone;
3. stop/deinitialize the WebFS Wi-Fi session;
4. release WebFS-owned dynamic buffers/state;
5. return from the command;
6. only then may normal app lifecycle cleanup run.

Repeated start/stop must not leak tasks, sockets, netifs, or meaningful heap.

### SoftAP V1

Use AP-only mode.

User-visible contract:

```text
SSID: MiniShell-XXXX
PASS: <session password>
URL : http://192.168.4.1/
```

Requirements:

- SSID suffix is session/device-distinguishing and printable;
- generate a new local session password using ESP32 hardware RNG / ESP-IDF random source;
- password is exactly 8 lowercase ASCII letters so iPhone entry never requires
  switching keyboard character classes; use an unambiguous lowercase alphabet;
- 8 characters is intentionally the WPA2-PSK minimum because the AP is ephemeral,
  local-only, one-client, and exists only while WebFS is foreground;
- WPA2-PSK or stronger compatible AP security; never open;
- credentials are not persisted;
- one associated Wi-Fi station maximum in V1;
- fixed local address `192.168.4.1`;
- no STA connection, Internet forwarding, NAT, DNS captive portal, or mDNS.

### ADV display/input

Use MiniShell Display/Input services for the user-visible WebFS screen and Q/Esc
exit where practical; do not create a second keyboard/display ownership model.

The 20x7 screen should convey, compactly:

```text
WebFS
MiniShell-XXXX
PW <password>
192.168.4.1
Browse /flash /sd
Q = stop
```

Exact line packing may adapt to the generated credential length while remaining
legible in 20 columns.

### Browser page

Serve one self-contained HTML/CSS/JavaScript page from firmware.

No CDN, external script, font, image, Internet fetch, or runtime file dependency.

Keep the page intentionally small and functional. It should:

- show current path;
- expose `/flash` and `/sd` navigation;
- list directories/files;
- show file sizes;
- show total/used/free space for the current volume;
- allow entering a directory;
- allow navigating to parent without escaping the selected volume;
- download a regular file;
- display clear API errors.

Sorting may be client-side. T033 does not require touch-specialized styling, but
the page must be usable in ordinary current mobile/desktop browsers.

### HTTP API

A minimal acceptable T033 surface is:

```text
GET /                         embedded WebFS page
GET /api/list?path=/flash     directory listing + space facts
GET /api/file?path=...        streamed regular-file download
```

A harmless `/favicon.ico` response may be added to avoid noisy browser errors.

Do not add mutation endpoints in T033.

Directory responses may use JSON. The server must stream/chunk entries rather
than cache an arbitrary directory in RAM.

For each entry return only bounded factual data needed by the page, e.g.:

```text
name
type = file|dir
size for regular files
```

Use `fs->space()` for current-volume storage facts. T033 hardware testing exposed
that ADV currently returns `MINI_ERR_UNSUPPORTED` from `space()` because ADV has
no global MiniShell storage quota. Fix this at the Filesystem ownership boundary,
not in WebFS: preserve quota-based space reporting when a MiniShell storage limit
is configured, and when the limit is zero allow a private backend space hook to
report physical per-volume total/free bytes. ADV should implement that hook for
`/flash` and `/sd` using the mounted FAT volumes. Public `mini_fs_api_t` and API
version remain unchanged.

File download must stream through a bounded transfer buffer and close the
MiniShell file handle exactly once on every acquired-handle path.

### Path and encoding safety

HTTP input is untrusted even on a private AP.

Implement one bounded URL/query decoder/path validator before FS calls.

Rules:

- decode percent encoding exactly once;
- reject malformed `%xx`;
- reject embedded NUL/control bytes;
- decoded path must be absolute;
- accepted roots are exactly `/flash` and `/sd`;
- descendants must remain under one of those roots;
- reject `.` and `..` path components rather than allowing traversal;
- enforce fixed path/query capacity;
- never concatenate into an undersized buffer;
- JSON-escape names;
- HTML/JS must not inject raw filename text into markup without safe escaping.

UTF-8 filenames supported by the existing FATFS configuration should remain
representable; do not add ASCII-only filename policy.

### Memory

No unbounded allocation.

It is acceptable for ESP-IDF Wi-Fi/HTTP to allocate internal heap while WebFS is
active; that is one reason V1 is on-demand.

WebFS-owned buffers should be bounded and released on exit. Prefer one reusable
transfer buffer rather than per-request large allocations.

Report at handoff:

- firmware size delta;
- static `.bss`/`.data` delta attributable to WebFS;
- configured HTTP task stack;
- WebFS-owned transfer/context buffer sizes;
- any ESP-IDF configuration changes;
- expected dynamic-memory owners.

Do not increase the existing 16 KiB foreground app stack merely to make WebFS
work unless measurement demonstrates a real need and the task packet is updated
first.

## Suggested implementation shape

One reasonable layout:

```text
platform/adv/
    adv_webfs.c             foreground command/lifecycle/display/input
    adv_webfs_wifi.c/.h     SoftAP lifecycle
    adv_webfs_http.c/.h     esp_http_server + URI handlers
    adv_webfs_logic.c/.h    pure bounded URL/path/JSON helpers
    adv_webfs_page.h        firmware-resident HTML/JS

platform/adv/main/
    webfs_static.c          static command wrapper if needed
```

Exact file split is flexible, but do not put the implementation into
`shell.c` or `adv_apps.c`.

`adv_webfs_logic` should remain independent of ESP-IDF so its path/encoding
helpers can be unit-tested by host CTest.

## Non-goals

Do not include in T033:

- upload/write/overwrite;
- mkdir;
- rename;
- delete/rmdir;
- browser text editor;
- cross-volume copy/move;
- ZIP/archive operations;
- STA mode or saved Wi-Fi credentials;
- mDNS;
- captive portal/DNS interception;
- Internet routing/NAT;
- HTTPS/TLS;
- user accounts;
- remote shell;
- WebSocket terminal;
- always-on/background WebFS;
- concurrent FT8 + WebFS;
- Linux WebFS;
- public Network or HTTP MiniShell APIs;
- changing `usbmsc`;
- changing filesystem persistence/layout.

These belong to later tasks only if justified.

## Acceptance criteria

- [ ] `webfs` appears as an ADV command/application and starts from `M$>`.
- [ ] Starting WebFS requires no USB role change or cable unplug/replug.
- [ ] A WPA2-protected ephemeral SoftAP starts and shows SSID/password/IP on ADV.
- [ ] Browser at `http://192.168.4.1/` loads entirely without Internet.
- [ ] Browser can list `/flash` through MiniShell Filesystem.
- [ ] Browser can list `/sd` when present.
- [ ] Missing/unavailable SD is shown cleanly without killing WebFS.
- [ ] Directory names and regular-file sizes are correct.
- [ ] Current-volume total/used/free space is displayed.
- [ ] Browser can navigate nested directories without escaping `/flash` or `/sd`.
- [ ] Browser can download a small file exactly.
- [ ] Browser can download a file larger than the transfer buffer, proving streaming.
- [ ] Directory listing does not cache an arbitrary number of entries.
- [ ] Downloads do not buffer the whole file.
- [ ] Malformed percent encoding/path traversal/control-byte attempts are rejected.
- [ ] Q/Esc stops WebFS and returns to a functional `M$>`.
- [ ] Repeated start/stop works without task/socket/netif leaks or meaningful heap loss.
- [ ] MiniShell USB console and USB-host ownership are not altered by WebFS.
- [ ] Existing `/flash` and `/sd` filesystem behavior remains unchanged afterward.
- [ ] No public MiniShell API/version change.
- [ ] No existing Linux/ADV regression.

## Automated tests

Add a host-testable focused unit for pure WebFS logic covering at least:

1. valid `/flash`, `/sd`, and nested paths;
2. reject relative paths;
3. reject `.` and `..` components;
4. reject root escape attempts including percent-encoded traversal;
5. malformed percent sequences;
6. percent-decoded UTF-8 bytes survive;
7. encoded slash behavior is handled deliberately and cannot escape roots;
8. bounded path/query overflow;
9. JSON escaping for quote, backslash and control characters;
10. any URI/query helper boundary cases introduced by the implementation.

If practical without ESP-IDF, add a small fake-FS test around directory-response
streaming. Do not create a fake HTTP stack merely to claim integration coverage.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T033-build-unit
cmake --build /tmp/T033-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T033-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

Add any WebFS-specific compile/config guard needed to make required ESP-IDF
components explicit.

## Manual / hardware validation

After supervisor review/merge, architect validates on Cardputer ADV.

Use a phone/tablet/laptop browser. A PC browser/curl may additionally verify
byte-exact download behavior.

1. Boot to MiniShell with normal ADV storage mounted.
2. Leave normal USB/QMX cabling physically unchanged.
3. Run `webfs`.
4. Record free/largest heap before WebFS if available.
5. Confirm SSID/password/IP are readable on the ADV display.
6. Connect one client to the SoftAP.
7. Open `http://192.168.4.1/`.
8. Browse `/flash`, nested directories, and `/sd` when present.
9. Confirm space values are plausible.
10. Download a known small text file and confirm contents.
11. Download a file larger than the transfer buffer and confirm complete size/content.
12. Press Q/Esc and confirm immediate return to usable `M$>`.
13. Confirm normal `ls /flash` / `ls /sd` still work.
14. Repeat WebFS start/connect/browse/stop at least three times.
15. Compare heap after each stop; investigate monotonic loss.
16. Confirm USB console/host behavior did not require cable re-enumeration because
    WebFS never took ownership of USB.
17. Test with SD absent if practical; `/flash` must remain usable.

Record runtime heap/minimum-free evidence and any observed AP/HTTP task stack
high-water if available.

## Codex implementation notes

### Implementation summary

Implemented the read-only ADV system utility on
`codex/T033-adv-webfs-readonly`, based on requested `main`
`f0d759600c8d277765546a6270180d813f0354b3`. The canonical ownership and scope in
`docs/project/webfs.md` are preserved.

`webfs` runs on the existing foreground application task. It displays the
session SSID, 12-character password, URL, roots, and Q/Esc instructions using
MiniShell Display; exit uses MiniShell Input. The AP uses WPA2-PSK, one station,
RAM-only Wi-Fi configuration, and the default AP netif's checked
`192.168.4.1` address. No credentials are persisted. A 16-bit random suffix
and a uniformly selected 60-bit password come from `esp_fill_random` with RF
entropy enabled. Before advertising the AP, the Wi-Fi driver briefly enables
promiscuous reception solely to activate RF entropy, installs no receive
callback, then disables it. This avoids borrowing/resetting the battery ADC.
The Wi-Fi mode is AP throughout; there is no STA connection or scanning UI.

The firmware page provides root/parent navigation, client-side sorting, sizes,
volume space facts, and native browser downloads. It has no external assets or
Internet dependency. Filename display uses DOM `textContent`, and request paths
use `encodeURIComponent`. The only registered routes are GET `/`, `/api/list`,
`/api/file`, and an empty favicon response. GET request bodies are rejected.

The HTTP task calls the pure streaming helpers with `mini_api_get()->fs`.
Directory reads retain one entry, stat its logical child path for size, and
stream JSON; file reads use the single reusable 2048-byte transfer buffer.
Partial reads are supported, premature EOF is an error, and successful stream
termination is sent only after a successful close. Every acquired handle gets
exactly one close attempt, including FS failures, disconnected clients, and
cancellation. Errors before streaming receive HTTP status plus JSON diagnostics;
a failed in-progress response is closed without a successful chunk terminator.
The page reports incomplete/failed listings instead of presenting them as complete.

One bounded query decoder accepts exactly one `path=` parameter, decodes once,
preserves UTF-8 bytes, and validates before FS access. Encoded slashes become
separators and receive the same component checks. Double-encoded percent data
remains literal. Relative/foreign roots, dot components, malformed escapes,
NUL/control bytes, backslashes (also FAT separators), duplicate/trailing slashes,
extra parameters, and capacity overflow are rejected. Names are JSON-escaped.

Shutdown marks the context as stopping before blocking in `httpd_stop`.
Cancellation checks cover request entry, streaming, and socket send/receive
callbacks so a client trickling headers/body cannot perpetually delay exit.
Socket send/receive timeouts are two seconds; backend FS-call latency still
contributes to exit latency. HTTP handlers are synchronous on the one HTTP task;
no asynchronous work outlives server stop. Only then are Wi-Fi, the AP netif,
default event loop, and the application-owned context released. Startup failures
use the same cleanup path. A stop failure is logged/retried while retaining
ownership rather than returning with live handlers or Wi-Fi state.

### Files changed

- `platform/adv/adv_webfs.c`: foreground command, Display/Input, heap diagnostics.
- `platform/adv/adv_webfs_wifi.[ch]`: ephemeral AP and Wi-Fi lifecycle.
- `platform/adv/adv_webfs_http.[ch]`: bounded HTTP configuration, routes, cancellation.
- `platform/adv/adv_webfs_logic.[ch]`: pure path/JSON and MiniShell FS streaming.
- `platform/adv/adv_webfs_page.h`: self-contained browser page in flash.
- `platform/adv/adv_apps.c`: ADV-only command registration.
- `platform/adv/main/CMakeLists.txt`: sources and explicit ESP-IDF dependencies.
- `platform/adv/sdkconfig.defaults`: explicit SoftAP/DHCP and HTTP input capacities.
- `tests/adv_webfs_test.c` and root `CMakeLists.txt`: focused host CTest.
- This task packet: implementation and validation evidence.

### Invariants preserved

No portable application, public API/version, filesystem implementation/layout,
USB console/host ownership, USB MSC code, or foreground stack-size change.
All content/directory/space access uses MiniShell Filesystem. Direct socket
operations are private HTTP transport operations, not filesystem bypasses.
No mutations, STA, mDNS, captive portal, HTTPS, background WebFS, public network
service, or second file database. No arbitrary directory/file cache on ADV.

### Memory / firmware evidence

Measured before/after real ADV builds with the same installed ESP-IDF
`v5.5.4-dirty` toolchain and configuration, except the documented HTTP URI limit.
The baseline ELF/BIN/MAP were saved before implementation. `xtensa-esp32s3-elf-size
-A` section totals and BIN byte counts give:

| Measurement | Baseline | T033 | Delta |
| --- | ---: | ---: | ---: |
| Firmware BIN | 802128 B (`0xc3d50`) | 1368880 B (`0x14e330`) | +566752 B |
| `.dram0.data` | 19384 B | 27000 B | +7616 B |
| `.dram0.bss` | 25608 B | 38864 B | +13256 B |
| Data + BSS | 44992 B | 65864 B | +20872 B |
| `.iram0.text` | 54903 B | 81975 B | +27072 B |

IRAM alignment adds a further 64 B, so the overall static internal SRAM cost
including linked Wi-Fi/network dependencies is **48008 B**. The application
partition remains 78% free. WebFS's own only writable static is the one-byte
`tcpip_ready` flag; the larger data/BSS increase comes from the newly linked
ESP-IDF networking components. The embedded page is 2897 B including NUL in
flash rodata, not a RAM copy.

Actual Xtensa `sizeof` probes report:

- `webfs_buffers_t`: 4608 B (query 1536, decoded path 512, child path 512,
  reusable transfer/JSON buffer 2048).
- `webfs_http_t`: 4616 B, one MiniShell Memory allocation released after stop;
  includes buffers, cancellation flag/alignment, and server handle.
- `webfs_wifi_t`: 36 B on the foreground stack, including displayed credentials.
- HTTP task stack: 6144 B; two client sockets, four URI handlers.
- Existing foreground stack: unchanged at 16384 B.

Recompiled the real ADV compile-command entries with `-fstack-usage` and
otherwise unchanged flags. WebFS static frames: foreground entry 176 B,
heap reporter 208 B, Wi-Fi start 416 B, HTTP start 160 B, URI handler 48 B,
error formatter 160 B, directory streaming 400 B, file streaming 80 B,
socket callbacks and JSON/path helpers 32–48 B. These are individual compiler
frames, not measured complete call-chain/high-water values.

Explicit dependencies: `esp_wifi`, `esp_netif`, `esp_event`, and
`esp_http_server`. Defaults enable SoftAP and DHCP server, retain the 1024-byte
HTTP request-header limit, and raise the URI limit from 512 to 1552 bytes.
Compile guards reject missing AP/DHCP support or a stale URI capacity. The first
build correctly rejected the old generated `sdkconfig` URI limit; it was updated
to match `sdkconfig.defaults` before the successful build. No Wi-Fi buffer/IRAM
performance tuning or foreground stack increase was made. Wi-Fi NVS is disabled
in the initialization struct and storage is set to RAM.

Expected dynamic owners while active: the one MiniShell-tracked context, HTTP
server task/control sockets/session/parser allocations, Wi-Fi driver/task and
bounded RX/TX buffers, AP netif/DHCP, default event loop, and normal MiniShell
FS/backend handle/LFN allocations. There are no per-request large WebFS buffers.

**SDK lifecycle limitation:** ESP-IDF 5.5's `esp_netif_deinit()` returns
`ESP_ERR_NOT_SUPPORTED`; lwIP and its shared TCP/IP task are initialized once on
first use and remain for the boot. This is shared stack infrastructure, not an
active AP, HTTP server, or background WebFS service. Compare the first-launch
cost separately from subsequent warm start/stop cycles. No runtime heap delta
or leak-free hardware result is claimed. The command logs free/largest heap,
boot-lifetime minimum free heap, and foreground stack high-water before, during,
and after each session, with a 100 ms idle-cleanup allowance after stop.

SDK lifecycle/entropy semantics were checked against the installed sources
(`esp_netif_lwip.c`, `httpd_main.c`, `httpd_parse.c`, `wifi_default.c`) and the
[Espressif Wi-Fi guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-guides/wifi.html)
and [RNG guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/system/random.html).

### Local tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux -R adv_webfs_unit --output-on-failure
# PASS focused WebFS test.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS 66/66, including WebFS; no retries required on final run.

cmake -S tests/unit -B /tmp/T033-build-unit
cmake --build /tmp/T033-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T033-build-unit --output-on-failure
# PASS 15/15.

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
# All PASS.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS real ADV build and partition-size checks.
# Existing SDK/C++ pedantic warnings remain non-fatal.

cc -std=c11 -g -O1 -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -Iinclude -Iplatform/adv \
  tests/adv_webfs_test.c platform/adv/adv_webfs_logic.c \
  -o /tmp/T033-webfs-sanitize
ASAN_OPTIONS=detect_leaks=0 /tmp/T033-webfs-sanitize
# PASS address/undefined-behavior checks; not a lifecycle leak test.
node --check /tmp/T033-page.js
# PASS syntax check of JavaScript extracted from the embedded page.

git diff --check
# PASS.
```

Focused tests cover both roots/nested paths, encoded slash/traversal, malformed
escapes, NUL/control/backslash rejection, UTF-8 preservation, literal double
encoding, query ambiguity, component/path/query capacity boundaries, and JSON
quote/backslash/control escaping. Fake MiniShell FS tests exercise an exact
small JSON listing, empty directory, 10000-entry streamed listing, zero-length
file, and a byte-exact 10001-byte download using seven-byte partial reads.
Every FS operation and sink/cancellation point in the representative directory
and download cases is failed in turn, asserting one close per acquired handle.
Invalid paths reach no FS calls. No fake HTTP stack was introduced.

### Manual/hardware validation still required

All architect steps above remain pending. No flashing, AP/client connection,
real browser interaction, USB/QMX coexistence, or measured runtime heap evidence
was available locally. In particular verify three or more warm start/stop cycles,
SSID/password changes, one-client limit, SD absent, nested/UTF-8 names, byte-exact
small/large downloads, Q/Esc during an active download and slow/incomplete HTTP
request, and usable shell/storage afterward with cables unchanged. Confirm
normal ADV application operation after exit given the linked static-memory cost
and first-use TCP/IP allocation. Measure HTTP task stack high-water if available.

### Known limitations / risks

The first-use shared TCP/IP allocation is intentionally retained because the SDK
cannot deinitialize it. Runtime headroom, monotonic heap loss on subsequent cycles,
actual stop latency, RF entropy startup, and mobile-browser behavior require ADV
evidence. Partial I/O failures after HTTP output starts terminate the stream;
the browser must not treat the incomplete response as success. Native browser
download UI handles interrupted downloads; there is no transfer queue/resume UI.
The browser stores/sorts listing facts locally; ADV retains only one entry.
Persistent SDK stop failures hold the utility in logged cleanup retries instead
of releasing live state. Existing untracked Python cache directories are untouched
and excluded from the commit. No feature-scope or ownership deviation.

### Commit

One implementation commit on `codex/T033-adv-webfs-readonly`, based on
`f0d759600c8d277765546a6270180d813f0354b3`. The commit containing these notes is
the implementation reference; the exact pushed SHA is returned in the handoff.
Task set to **REVIEW**. No PR or GitHub Actions wait.

## Supervisor review

Reviewed implementation commit:

```text
ec027f5fd7c102b288e0259fd9c9a5832409d1e4
```

Result: **CHANGES REQUESTED — reduce permanent Wi-Fi IRAM cost before hardware testing.**

The implementation is otherwise structurally sound:

- WebFS remains ADV-only and foreground;
- all file/directory/space operations go through the MiniShell Filesystem API;
- no FATFS/VFS content bypass;
- no USB MSC handoff, USB-console suspension, or QMX USB-host ownership change;
- bounded path/query/transfer buffers;
- traversal and malformed encoding are rejected;
- directory/file data are streamed rather than whole-object cached;
- HTTP handlers are synchronous and stopped before app-owned context is freed;
- no public MiniShell API/version change;
- Linux 66/66, portable 15/15, architecture checks, ADV build and diff check pass.

The blocking concern is the measured permanent internal-SRAM delta:

```text
Data + BSS     +20,872 B
IRAM text      +27,072 B
total SRAM     +48,008 B
```

This cost exists even when `webfs` is not running because the networking
components are linked into the ADV image. That is too large to accept unchanged
without first applying the obvious low-throughput WebFS tradeoff.

T033 is a one-client local file manager, not a Wi-Fi throughput benchmark. Update
`platform/adv/sdkconfig.defaults` to disable the ESP-IDF Wi-Fi IRAM throughput
optimizations:

```text
# CONFIG_ESP_WIFI_IRAM_OPT is not set
# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set
```

If the resolved ESP-IDF configuration exposes an additional Wi-Fi/LwIP IRAM
optimization that is enabled by default, measure it but do not broaden the
change unnecessarily. `CONFIG_LWIP_IRAM_OPTIMIZATION` is normally disabled by
default and should remain disabled.

Then regenerate/rebuild the real ADV configuration and report the new exact
`.iram0.text`, data/BSS and total internal-SRAM deltas. WebFS throughput may
decrease; that is acceptable so long as normal directory browsing and file
download remain practical.

Do **not** change the 16 KiB foreground stack, HTTP 6 KiB stack, WebFS feature
scope, MiniShell API, or FT8 memory profile as part of this amendment.

Also tighten cleanup evidence while touching the lifecycle: do not silently
discard a failure from `esp_event_loop_delete_default()`. Either handle/retry it
without returning with an owned default loop, or retain explicit ownership state
so the next start can recover deterministically.

After the amendment, rerun all T033 gates and return a new commit SHA. Hardware
validation remains pending until supervisor re-review.

No other blocking finding was identified.

No PR is required.

## Codex supervisor amendment response

### Implementation summary / files changed

Amended reviewed branch head `eeec2cdfdc2dfda18d6bdc32aa178925727a3b74`.
Only the following tracked files change:

- `platform/adv/sdkconfig.defaults`: disable `CONFIG_ESP_WIFI_IRAM_OPT` and
  `CONFIG_ESP_WIFI_RX_IRAM_OPT` to trade Wi-Fi throughput for permanent SRAM.
- `platform/adv/adv_webfs_wifi.c`: check default event-loop deletion and retain
  ownership while logging/retrying failures at 100 ms intervals. Clear ownership
  only on success or a logged `ESP_ERR_INVALID_STATE`, which the installed IDF
  implementation returns when the default loop is already absent.
- This task packet: amendment evidence. Status remains **REVIEW**.

`CONFIG_LWIP_IRAM_OPTIMIZATION` and `CONFIG_LWIP_EXTRA_IRAM_OPTIMIZATION` remain
disabled. Wi-Fi EXTRA/SLP IRAM options were already disabled and remain so;
`CONFIG_ESP_PHY_IRAM_OPT` remains unchanged. No broader memory tuning was made.
No WebFS feature, 16 KiB foreground stack, 6 KiB HTTP stack, public API/version,
FT8 memory/profile, USB ownership, or filesystem architecture change.

### Exact firmware / static-memory evidence

First rebuilt the unmodified reviewed head and saved its ELF, BIN and generated
`sdkconfig`. Then updated the two Wi-Fi options and their generated ESP32
compatibility aliases in the local `sdkconfig` and ran a real IDF reconfigure
and build. The resulting configuration diff contains only those two options
and aliases; all other resolved settings are identical. The tracked defaults
also contain both disable directives for fresh configurations.

Measured the two ELF files with `xtensa-esp32s3-elf-size -A` and counted the BIN
bytes, using the same installed ESP-IDF `v5.5.4-dirty` toolchain:

| Measurement | Before amendment | After amendment | Change |
| --- | ---: | ---: | ---: |
| `.iram0.text` | 81975 B | 63959 B | -18016 B |
| `.iram0.text_end` alignment | 197 B | 37 B | -160 B |
| `.dram0.data` | 27000 B | 27000 B | 0 B |
| `.dram0.bss` | 38864 B | 38864 B | 0 B |
| Data + BSS | 65864 B | 65864 B | 0 B |
| Total static internal-SRAM increase vs pre-WebFS | +48008 B | +29832 B | **-18176 B** |
| Firmware BIN | 1368880 B (`0x14e330`) | 1368576 B (`0x14e200`) | -304 B |

The total SRAM reduction includes IRAM alignment and is independently confirmed
by `.dram0.heap_start` moving from address 1070237264 to 1070219088, exactly
18176 bytes earlier. Relative to the original pre-WebFS baseline recorded above,
the amended image has +9056 B `.iram0.text`, +7616 B data, +13256 B BSS, and
-96 B IRAM end alignment, totaling **+29832 B permanent internal SRAM**.
The firmware remains within its partition with 78% free. These are linked-image
measurements, not hardware heap or throughput results.

### Validation commands / results

```bash
# Before editing: preserve the actual reviewed-head build for comparison.
source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
cp platform/adv/build/minishell_adv.elf /tmp/T033-R1-before.elf
cp platform/adv/build/minishell_adv.bin /tmp/T033-R1-before.bin
cp platform/adv/sdkconfig /tmp/T033-R1-before.sdkconfig
# PASS.

# After updating the local generated configuration to match the new defaults:
idf.py -C platform/adv reconfigure build
# PASS real ADV build; partition-size checks pass.
xtensa-esp32s3-elf-size -A /tmp/T033-R1-before.elf \
  platform/adv/build/minishell_adv.elf
# Exact section measurements above; BIN sizes counted directly.

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS 66/66, no retries.
ctest --test-dir build-linux -R adv_webfs_unit --output-on-failure
# PASS 1/1.

cmake -S tests/unit -B /tmp/T033-build-unit
cmake --build /tmp/T033-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T033-build-unit --output-on-failure
# PASS 15/15.

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
# All PASS.

cc -std=c11 -g -O1 -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -Iinclude -Iplatform/adv \
  tests/adv_webfs_test.c platform/adv/adv_webfs_logic.c \
  -o /tmp/T033-R1-webfs-sanitize
ASAN_OPTIONS=detect_leaks=0 /tmp/T033-R1-webfs-sanitize
# PASS; not a hardware lifecycle/leak test.
node --check /tmp/T033-R1-page.js
# PASS syntax check of JavaScript extracted from the unchanged embedded page.

git diff --check
# PASS including this task update.
```

### Remaining validation / limitations / commit

No hardware testing or flashing was performed. All existing manual checks remain
pending supervisor re-review, including repeated startup/shutdown, warm-cycle
heap recovery, practical browsing/download throughput, and normal ADV application
operation after exit. Reduced WebFS throughput is the accepted configuration
tradeoff. The SDK's first-use TCP/IP retention remains unchanged. Persistent
unexpected event-loop deletion failures now hold the utility in logged cleanup
retries rather than silently losing ownership. The cleanup branch was reviewed
against the installed SDK return semantics and compiled in the real ADV build;
no hardware fault-injection result is claimed.

The commit containing this amendment is the new implementation reference on
`codex/T033-adv-webfs-readonly`; its exact pushed SHA is returned in the handoff.
Task remains **REVIEW**. No PR.

## Supervisor amendment re-review

Reviewed amendment commit:

```text
0c5b274a9e5e764f9c349e3a921abc87c310026c
```

Result: **PASS — ready for ADV hardware validation.**

The requested memory/lifecycle amendment is satisfied:

- `CONFIG_ESP_WIFI_IRAM_OPT` disabled;
- `CONFIG_ESP_WIFI_RX_IRAM_OPT` disabled;
- no broader Wi-Fi/LwIP/FT8 memory tuning;
- event-loop deletion is no longer silently ignored;
- all T033 software/build gates remain green.

Measured permanent internal-SRAM cost improves from **+48,008 B** to
**+29,832 B**, recovering **18,176 B**. Firmware is 1,368,576 B (`0x14e200`)
with 78% of the app partition still free.

The remaining permanent cost is accepted for hardware testing, not yet final
acceptance. T033 specifically needs real ADV evidence for:

- first-launch and warm-cycle heap/largest-block behavior;
- practical WebFS browsing/download throughput with Wi-Fi IRAM optimizations off;
- repeated start/stop without monotonic heap loss;
- shell/filesystem usability after exit;
- MiniFT8 launch and normal operation **after WebFS has run at least once in the
  same boot**, because ESP-IDF retains shared TCP/IP infrastructure after the
  WebFS Wi-Fi session is stopped;
- unchanged USB/QMX cable/ownership behavior.

No additional software change is required before that test.

## Architect hardware finding — first WebFS run

Hardware test on 2026-09-20 found two issues before T033 acceptance:

1. The generated 12-character mixed-class password is inconvenient on iPhone
   because entering it requires keyboard-mode switching. Architect decision:
   **use exactly 8 lowercase letters** for the ephemeral WPA2 session password.
2. The browser reaches WebFS and shows the `/flash` and `/sd` roots, but browsing
   `/flash` reports:

   ```text
   Cannot list /flash: Filesystem I/O error (-10)
   ```

   `-10` is `MINI_ERR_UNSUPPORTED`. Supervisor traced it to `webfs_list()`
   calling `fs->space()` before `dir_open()`. ADV intentionally configures
   `storage_bytes = 0` because `/flash` and `/sd` have independent physical
   capacities; current `filesystem_service.c::fs_space()` therefore returns
   `MINI_ERR_UNSUPPORTED` whenever no global quota exists.

Required correction:

- do not remove space reporting from WebFS;
- do not call FATFS/VFS directly from WebFS;
- add a **private backend physical-space hook** to `minishell_services_port_t`;
- `fs_space()` keeps current quota semantics when `storage_bytes != 0`;
- when `storage_bytes == 0`, `fs_space()` may delegate to the backend physical
  space hook after normal path normalization/stat validation;
- ADV implements per-volume physical total/free reporting for mounted `/flash`
  and `/sd` (nested paths resolve to their containing volume);
- unavailable `/sd` returns the appropriate not-found/not-ready result;
- Linux behavior with its configured MiniShell storage quota stays unchanged;
- public `mini_fs_api_t`, `MINISHELL_API_VERSION`, filesystem paths, and quota
  enforcement remain unchanged.

Add unit coverage for both modes: quota-backed `space()` and zero-quota backend
physical-space fallback. Add ADV-focused coverage for `/flash`, `/sd`, nested
paths, and unavailable volume as practical without hardware.

After fixing both findings, rerun the complete T033 software/build gates and
return to supervisor review before resuming hardware validation.

## Codex hardware-finding correction

### Implementation summary

Implemented only the two requested corrections from branch/main head
`63854d7478db697fa56711cc8282c82b6ede4977`.

The ephemeral password is now exactly eight lowercase ASCII letters, drawn from
`abcdefghjkmnpqrstuvwxyz` (no i/l/o, digits, or uppercase). The existing
RF-enabled `esp_fill_random` path still runs each session. Rejection sampling
removes modulo bias for the 23-letter alphabet while RF entropy remains enabled.
SSID generation still uses the same two bytes and formatting. This implements
the architect's explicit override of the original longer-password requirement.

A new optional **private** `minishell_services_port_t::fs_space` hook returns
physical total/free byte counts for a normalized, stat-validated path. Public
`mini_fs_api_t` and `MINISHELL_API_VERSION` are unchanged. When `storage_bytes`
is nonzero, the existing quota refresh, global used-byte accounting, and
saturated free-byte calculation run unchanged; the backend hook is not called.
With zero quota, the service validates the path and delegates to the hook,
computes used bytes as total minus free, and propagates backend failures.
An absent hook returns `MINI_ERR_UNSUPPORTED`; impossible free-greater-than-total
results return `MINI_ERR_IO` without unsigned underflow.

ADV registers the hook and uses ESP-IDF `esp_vfs_fat_info()` below the backend
boundary. Its existing mounted-volume path predicate is shared with a small
host-testable capacity dispatcher. Nested paths resolve to the containing
`/flash` or `/sd` mount root because the SDK query accepts a mount root, not an
arbitrary descendant. Unmounted volumes return `MINI_ERR_NOT_FOUND`; a missing
VFS FAT context returns `MINI_ERR_NOT_READY`; native errors retain errno-based
mapping, including `ENODEV` to `MINI_ERR_NOT_READY` for unavailable media.
Space values describe filesystem allocation capacity, not raw partition bytes.
WebFS listing and space reporting still use only MiniShell Filesystem; there is
no WebFS fallback, bypass, or removed space query.

### Files changed / invariants

- `core/minishell_services/minishell_services.h`: optional private backend hook.
- `core/minishell_services/filesystem_service.c`: zero-quota physical fallback.
- `platform/adv/adv_filesystem.c` and `adv_filesystem_space.h`: mounted-volume
  dispatch, ESP-IDF FAT capacity query, unavailable-device result mapping.
- `platform/adv/adv_webfs_logic.[ch]` and `adv_webfs_wifi.[ch]`: lowercase sample
  mapping and eight-character session credential generation/storage.
- `tests/unit/test_filesystem.c`: public Filesystem tests for both space modes.
- `tests/adv_filesystem_space_test.c`, `tests/adv_webfs_test.c`, root
  `CMakeLists.txt`: ADV volume dispatcher and password-sampling coverage.
- This task packet: correction and validation evidence.

No public API/version, Linux backend/quota policy, FT8 code/profile, USB ownership,
filesystem namespace/persistence, WebFS endpoints, stack configuration, or Wi-Fi
IRAM configuration changes. No T034 work or mutation endpoints. No whole-file or
whole-directory cache added. Existing untracked Python caches remain excluded.

### Validation evidence

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS 67/67; no retries.
ctest --test-dir build-linux \
  -R 'adv_(webfs|filesystem_space)_unit' --output-on-failure
# PASS 2/2, including focused WebFS.

cmake -S tests/unit -B /tmp/T033-build-unit
cmake --build /tmp/T033-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T033-build-unit --output-on-failure
# PASS 15/15, including expanded api_filesystem_unit.

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
# All PASS.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS real ADV build; BIN 1369376 B (0x14e520), 78% partition free.
# Existing SDK/C++ pedantic warnings remain non-fatal.

git diff --check
# PASS including task evidence.
```

Tests verify unchanged global quota totals across both roots, refresh after
changed file sizes, used bytes exceeding quota with free clamped to zero, and
that quota mode ignores the physical hook (including when the hook is absent).
Zero-quota tests exercise both volumes, normalized nested paths, 64-bit capacity
above 4 GiB, missing hooks, invalid arguments/paths, unavailable SD, stat errors
before dispatch, backend error propagation, and invalid capacity rejection.
ADV-focused tests exercise the actual shared volume dispatcher for `/flash`,
`/flash/ft8`, `/sd`, `/sd/foo/bar`, absent mounts, root-prefix impostors, and
backend errors. The FAT SDK call itself is compiled by the real ADV build;
physical-media behavior is not emulated by these host tests. Password tests
exhaust all 256 possible sample bytes, verifying only the selected lowercase
letters, equal frequency per accepted letter, and the rejection count.

### Manual validation / known limitations / commit

Hardware acceptance remains paused until supervisor re-review. No flashing or
new hardware test was performed. After re-review, confirm the eight-letter
password UX, real `/flash` and `/sd` listings/space figures, nested paths,
SD-absent behavior, downloads, and existing T033 lifecycle/heap/FT8-after-WebFS
checks. Physical free capacity can change with filesystem allocation; the SDK
reports the current FAT allocation view. The synthetic `/` root has no combined
physical-volume capacity. Existing first-use TCP/IP retention remains unchanged.

Task returned to **REVIEW**. The commit containing this section is the correction
reference on `codex/T033-adv-webfs-readonly`; its exact pushed SHA is returned in
the handoff. No PR and no hardware acceptance resumed.

## Supervisor hardware-fix re-review

Reviewed correction commit:

```text
e6ed8e5cc8b00a9c7111a81c7a15ad58292ea33e
```

Result: **PASS — resume ADV hardware validation.**

The two first-run findings are correctly addressed:

- session password is exactly eight lowercase letters, regenerated from the
  existing RF-enabled ESP32 RNG path;
- zero-quota `Filesystem.space()` now delegates to an optional private backend
  physical-space hook after normal normalization/stat validation;
- quota-backed Linux behavior remains unchanged and does not call the physical
  hook;
- ADV maps nested `/flash` and `/sd` paths to the containing FAT volume and
  obtains physical allocation capacity below the backend boundary;
- WebFS itself still uses only the public MiniShell Filesystem API;
- public `mini_fs_api_t` and `MINISHELL_API_VERSION` are unchanged;
- no T034 mutation work, USB ownership change, FT8 change, or Wi-Fi memory-policy
  change is included.

Validation evidence is sufficient: Linux **67/67**, portable **15/15**, focused
WebFS/ADV-space tests, architecture checks, real ADV build, and diff check all
pass.

One non-blocking note: `adv_fs_volume_path()` preserves the pre-existing ADV
root-prefix implementation pattern that may index at `path[strlen(root)]` for a
short non-matching string. T033's public Filesystem path reaches the backend only
after stat/path validation, and this pattern predates the WebFS correction, so it
is not a blocker for this hardware retest. It should be cleaned up separately.

Resume the same T033 hardware acceptance, beginning with the two failed points:

1. confirm the displayed password is eight lowercase letters and convenient to
   enter on iPhone;
2. browse `/flash` and confirm a real listing plus total/used/free space instead
   of `MINI_ERR_UNSUPPORTED`.

Then continue nested paths, `/sd`, downloads, repeated start/stop heap behavior,
and FT8-after-WebFS in the same boot.

## Architect hardware finding — FT8 fails before WebFS launch

On the T033 firmware, architect reports that `ft8` already fails on a fresh boot
**before `webfs` has been run**.

This rules out retained first-use TCP/IP/lwIP state as the primary cause of the
FT8 startup failure.

The active suspect is now the T033 firmware's **permanent linked internal-SRAM
cost**, currently measured at approximately **+29,832 B** versus the pre-WebFS
image. That cost exists from boot because Wi-Fi/network components are linked
into the resident ADV image even while WebFS is inactive.

T033 must not be accepted until the pre-WebFS FT8 baseline is restored on the
same firmware.

Required next evidence before changing architecture:

1. On a fresh boot of current T033 firmware, run `free` at the shell and record
   free bytes / largest block.
2. Run `ft8` immediately, without ever starting `webfs`.
3. Capture the exact FT8 failure text plus any ADV/UAC allocation diagnostics
   (especially engine-workspace or UAC-ring free/largest-block logs).
4. Compare against the last known-good pre-WebFS ADV firmware if needed.

If the failure is allocation/headroom-related, treat the permanent linked Wi-Fi
SRAM as the T033 blocker. Do not attribute it to WebFS runtime cleanup.

Do not start T034.

## Architect hardware finding — USB interrupt exhaustion

Fresh-boot T033 hardware evidence:

```text
free:
  heap    279.3K
  largest 224.0K

ft8:
  QMX startup/cleanup failed
  app: ft8 returned 12

debug UART:
  E (...) intr_alloc: No free interrupt inputs for USB interrupt (flags 0x802)
  E (...) HCD DWC: Interrupt alloc error: ESP_ERR_NOT_FOUND
  E (...) USB HOST: HCD install error: ESP_ERR_NOT_FOUND
```

This supersedes the earlier memory-pressure hypothesis. The heap/largest block is
healthy and the failure occurs specifically inside `usb_host_install()` because
ESP-IDF cannot allocate a suitable CPU interrupt input.

ESP-IDF documents this exact `ESP_ERR_NOT_FOUND` failure mode and recommends
`esp_intr_dump()` to identify Reserved/Used/Shared/Free interrupt inputs.

Required diagnostic amendment before any architectural fix:

1. Add a temporary/bounded ADV diagnostic immediately before
   `usb_host_install()` in the QMX/UAC prepare path:
   ```c
   esp_intr_dump(...)
   ```
   Send the dump to the existing debug UART/log path so it remains visible while
   USB Serial/JTAG is about to be handed off.
2. Keep the existing USB host interrupt flags unchanged for this diagnostic run.
3. Do not change task/core affinity, USB interrupt sharing, FT8 memory profile,
   WebFS feature scope, or Wi-Fi config yet.
4. Build/flash the T033 firmware and capture the full interrupt table from a
   fresh boot followed by `ft8`, before ever running `webfs`.
5. For comparison, if practical build/flash the last known-good pre-WebFS ADV
   commit and capture the same dump before `usb_host_install()`.

The comparison should identify exactly which low/medium interrupt input(s) became
reserved/used between the known-good and T033 images.

Possible follow-up directions, to choose only after the dump:

- move USB Host install/uninstall to a task pinned to the other core;
- free/defer an unnecessary resident driver;
- use a supported shared/low-medium interrupt policy if the USB HCD permits it;
- remove a compile-time feature that reserves an interrupt unnecessarily.

Do not guess among these before the interrupt table is available.

## Codex interrupt-diagnostic amendment

### Implementation summary / files changed

Based on `602aaf54979aa5d7e2e0be3a81a1715856bee794`, added only the requested
pre-install allocator diagnostic:

- `platform/adv/adv_audio_uac.cpp`: call the diagnostic immediately before the
  existing `usb_host_install(&host)` in `prepare()`, after the existing debug
  UART handoff and FIFO configuration.
- `platform/adv/adv_console.c`: `esp_intr_dump()` writes to a temporary unbuffered
  `funopen` stream whose write callback forwards all text through the existing
  `adv_console_debug_write()` path. A 128-byte stack scratch buffer splits writes
  without truncating the table; there is no whole-table allocation or row filter.
  The FILE object is closed after the dump. Allocation/output failures are
  reported, and diagnostics do not replace the existing USB startup result.
- `platform/adv/adv_internal.h`: private diagnostic helper declaration.
- This task packet: implementation, software validation, and capture status.

The helper holds the existing recursive output lock to prevent log interleaving
within the table. It marks the dump start/end and waits up to one second for the
existing debug UART TX queue to drain before USB installation. This prevents a
quick failed-startup cleanup from relying solely on the normal shorter UART
teardown drain. UART remains UART0 TX GPIO3 / RX GPIO6 at 115200 baud; no new
UART driver or interrupt is allocated by the diagnostic.

`esp_intr_dump()` supplies the full table for both CPUs plus its available/shared
interrupt summaries. It uses `fprintf`, so merely routing ESP log output to UART
would not redirect this dump; the private FILE adapter is necessary while USB
Serial/JTAG is suspended.

### Invariants preserved

The existing `ESP_INTR_FLAG_LEVEL1` assignment and FIFO values are untouched.
No interrupt sharing, task/core affinity, FT8 memory/profile, WebFS behavior,
Wi-Fi configuration, or public API change. No permanent interrupt/core fix.
Only diagnostic output/timing and the short-lived stdio stream are added.
T033 remains **IMPLEMENTING — USB INTERRUPT REGRESSION**.

### Software/build validation

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS 67/67; no retries.
ctest --test-dir build-linux \
  -R 'adv_(webfs|filesystem_space)_unit' --output-on-failure
# PASS 2/2.

cmake -S tests/unit -B /tmp/T033-build-unit
cmake --build /tmp/T033-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T033-build-unit --output-on-failure
# PASS 15/15.

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
# All PASS.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS real ADV build, including the newlib funopen adapter and esp_intr_dump.
# BIN 0x14edb0 bytes; 78% of application partition free.

git diff --check
# PASS including this evidence update.
```

### Hardware evidence / remaining capture

The architect's supplied fresh-boot evidence above remains the only hardware
result: heap 279.3K, largest 224.0K, FT8 return 12, and USB interrupt allocation
failure with flags `0x802`. No new interrupt table was captured in this workspace.
There is no `/dev/serial/by-id`, `/dev/ttyUSB*`, or `/dev/ttyACM*` device exposed,
so flashing and debug-UART capture could not be performed here. No comparative
pre-WebFS capture or unverified claim about a changed interrupt input is made.

Required next captures, before choosing any permanent fix:

1. Flash this diagnostic T033 build, capture UART0 TX GPIO3 at 115200, fresh boot,
   then run `ft8` without running `webfs`. Save everything from
   `ADV: interrupt allocator before usb_host_install` through
   `ADV: interrupt dump complete`, including both CPU tables and the subsequent
   USB allocation error. A failed/timeout diagnostic marker means the capture
   needs investigation rather than being treated as a complete table.
2. If practical, apply the same three-file diagnostic change to the
   architect-confirmed last known-good pre-WebFS firmware. Use the same wiring,
   fresh-boot sequence, and insertion point. Capture the complete table there.
3. Compare each CPU/input's level, type, and Free/Reserved/Used/Shared status,
   especially low/medium level-triggered inputs, to identify the composition
   difference. This comparison is **pending**, not inferred from heap figures.

### Known limitations / commit

The diagnostic does not fix the USB startup regression. Its bounded UART drain
adds startup latency, and hardware must verify delivery and capture both tables.
Do not resume T033 acceptance or implement an interrupt/core policy change based
on this software-only evidence. Existing untracked Python caches are untouched.

The commit containing this section is the diagnostic reference on
`codex/T033-adv-webfs-readonly`; its exact pushed SHA is returned in the handoff.
No PR. Task remains IMPLEMENTING pending the table comparison.

## Supervisor interrupt-diagnostic review

Reviewed diagnostic commit:

```text
397fcec65c424d18d9fc439db9690e61037f1aaa
```

Result: **PASS for hardware diagnostic capture.**

The change is instrumentation-only:

- full `esp_intr_dump()` occurs immediately before the unchanged
  `usb_host_install()`;
- dump is routed through the existing UART0/GPIO3 debug path;
- existing `ESP_INTR_FLAG_LEVEL1`, task/core affinity, USB FIFO geometry,
  FT8 profile, WebFS/Wi-Fi configuration, and public APIs are unchanged;
- software/build gates remain green.

This is not a functional fix and T033 remains IMPLEMENTING. The next required
evidence is the fresh-boot interrupt table on the failing T033 composition.

For reference, the last canonical pre-WebFS baseline is commit:

```text
6e6af91b22754d156602df4c6d54050745474dc2
```

If a comparison build is needed, apply only the same interrupt-dump instrumentation
to that baseline; do not mix any WebFS code into the comparison.

## Architect test result

Pending.
