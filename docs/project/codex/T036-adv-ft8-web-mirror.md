# T036 — ADV MiniFT8 mirrored web front panel

Status: TESTING — MAKE/BREAK

## Architect intent

Implement the MiniFT8 iPhone UI in **one bounded make-or-break task**.

The web page is not a second MiniFT8 UI and does not expose MiniFT8 internals.
It is a remote copy of the existing Cardputer ADV front panel:

- exactly the same 20 x 7 text display;
- same inverse-text attributes;
- remote keys become the same logical key events as the physical ADV keyboard;
- operator may use the ADV keyboard/display and the web page interchangeably.

WebFS remains specifically the file manager. T036 must not merge the WebFS file
manager into MiniFT8.

If concurrent Wi-Fi/HTTP + live QMX/FT8 cannot run reliably within ADV resources,
record the failure and stop. Do not redesign into a larger alternate architecture
inside this task.

## Objective

On ADV only:

```text
M$> ft8
     |
     +-- start MiniFT8 web mirror
     |      stable SoftAP credentials from T035
     |      http://192.168.4.1/
     |
     +-- run normal MiniFT8
     |      ADV LCD <------ same 20x7 state ------> iPhone page
     |      ADV keys ------ same input stream <---- iPhone keys
     |
     `-- when FT8 exits, stop HTTP/Wi-Fi and return to shell
```

Portable MiniFT8 source and public MiniShell APIs remain unchanged.

## Baseline

```text
main = 08fe0a427b9d895a1c1e609955586f82ab8ec114
```

Accepted existing behavior:

- T033 WebFS read-only file manager COMPLETE.
- T034 WebFS mutation file manager COMPLETE.
- T035 persistent SoftAP credentials COMPLETE.
- WebFS remains command-scoped and foreground-only.
- ADV MiniFT8/QMX RX is hardware validated.
- USB Host install/event/uninstall lifetime is owned by a CPU1-pinned task.
- ADV app task is CPU0, 16 KiB stack.
- ADV display already owns a 20 x 7 character buffer plus per-cell attributes.
- MiniShell Input exposes the same `mini_key_event_t` model used by the physical keyboard.

## Non-negotiable architecture

### WebFS stays separate

Do not add file-manager routes to the MiniFT8 mirror.

WebFS remains:

```text
M$> webfs
    -> /          file-manager page
    -> /api/list
    -> /api/file
    -> /api/dir
    -> /api/rename
```

MiniFT8 mirror is a separate ADV-private service used only while the compiled-in
ADV `ft8` app is running:

```text
M$> ft8
    -> /           mirrored front panel only
    -> /api/screen
    -> /api/key
```

The two services never run concurrently because MiniShell runs one foreground app.

It is acceptable to reuse T035's validated SoftAP/settings helper code internally.
Do not start the WebFS HTTP/file-manager server from FT8.

### MiniFT8 portability

Do not add ESP-IDF, Wi-Fi, HTTP, ADV-private or WebFS calls under `apps/ft8/**`.

Do not add a public Network/Web API.

The portable MiniFT8 application remains unaware that the web mirror exists.

ADV platform code owns the mirror lifetime around the compiled-in FT8 entry.

A direct ADV launcher wrapper/special case for the built-in `ft8` entry is
acceptable and preferred over contaminating portable MiniFT8.

External/ELF apps are not mirrored in T036.

### Display mirror boundary

The web page mirrors the **ADV display provider**, not MiniFT8 state.

Current display geometry:

```text
20 columns x 7 rows
140 character cells
140 attribute cells
MINI_TEXT_ATTR_INVERSE supported
```

Add a private ADV snapshot interface.

Required semantics:

- browser sees the last **presented** physical display state;
- a snapshot must never contain a half-updated frame;
- characters and inverse attributes must correspond to the same generation;
- snapshot copying must be safe against the HTTP task reading while the app task
  updates/presents the display;
- no public Display API change.

A small second "presented" shadow (140 chars + 140 attrs + generation/lock) is
acceptable. Prefer correctness over trying to save a few hundred bytes.

The physical LCD remains authoritative and unchanged.

### Remote input boundary

Do not let the HTTP task modify MiniShell's internal portable input queue directly.

Add a small ADV-private, thread-safe remote-key queue active only while the FT8
mirror is running.

```text
ADV physical keyboard ----+
                          +--> adv_input_wait --> MiniShell Input --> MiniFT8
FT8 web remote-key queue -+
```

Required semantics:

- remote events use normal `mini_key_event_t` values;
- physical keyboard remains active at all times;
- remote and physical keys may be interleaved;
- remote queue is bounded;
- queue overflow returns an error to HTTP; never block FT8 indefinitely;
- `adv_input_flush()` also flushes remote pending keys;
- stopping the mirror disables/drains the remote queue before its storage is freed;
- no public Input API change.

Use a FreeRTOS queue or equivalently thread-safe ADV-private mechanism. Prefer
dynamic/session allocation so it does not add a large permanent buffer.

### Lifecycle

The mirror is optional infrastructure around FT8.

Start order on ADV:

1. prepare/start FT8 mirror SoftAP + HTTP;
2. then invoke the existing MiniFT8 entry normally.

If mirror startup fails, MiniFT8 must still be allowed to run locally. Log a
concise warning; do not make network availability a requirement for FT8.

Stop order:

1. MiniFT8 returns;
2. stop HTTP server completely;
3. disable/drain remote input;
4. stop/deinit Wi-Fi using the existing proven SoftAP lifecycle;
5. return to shell.

Remote Q/Esc is **not** a special HTTP stop command. It is simply the same key
event MiniFT8 normally receives. MiniFT8 decides whether that exits/back-navigates.
When MiniFT8 ultimately returns, the wrapper stops the mirror.

No remote shell command execution.

## SoftAP

Reuse T035 behavior:

```text
/flash/minishell/setting.txt
SSID=...
PW=...
```

Valid configured credentials should therefore cause the iPhone to reconnect to
the same remembered SoftAP when `ft8` starts.

Fallback remains the T035 generated credential pair.

No STA mode, NVS persistence, mDNS, captive portal, HTTPS or Internet routing.

Do not log the configured password.

Because the ADV FT8 screen must remain the real MiniFT8 screen, do **not** replace
it with a Wi-Fi connection screen when FT8 starts. Network connection information
may be emitted to the normal diagnostic/console path without exposing the password,
or omitted entirely; the known stable SSID/PW is already operator configuration.

## HTTP server

Create a separate small MiniFT8 mirror server, not `adv_webfs_http_start()`.

Only these routes are needed:

```text
GET /               self-contained mirror page
GET /api/screen     current 20x7 presented snapshot
PUT /api/key        submit one key event
GET /favicon.ico    optional 204
```

No file-manager endpoints.

No POST, OPTIONS, CORS, WebSocket, SSE or external assets.

Use at most two sockets.

Use a smaller HTTP task stack than WebFS if real build/stack evidence supports it;
otherwise reuse 6 KiB. Do not increase any existing stack.

Keep HTTP priority low enough that screen polling cannot interfere with UAC or
FT8 decode work.

## Screen protocol

Keep it intentionally simple.

A fixed binary `/api/screen` response is preferred:

```text
uint32_le generation
uint8_t   cells[140]
uint8_t   attrs[140]
```

Total payload: 284 bytes.

Alternative equally bounded fixed representation is acceptable, but do not add a
general JSON/document model merely for 140 cells.

Browser polls at a modest fixed rate: **4 Hz** (250 ms) by default.

No WebSocket for T036.

Browser must render:

- exactly 20 columns x 7 rows;
- monospace fixed cells;
- spaces preserved;
- inverse attribute visually inverted;
- no interpretation of FT8-specific lines/fields.

### Browser input

Keep controls simple but sufficient for MiniFT8.

Required remote key support:

- printable ASCII character events;
- Escape;
- Enter;
- Backspace;
- Up / Down / Left / Right;
- Page Up / Page Down;
- Tab.

The page should support a normal browser/physical keyboard with `keydown`.

For iPhone touch use, provide:

- a small focusable text/key entry control for printable characters;
- explicit buttons for Esc, Enter, Backspace, arrows, Page Up/Page Down and Tab.

Do not build a MiniFT8-specific button/menu model. A letter typed remotely is the
same letter event as on ADV.

No key-repeat protocol is required. One HTTP request = one key event.

## HTTP key protocol

Use a small fixed/bounded request representation.

For example, query parameters:

```text
PUT /api/key?c=79&m=0
PUT /api/key?k=1&m=0
```

where `c` is a printable ASCII codepoint and `k` is a supported
`MINI_KEY_*` value. Exact encoding may differ if tests are cleaner.

Validate strictly:

- exactly one of char or special key;
- printable ASCII only for T036 char events;
- only explicitly supported special keys;
- known modifier bits only;
- bounded query length;
- reject duplicates/unknown fields.

No arbitrary structure-size/event injection from the browser.

## Concurrency / CPU

Do not move the FT8 foreground app from CPU0.

Do not change CPU1 USB Host ownership, LEVEL1 interrupt policy or FIFO 91/18/91.

Do not alter FT8 engine profile, UAC ring, decode scheduling or audio behavior to
"make room" for the mirror.

The purpose of T036 is to learn whether the already-selected MiniFT8 architecture
can coexist with Wi-Fi/HTTP.

If it cannot, that is a legitimate **BREAK** result.

## Resource budget / make-or-break gate

The mirror-specific state should be small:

- ~280 B presented screen shadow;
- generation/lock;
- bounded remote-key queue;
- small HTTP context;
- embedded page in flash.

The expensive dynamic cost is ESP-IDF Wi-Fi/lwIP/HTTP. That is the actual test.

Record before/after real ADV build:

- firmware size;
- .dram0.data;
- .dram0.bss;
- .iram0.text;
- permanent static SRAM delta;
- mirror HTTP context size;
- remote key queue allocation;
- HTTP stack;
- any display shadow static bytes.

At runtime record:

- heap free / largest before mirror startup if available;
- after mirror HTTP/Wi-Fi active;
- after FT8/QMX UAC is active;
- MiniFT8 Memory screen while RX is active, if still available;
- minimum heap;
- mirror HTTP task stack high-water if practical.

Do not weaken the accepted FT8 profile to satisfy this test.

## Acceptance criteria

### Functional mirror

- [ ] ADV `ft8` starts normal MiniFT8 plus mirror SoftAP/HTTP.
- [ ] iPhone reconnects using the same T035 SSID/PW.
- [ ] Browser shows the same 20 x 7 characters as ADV LCD.
- [ ] Inverse cells match the ADV LCD selection/highlight.
- [ ] Screen updates appear within approximately 250-500 ms.
- [ ] No half-rendered/mixed display snapshots are observable.
- [ ] Physical keyboard remains fully functional while browser is connected.
- [ ] Printable browser keys drive the same MiniFT8 behavior as ADV keys.
- [ ] Esc/Enter/Backspace/arrows/PageUp/PageDown/Tab work remotely.
- [ ] Alternating physical and remote keys behaves normally.
- [ ] Q/Esc semantics remain MiniFT8-owned, not HTTP-owned.
- [ ] When FT8 exits, mirror HTTP/Wi-Fi shuts down and shell returns normally.
- [ ] Relaunching FT8 starts the mirror again with the same configured credentials.
- [ ] WebFS remains a separate file-manager command and still works after FT8 exit.

### Architecture

- [ ] No changes under `apps/ft8/**` are required for the web mirror.
- [ ] No public MiniShell API/version change.
- [ ] No MiniFT8-specific data model/API in HTTP.
- [ ] No file-manager API in the FT8 mirror.
- [ ] Remote input reaches MiniFT8 through the same Input path as physical keys.
- [ ] Display data comes from ADV Display presented state, not FT8 internals.
- [ ] CPU1 USB Host ownership remains unchanged.
- [ ] T033-T035 WebFS behavior remains unchanged.

### Make-or-break runtime gate

With iPhone connected and actively polling the mirror at 4 Hz:

- [ ] QMX USB Host/UAC starts normally.
- [ ] FT8 RX starts normally.
- [ ] no allocation/startup regression;
- [ ] no UAC discontinuity/overflow attributable to web activity;
- [ ] no visible FT8 slot-clock stall attributable to web activity;
- [ ] live RX remains continuous for at least **10 consecutive FT8 slots**;
- [ ] normal decode completes while the web page remains open and polling;
- [ ] local and remote UI remain responsive during decode;
- [ ] FT8 can exit cleanly and release QMX USB Host;
- [ ] WebFS can subsequently start in the same boot.

If these gates fail due concurrent RAM/CPU/interrupt/resource pressure, mark T036
**BREAK / NOT ACCEPTED** with measured evidence. Do not change FT8 DSP profile,
USB ownership or build a second architecture in the same task.

## Expected implementation areas

Likely ADV-only additions/changes:

```text
platform/adv/adv_display.cpp
    last-presented snapshot + generation + private snapshot hook

platform/adv/adv_input.c
    merge bounded remote-key queue with physical keyboard polling

platform/adv/adv_ft8_web.c/.h
    FT8 mirror lifecycle
    tiny HTTP server
    /api/screen
    /api/key

platform/adv/adv_ft8_web_page.h
    self-contained 20x7 browser mirror + keyboard controls

platform/adv/adv_apps.c
    start/stop ADV FT8 mirror around built-in ft8 entry only
```

Reuse T035 SoftAP/settings implementation internally if that minimizes code/risk.
Do not start the WebFS file-manager HTTP server.

## Automated tests

Add host/source-boundary tests for:

### Display snapshot

- 20x7 geometry;
- chars copied exactly;
- inverse attrs copied exactly;
- generation changes only on presented frames;
- snapshot cannot observe mixed generations;
- console-mode immediate rendering is mirrored too.

### Remote input

- queue inactive outside mirror session;
- bounded queue;
- FIFO ordering;
- physical input still accepted;
- flush clears remote + physical pending state;
- stop disables/drains queue;
- overflow/error path;
- supported special-key validation.

### HTTP

- exactly mirror routes, no file routes;
- no POST/OPTIONS/CORS/WebSocket;
- fixed bounded screen response;
- strict key parsing;
- queue-full HTTP error;
- stopping flag/session teardown;
- two sockets maximum;
- page polls at 4 Hz;
- page renders 140 cells and inverse state;
- browser keydown mapping;
- touch special controls;
- embedded JS syntax check.

### Lifecycle / boundaries

- ADV ft8 wrapper starts mirror before FT8 entry;
- mirror start failure does not prevent FT8 entry;
- mirror always stops after FT8 returns;
- other apps are not mirrored;
- no `apps/ft8/**` change;
- CPU1 USB Host code unchanged;
- WebFS file-manager routes/lifecycle unchanged.

Run full existing gates:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T036-build-unit
cmake --build /tmp/T036-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T036-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

Rerun all `adv_webfs`, USB-owner/UAC and FT8 boundary regressions.

## Manual / hardware validation

One-shot acceptance run:

1. Boot ADV with QMX connected.
2. Run `ft8`.
3. Confirm normal MiniFT8 LCD appears; no replacement Wi-Fi splash screen.
4. Confirm configured SoftAP appears and iPhone reconnects automatically.
5. Open `http://192.168.4.1/`.
6. Compare all 20x7 rows with ADV LCD, including inverse selection.
7. Navigate several MiniFT8 screens using ADV keys while watching iPhone mirror.
8. Navigate back using iPhone keys while watching ADV LCD.
9. Alternate local/remote keys.
10. Keep the page open/polling for at least 10 FT8 slots with live QMX RX.
11. Record memory diagnostics and any UAC/slot timing warnings.
12. Confirm at least normal decode activity with page polling.
13. Exit FT8 using a normal remote Q/Esc path.
14. Confirm shell returns and mirror AP/HTTP stops.
15. Relaunch FT8; confirm automatic iPhone reconnect and mirror operation.
16. Exit FT8, run `webfs`; confirm T035/T034 file manager still works.
17. Run FT8 again in the same boot.

Acceptance is binary:

- **MAKE**: all functional + runtime gates pass -> T036 COMPLETE.
- **BREAK**: concurrent mirror materially breaks FT8/QMX/timing/resources -> record
  evidence and leave feature unaccepted. No architecture expansion in T036.

## Codex implementation notes

### Implementation summary

Implemented from `3fdd18c6bd5966a719533fdafcae166077a1c099` on
`codex/T036-adv-ft8-web-mirror`. Software/build gates pass. **Runtime MAKE/BREAK
acceptance is pending supervisor review and the prescribed hardware run.** No
profile, USB ownership, buffering, or alternate-architecture workaround was needed
for implementation or the real build.

The ADV launcher wraps only the built-in `minishell_app_ft8_main` entry. It starts
the mirror, calls the existing entry regardless of mirror-start success, then
stops HTTP completely, disables/frees remote input, and deinitializes the existing
SoftAP lifecycle. Partial startup failures are cleaned before local FT8 proceeds.
External ELF apps and every other built-in app retain their original dispatch.

The physical display provider publishes a 284-byte generation/character/attribute
shadow after each complete physical render, including console-mode immediate
renders. Writers and snapshot readers use a short cross-core critical section;
HTTP never reads the mutable working display buffers. The packet is encoded
explicitly as little-endian generation followed by 140 characters and 140 attrs.

A session-allocated 16-event ring is protected by a separate cross-core lock.
Only `adv_input_wait()` submits events into MiniShell Input; physical and remote
input alternate preference when both are pending. Flush clears both sources.
Queue detach occurs under the lock before freeing storage, so HTTP producers
cannot retain a dangling queue pointer. Full queues return HTTP 429; inactive
queues return 503. Q/Esc have no HTTP-owned shutdown meaning.

The separate server registers exactly `GET /`, `GET /api/screen`, and
`PUT /api/key`. Key queries contain one decimal `c` or `k` field plus decimal
`m`, in either order, within a 32-byte buffer. Duplicate/unknown fields, bodies,
non-ASCII characters, unsupported specials, and unknown modifier bits are rejected.
The self-contained page renders exactly 140 fixed monospace cells, applies inverse
attributes, and polls every 250 ms without overlapping screen requests. Browser
keydown, a mobile text input, and ten touch special-key buttons emit generic key
events. Client key submissions are serialized and bounded; errors are shown and
uncertain submissions are not retried.

T035 settings are read once before SoftAP startup and passed to the existing
validated-credential Wi-Fi helper. No connection splash replaces the FT8 display.
No password is logged. Generated fallback remains available, but its password is
not presented by this mirror; configure T035 credentials for practical phone use.

### Files changed

- `platform/adv/adv_apps.c`: built-in FT8-only lifetime wrapper dispatch.
- `platform/adv/adv_display.cpp`: last-presented snapshot and lock.
- `platform/adv/adv_input.c`: bounded session queue, merge, flush and teardown.
- `platform/adv/adv_ft8_web_io.h`: ADV-private geometry, snapshot and input hooks.
- `platform/adv/adv_ft8_web_logic.c`: strict key parsing and fixed screen encoding.
- `platform/adv/adv_ft8_web.[ch]`: optional lifecycle, separate HTTP server,
  stopping/socket guards and memory diagnostics.
- `platform/adv/adv_ft8_web_page.h`: generic mirrored display and browser input.
- `platform/adv/main/CMakeLists.txt`: ADV source registration.
- `tests/adv_ft8_web_io_test.py`: actual provider/input code with host hardware and
  lock stubs, including concurrent frame and queue-lifetime stress.
- `tests/adv_ft8_web_http_test.py`: actual server/lifecycle code with SDK failure
  injection, HTTP dispatch, key parser and boundary tests.
- `tests/adv_ft8_web_page_test.py`: embedded JS syntax and DOM/fetch behavior tests.
- `CMakeLists.txt`: three host regressions (browser test when Node is available).
- This packet: implementation/resource/test evidence and REVIEW status.

### Invariants preserved

Verified empty diff against the starting baseline for `apps/ft8/**`, public API,
`adv_audio_uac.cpp`, all existing WebFS production files, and Wi-Fi SDK defaults.
No FT8 state model, file-manager routes, POST/OPTIONS/CORS, WebSocket/SSE, STA,
mDNS, NVS persistence, or public Network API. The foreground task remains CPU0,
16 KiB; CPU1 USB Host ownership, LEVEL1, FIFO 91/18/91, FT8 DSP/memory profile,
UAC buffers and decode scheduling remain unchanged. Wi-Fi and HTTP remain scoped
to the foreground app; WebFS and this server never run concurrently.

### Memory / firmware evidence

Built and saved the exact starting baseline ELF/BIN before edits. Built the final
ESP32-S3 firmware with the installed ESP-IDF v5.5.4 toolchain. Measured sections
with `xtensa-esp32s3-elf-size -A` and BIN lengths with `wc -c`.

| Measurement (bytes) | Baseline | T036 | Delta |
| --- | ---: | ---: | ---: |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,000 | 27,016 | +16 |
| `.dram0.bss` | 38,864 | 39,152 | +288 |
| Firmware BIN | 1,375,552 | 1,382,288 | +6,736 |

**Permanent static internal-SRAM delta: +304 bytes**, including linker alignment.
DRAM heap-start moved from `1070219088` to `1070219392`, confirming the same delta.
IRAM vector/end padding and RTC sections are unchanged. Final firmware is
`0x151790`, with `0x49e870` bytes (78%) free in the `0x5f0000` application partition.

Real ADV compile-flag `sizeof` probes, inspected using
`xtensa-esp32s3-elf-nm -S`, establish:

| Object | Bytes | Lifetime/allocation |
| --- | ---: | --- |
| Presented snapshot | 284 | Permanent: 140 chars + 140 attrs + generation |
| Each `portMUX_TYPE` | 8 | Two permanent locks |
| Remote queue pointer / preference flag | 4 / 1 | Permanent; linker padding shared |
| `mirror_t` including Wi-Fi state | 120 | Foreground wrapper stack; HTTP borrows it |
| `webfs_wifi_t` | 108 | Included in `mirror_t`, unchanged from T035 |
| Remote queue | 328 | One session `heap_caps_calloc`, INTERNAL + 8BIT |
| HTTP task stack | 6,144 | Session SDK task, unchanged WebFS stack size |
| HTTP `handle_request` frame | 656 | Compiler stack-usage measurement |
| Mirror wrapper frame | 160 | Compiler stack-usage measurement |
| Mirror startup frame | 288 | Compiler stack-usage measurement |

The queue consists of 16 x 20-byte `mini_key_event_t` plus two 4-byte indices;
there is no separate queue task or RTOS queue metadata allocation. HTTP priority
is idle+1 (SDK default is idle+5), at most two sockets, three handlers, and two-
second send/receive timeouts. No task stack was increased.

Additional dynamic-allocation inventory from the installed SDK's actual
`httpd_create()`/handler registration paths and real compile-flag size probes:

- HTTP instance: 1,816 bytes; two socket database entries: 384 bytes.
- Eight response-header entries: 64 bytes; error-handler table: 52 bytes.
- Three URI pointers: 12 bytes; three handler objects: 48 bytes; URI strings: 23
  bytes. These listed HTTP control allocations total **2,399 requested bytes**.
- RTOS `StaticTask_t` size probe: 340 bytes, separate from the 6,144-byte stack.
- With queue, stack and TCB, these identified session allocations total **9,211
  bytes**, excluding allocator overhead, socket/lwIP/request allocations and
  Wi-Fi/netif/event-loop allocations. This is an inventory, **not a measured total
  live heap cost**.
- Wi-Fi configuration is unchanged: ten static RX buffers, 32 dynamic RX and 32
  dynamic TX limits; shared lwIP task stack remains 3,072 bytes. Shared lwIP is
  initialized on first use under the existing T035 lifecycle and is not deinitable.

**Runtime dynamic evidence: not collected (hardware testing explicitly deferred).**
No heap, UAC continuity, decode, timing or runtime high-water result is inferred
from the above allocation inventory. Instrumentation is ready for the acceptance
run:

- `ft8-web: before mirror`: internal free/largest/boot-minimum heap, foreground
  stack high-water before Wi-Fi/HTTP.
- `mirror active, before FT8/QMX`: same metrics after successful mirror startup.
- `screen poll (HTTP task)`: first poll and every 60 polls (about 15 seconds at
  4 Hz), internal free/largest/boot-minimum plus HTTP stack high-water. Capture
  these through live RX and decode to obtain the after-QMX-active evidence.
- `stopped`: post-HTTP/queue/Wi-Fi cleanup, after 100 ms for idle task cleanup.

ESP-IDF logs use the existing diagnostic path (USB console before host handoff,
UART0 GPIO3/6 while QMX owns USB); they do not write to the LCD. Capture the
pre-start transport as well as UART0 for all phases. Also record MiniFT8's existing
Memory screen during active RX. High-water samples are runtime diagnostics;
compiler frame sizes above do not establish stack margin.

### Local tests run

All final gates passed:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 76/76 passed, including all existing FT8 and WebFS tests

cmake -S tests/unit -B /tmp/T036-build-unit
cmake --build /tmp/T036-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T036-build-unit --output-on-failure
# 15/15 passed

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
# all passed

PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure \
  -R 'adv_(webfs|ft8|usb|uac|qmx)|ft8_(dependency|platform)'
# 16/16 passed: WebFS, mirror, USB-owner/console, UAC/QMX and FT8 ADV regressions

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# real ADV build passed; size evidence above

git diff --check
# passed
```

New tests compile production C/C++ against narrow host stubs. They cover unchanged
snapshot before present, exact chars/attrs/generation, console-mode publication,
10,000 concurrent frame publications, queue inactive/full/FIFO/flush/interleaving,
allocation failure, 1,000 producer-versus-stop/restart iterations, no remaining
queue allocations, startup failure at every Wi-Fi/queue/HTTP/registration stage,
local FT8 entry despite failure, cleanup ordering and stop retry, fixed 284-byte
responses, strict char/special/modifier queries, queue-full/inactive HTTP errors,
stopping socket behavior and method/route boundaries. Browser tests extract the
embedded JS, run `node --check`, and exercise all 140 cells/inverse flags, 250 ms
polling, truncated-frame rejection, physical/touch keys, ASCII-only input,
modifier mapping, bounded pending keys and error/no-retry behavior.

### Manual/hardware validation still required

No hardware testing or flashing performed. Supervisor review must precede the
packet's one-shot live-QMX/iPhone run: exact LCD match, local/remote interleaving,
10 consecutive FT8 RX slots with 4 Hz polling, normal decode, memory/stack/UAC/
timing evidence, remote normal exit, relaunch/reconnect, then WebFS and FT8 again
in the same boot. Software success does not establish MAKE.

### Known limitations / risks

- Concurrent Wi-Fi/HTTP plus QMX/UAC/FT8 dynamic heap, interrupt, CPU and timing
  behavior remains the central unmeasured risk. A resource-related hardware failure
  is BREAK / NOT ACCEPTED; no FT8-profile or USB redesign is authorized here.
- At most 16 remote keys are pending on-device; overflow is reported without
  blocking. Browser network failures can leave delivery uncertain, so the page
  does not retry keys automatically.
- Polling can be delayed by real scheduling/network conditions; 250 ms is the
  requested browser interval, not a measured update-latency guarantee.
- HTTP/Wi-Fi cleanup retains the proven retry-before-return policy; a persistent
  SDK cleanup failure will keep ownership rather than freeing live state.
- Fallback credentials are generated but intentionally do not replace the FT8
  display with a connection screen. Use configured T035 credentials for access.

### Commit

One implementation commit on `codex/T036-adv-ft8-web-mirror`, parent
`3fdd18c6bd5966a719533fdafcae166077a1c099`, titled
`Add ADV-only mirrored FT8 web front panel`.
Exact pushed SHA is returned in the handoff. No PR.

## Supervisor review

Review the complete implementation diff before hardware testing. In particular:

- no portable FT8 contamination;
- exact presented-screen snapshot semantics;
- thread-safe bounded remote input;
- FT8-local mirror lifetime;
- no WebFS file routes;
- no CPU1 USB/FT8 profile changes;
- no hidden password logging;
- resource evidence adequate for the make-or-break hardware run.

No PR required.

## Supervisor review — implementation

Reviewed implementation commit:

```text
7c34877053ae9cf97bedb664a3f94ac53da436cf
```

Result: **PASS — proceed to the T036 hardware MAKE/BREAK run.**

The implementation satisfies the one-shot architecture:

- portable `apps/ft8/**` is unchanged;
- only the built-in ADV FT8 entry is wrapped; other built-ins and ELF apps retain normal dispatch;
- mirror startup failure is non-fatal and local FT8 still runs;
- WebFS production files/routes remain unchanged and the mirror exposes no file-manager endpoints;
- the mirror HTTP surface is only `/`, `/api/screen`, and `/api/key`;
- no POST, OPTIONS, CORS, WebSocket/SSE, STA mode, NVS, mDNS, or public Network API was added;
- the browser receives a fixed 284-byte 20x7 presented-frame snapshot;
- HTTP never reads the mutable display working buffers;
- snapshot chars/attrs/generation are published and copied under a cross-core critical section;
- remote input uses a bounded 16-event ADV-private session queue;
- only `adv_input_wait()` forwards remote events into the normal MiniShell Input service;
- physical keyboard remains active and local/remote arbitration is bounded;
- remote queue detach happens under lock before its storage is freed;
- input flush clears both physical and remote pending state;
- Q/Esc remain ordinary MiniFT8 key events, not HTTP lifecycle commands;
- shutdown order is HTTP -> remote input -> Wi-Fi, after MiniFT8 returns;
- CPU0 FT8 affinity, CPU1 USB Host ownership, LEVEL1 policy, FIFO 91/18/91, UAC buffering and FT8 DSP profile are unchanged.

Resource evidence is appropriate for hardware testing:

- firmware: +6,736 B;
- permanent static SRAM: +304 B;
- presented mirror shadow: 284 B;
- remote queue: 328 B session heap;
- HTTP stack: 6,144 B session task;
- identified mirror HTTP/queue/task control allocations: ~9.2 KiB before allocator/socket/Wi-Fi overhead;
- no existing task stack increase.

All software/build gates pass: Linux 76/76, portable 15/15, focused 16/16, architecture checks, real ADV build and diff check.

No software conclusion is being drawn about runtime coexistence. Hardware validation is the deciding gate.
## Architect test result

Pending.
