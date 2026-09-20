# T033 — ADV WebFS read-only SoftAP proof

Status: READY

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
- generate a new cryptographically strong-enough local session password using
  ESP32 hardware RNG / ESP-IDF random source;
- password length at least 10 characters from an unambiguous printable set;
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

Use `fs->space()` for current-volume storage facts.

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

Supervisor reviews the actual `main..<commit>` diff against T033, with special
attention to:

- no filesystem bypass;
- Wi-Fi/HTTP teardown order;
- server handlers cannot outlive app lifecycle;
- bounded path/query/transfer buffers;
- traversal/encoding rejection;
- directory streaming rather than arbitrary cache;
- file streaming rather than whole-file buffering;
- no USB handoff/console suspension;
- no public API expansion;
- ADV memory/firmware cost.

No PR is required.

## Architect test result

Pending.
