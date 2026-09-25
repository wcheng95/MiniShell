# WebFS — browser file management for MiniShell ADV

Status: ARCHITECTURE DECIDED — phased implementation

## Purpose

WebFS makes normal MiniShell file operations convenient without changing USB
roles, exporting raw media, or requiring cable unplug/replug.

The target user flow is:

```text
M$> webfs
    -> Cardputer ADV starts a temporary Wi-Fi SoftAP
    -> phone/tablet/laptop connects
    -> browser manages /flash and /sd
    -> Q/Esc stops WebFS
M$>
```

`usbmsc` remains available as a recovery/large-transfer fallback, but WebFS is
the preferred normal file-management path.

## V1 architectural decisions

- WebFS V1 is **on-demand SoftAP mode**.
- WebFS is foreground/exclusive: no FT8 or other foreground app runs concurrently.
- It is an ADV-specific MiniShell system utility, analogous to `usbmsc`, not a
  new portable domain application.
- Linux does not get a fake WebFS implementation; normal host filesystem access
  already solves the same problem there.
- `/flash` and `/sd` remain mounted by MiniShell. WebFS never hands raw media
  to USB/TinyUSB.
- WebFS file operations go through the existing public MiniShell Filesystem API.
  It must not call FATFS/VFS file APIs directly.
- No public MiniShell API change and no `MINISHELL_API_VERSION` bump are needed.
- WebFS must not suspend the USB console, stop USB host, or use
  `adv_filesystem_handoff_*`.
- Wi-Fi and HTTP implementation are private ADV/ESP-IDF code.
- The browser UI is self-contained in firmware: no CDN, cloud service, external
  JavaScript, or Internet dependency.
- V1 uses HTTP over a private WPA2 SoftAP. TLS is not required for the local,
  session-only first release.

## SoftAP contract

Each WebFS launch creates an ephemeral local AP:

```text
SSID: MiniShell-XXXX
PASS: <8 uppercase letters>
URL : http://192.168.4.1/
```

The suffix and password are generated for the session and displayed on the ADV.
Credentials are not persisted.

V1 defaults:

- AP-only mode; no STA mode and no Internet/NAT routing.
- one Wi-Fi station at a time;
- exactly 8 uppercase letters for the session password, generated from ESP32 RNG;
- WPA2-PSK, never an open AP;
- deterministic WebFS address `192.168.4.1`;
- Q or Esc on ADV stops HTTP/Wi-Fi and returns to MiniShell.

A later task may add saved STA credentials, mDNS, captive-portal behavior, or an
optional always-on mode only after RAM/lifecycle measurements justify them.

T035 adds an earlier convenience step: stable **SoftAP** credentials from
`/flash/minishell/setting.txt`, so phones can remember the WebFS network without
changing WebFS to STA mode.

## File ownership

Because WebFS is the foreground MiniShell utility, its Filesystem handles are
normal application-scoped MiniShell handles:

```text
adv_webfs HTTP handler
    -> mini_api_get()->fs
    -> MiniShell Filesystem service
       path normalization
       handle ownership/cleanup
       quota/accounting
       backend dispatch
    -> ADV FATFS backend
       /flash
       /sd
```

The WebFS application must stop its HTTP server before returning so no handler can
outlive `minishell_services_app_end()`.

## Web UI principles

The browser performs presentation work; ADV streams facts and file bytes.

- no whole-directory cache;
- no whole-file download/upload buffer;
- bounded query/path buffers;
- streamed/chunked directory JSON;
- streamed file transfer;
- client-side sorting/display;
- clear unavailable-volume and I/O errors;
- only `/flash` and `/sd` are exposed.

WebFS path parsing must reject traversal and malformed encoding before calling
the Filesystem API. Decoded paths must be absolute and remain within exactly
`/flash`, `/sd`, or their descendants. Components `.` and `..` are rejected
rather than normalized across WebFS roots.

## Phase plan

### T033 — read-only WebFS proof

Retire the major platform risks first:

- repeated SoftAP start/stop;
- lightweight HTTP server lifecycle;
- self-contained browser page;
- browse `/flash` and `/sd`;
- report storage space;
- streamed downloads;
- SD-absent behavior;
- bounded memory;
- clean return to MiniShell with no USB-role changes.

### T034 — safe file mutations

After T033 hardware acceptance:

- streamed upload;
- temporary-file + sync + close + rename commit;
- overwrite existing regular files safely;
- mkdir;
- rename regular files;
- delete regular files;
- delete empty directories;
- interrupted/no-space/error behavior;
- long/UTF-8 filename tests.

No multipart parser is required: the browser sends upload data as the raw request
body.

### T035 — persistent WebFS SoftAP credentials

Use `/flash/minishell/setting.txt` to provide stable operator-chosen SoftAP
credentials:

```text
SSID=<stable AP name>
PW=<stable WPA2 passphrase>
```

If both values are valid, WebFS uses them unchanged on each launch so a phone can
remember the network. Missing/invalid settings fall back to the current generated
SSID/password pair. Settings are read at WebFS launch and changes take effect on
the next launch.

### T068 — inline configuration editor

Every regular file has explicit **Download**, **Rename**, and **Delete** actions.
Only the exact basenames `setting.txt` and `alias.txt`, at most 64 KiB, have
clickable filenames that open a plain textarea with the full path, Save and
Cancel. Other filenames, including oversized configuration files, are plain
text. Directory navigation and actions are unchanged.

The browser reads through `GET /api/file` with `X-WebFS-Read: 1` to omit attachment
headers; ordinary GETs still stream attachments for explicit Download. Save
uses the existing safe complete-replacement `PUT /api/file` path. Cancel writes
nothing. Read/save errors are visible, failed saves retain the edits, and a
successful save returns to the listing even if refreshing that listing fails.
The browser checks both downloaded size and UTF-8 save size against 64 KiB;
ADV retains its existing bounded streaming buffers and Filesystem ownership.

This is a UTF-8 configuration convenience editor using normal textarea newline
handling, not a byte-preserving binary editor or general text editor. Resident
WebFS credential edits take effect on the next WebFS launch as before.

Later convenience work remains separate:

- optional STA-mode profiles;
- mDNS such as `minishell.local`;
- captive-portal convenience;
- cross-volume copy/move;
- remote stop/timeout;
- evaluate always-on mode from measured RAM/CPU cost.

## Non-goals for V1

- Internet access or NAT router behavior;
- cloud dependency;
- HTTPS certificates;
- user accounts;
- arbitrary host paths;
- raw FAT media export;
- concurrent FT8 + WebFS operation;
- public Network/HTTP MiniShell APIs;
- replacing `usbmsc` as a recovery path.
