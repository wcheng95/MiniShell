# T036 — ADV MiniFT8 mirrored web front panel

Status: READY

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

## Architect test result

Pending.
