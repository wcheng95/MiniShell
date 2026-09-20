# T034 — ADV WebFS safe file mutations

Status: COMPLETE

## Architect intent

Extend the hardware-validated T033 WebFS from read-only browsing/download into a
practical browser file manager for normal MiniShell use, while preserving the
same cable-free, foreground SoftAP architecture.

T034 adds only safe filesystem mutations:

- upload / create regular files;
- replace an existing regular file without exposing a partially uploaded file;
- create directories;
- rename regular files within the same directory;
- delete regular files;
- delete empty directories.

The browser remains a convenience layer. MiniShell Filesystem remains the sole
owner of logical file semantics.

## Objective

From the existing `webfs` page, allow a phone/tablet/laptop to perform normal
`/flash` and `/sd` file maintenance without USB MSC handoff or cable changes.

The most important invariant is upload replacement:

```text
HTTP body
   -> new temporary file in same parent
   -> write all bytes
   -> fs.sync(temp)
   -> fs.close(temp)
   -> fs.rename(temp, final)    COMMIT
```

Before the final rename succeeds, an existing destination must remain untouched.

## Current context

Task creation baseline:

```text
main = 4c3449821fa0b8d747953c6e4da60ccad966fa30
```

T033 is COMPLETE and hardware validated:

- on-demand WPA2 SoftAP;
- eight-uppercase-letter ephemeral password;
- `/flash` and `/sd` browsing;
- per-volume physical capacity reporting on ADV;
- streamed downloads;
- clean Q/Esc shutdown;
- FT8/QMX works before and after WebFS in the same boot;
- USB Host install/event/uninstall lifetime is owned by one CPU1-pinned task.

Existing public Filesystem API v3 already supplies every T034 operation:

```text
open / write / sync / close
stat
rename
remove_file
mkdir
rmdir
```

Important existing semantics:

- a write handle owns its normalized path exclusively;
- `rename()` supports regular files only;
- ADV same-volume regular-file replace is implemented by
  `adv_rename_replace()`;
- destination replacement has rollback/backup handling for ordinary failures;
- that ADV helper explicitly documents that replacement is **not crash-atomic**;
- `rmdir()` removes only empty directories;
- directory rename is not part of the MiniShell API behavior.

Canonical WebFS architecture:

```text
docs/project/webfs.md
docs/project/codex/T033-adv-webfs-readonly.md
```

## Source of truth

- `include/minishell/api.h` — public Filesystem API v3.
- `core/minishell_services/filesystem_service.c` — path ownership, writer
  exclusion, quota behavior and regular-file rename policy.
- `platform/adv/adv_filesystem.c` — ADV FAT backends.
- `platform/adv/adv_filesystem_rename.h` — same-volume replace/rollback
  semantics and explicit non-crash-atomic limitation.
- `platform/adv/adv_webfs_*.{c,h}` — accepted T033 WebFS implementation.
- `docs/project/webfs.md` — phased product architecture.

## Architectural constraints

### Keep T033 ownership unchanged

WebFS remains:

- ADV-only;
- foreground/exclusive;
- on-demand SoftAP;
- one associated Wi-Fi station;
- self-contained local HTTP page;
- no Internet/cloud dependency.

Do not change CPU1 USB Host ownership, FT8 profile, Wi-Fi IRAM policy, SoftAP
credential policy, USB console ownership, or storage mount/handoff policy.

### Filesystem boundary

Every logical file mutation must use `mini_api_get()->fs`.

Do not call POSIX/FATFS/VFS file mutation APIs from WebFS.

No public MiniShell API change. Do not bump `MINISHELL_API_VERSION`.

### Mutation serialization

The ESP-IDF HTTP server remains synchronous on one HTTP task. T034 may rely on
that single handler execution context to serialize WebFS mutations; do not add a
second mutation worker or filesystem mutex merely for T034.

The browser may issue operations sequentially. It does not need parallel uploads.

### Mutation HTTP methods

Use non-simple mutation methods intentionally:

```text
GET     /api/list?path=...
GET     /api/file?path=...

PUT     /api/file?path=...           raw-body upload/create/replace
DELETE  /api/file?path=...           remove regular file

PUT     /api/dir?path=...            mkdir
DELETE  /api/dir?path=...            remove empty directory

PUT     /api/rename?from=...&to=...  rename/replace regular file
```

Keep `GET /` and favicon behavior.

Do **not** add form-style mutation POST endpoints. Do not add CORS or OPTIONS
support. PUT/DELETE are deliberate: ordinary cross-origin HTML forms cannot issue
these mutation methods, while the same-origin WebFS page can.

Raise `max_uri_handlers` only as required for the method/URI registrations;
keep socket count and HTTP stack unchanged unless measured evidence requires a
change.

### Path rules

Reuse the T033 decode-once validation boundary.

For every mutation:

- only `/flash` and `/sd` descendants are mutable;
- never mutate the volume roots themselves;
- reject `.`, `..`, backslash, controls, malformed percent encoding, ambiguous
  query parameters and overflow;
- preserve UTF-8 byte names supported by the existing FATFS configuration.

Rename API rules for T034:

- source must be a regular file;
- destination is a regular-file path;
- source and destination must have the **same parent directory**;
- same-name rename is a harmless no-op through existing Filesystem semantics;
- replacing an existing regular destination is allowed;
- destination directory returns the normal type error.

Same-parent restriction is intentional. Moving files between directories or
volumes belongs to T035.

Directory rename is not supported and must not be offered by the browser.

### Upload commit protocol

Uploads use the raw PUT request body. Do not add multipart/form-data parsing.

For target `/volume/dir/name`:

1. validate target;
2. reject volume root as a target;
3. if target exists and is a directory, fail without creating a temp file;
4. construct a bounded same-parent temporary regular-file path using a reserved
   WebFS prefix, for example:
   ```text
   .webfs-upload-XXXXXXXX.tmp
   ```
5. choose a random suffix using the existing ESP32 random source;
6. create the temp file with MiniShell `WRITE|CREATE|EXCL`;
7. on collision, try a bounded number of new random names;
8. stream exactly `req->content_len` bytes through the existing bounded transfer
   buffer;
9. handle partial MiniShell writes by looping until each received chunk has been
   fully written;
10. `fs->sync(temp)`;
11. `fs->close(temp)`;
12. commit with `fs->rename(temp, target)`;
13. only after rename success report upload success.

No whole-file buffer.

A zero-byte PUT creates/replaces with an empty regular file.

If the target parent is so long that no bounded temp filename fits, return
`MINI_ERR_NAME_TOO_LONG` without touching the destination.

The implementation must make no claim that FAT replacement survives arbitrary
power loss atomically; the accepted guarantee is against ordinary HTTP/network/
write/sync/close failures before commit.

### Upload failure cleanup

For failures before successful rename:

- close an acquired temp handle exactly once;
- best-effort remove the temporary file after the handle is closed;
- never truncate/remove the pre-existing destination;
- preserve the primary operation error for the HTTP response;
- a cleanup failure may leave the reserved-prefix temp file visible so the user
  can inspect/delete it; do not silently scan/delete old temp files at startup.

Q/Esc while an upload is active must cancel/terminate the HTTP transfer through
the existing stopping/socket mechanism, then execute the same temp cleanup path
before WebFS returns.

### Other mutations

`mkdir`
- path must be a descendant of one volume root;
- use `fs->mkdir()`;
- existing path propagates the Filesystem result.

`remove_file`
- use `fs->remove_file()`;
- never implement recursive behavior.

`rmdir`
- use `fs->rmdir()`;
- only empty directories succeed;
- non-empty directory must remain intact and return `MINI_ERR_NOT_EMPTY`.

`rename`
- use `fs->rename()`;
- regular files only;
- same parent only;
- replacement of an existing regular file uses existing MiniShell/ADV semantics.

## Browser UI

Keep the T033 page small and self-contained.

Add:

- upload chooser + Upload action for the current directory;
- multiple selected files may be uploaded **sequentially** if convenient; device
  code still handles one request at a time;
- New Directory button;
- Rename action for regular files only;
- Delete action for regular files;
- Delete action for directories, with empty-directory semantics;
- clear operation/error status;
- refresh current listing and space values after successful mutation.

Safety UX:

- confirm file deletion;
- confirm directory deletion;
- if the current listing already contains an upload destination, confirm overwrite;
- if rename destination already exists as a regular file, confirm replacement;
- do not offer directory rename;
- do not offer root deletion.

Use DOM `textContent` / element creation for filenames as in T033. Never inject
untrusted filename text with `innerHTML`.

T034 does not require a text editor.

## HTTP responses

Mutation success may return compact JSON such as:

```json
{"ok":true}
```

Update error mapping so mutation-specific failures are understandable:

- invalid/name-too-long -> 400;
- not found -> 404;
- access -> 403;
- exists -> 409;
- non-empty -> 409;
- wrong file/dir type -> 400;
- no space -> 507;
- not ready -> 503;
- too many open handles -> 503;
- unsupported -> 501;
- unexpected I/O -> 500.

Do not expose internal paths outside the validated namespace.

## Expected implementation shape

Likely modifications:

```text
platform/adv/adv_webfs_http.c
    register PUT/DELETE handlers
    receive raw upload body
    HTTP error/status mapping

platform/adv/adv_webfs_logic.[ch]
    bounded mutation path/query helpers
    temp path construction
    streamed upload transaction
    mkdir/rename/delete wrappers if useful for host testing

platform/adv/adv_webfs_page.h
    upload/mkdir/rename/delete UI

tests/adv_webfs_test.c
    mutation and fault-injection coverage
```

A separate small HTTP/source-boundary regression is acceptable if useful.

Do not put mutation policy into `adv_apps.c`, shell code, or the ADV FAT backend.

## Non-goals

Do not include in T034:

- directory rename;
- recursive delete;
- recursive upload;
- drag/drop requirement;
- text editor;
- station/settings editor;
- cross-directory move;
- cross-volume move/copy;
- archive/ZIP support;
- upload resume;
- concurrent uploads;
- background WebFS;
- STA mode;
- mDNS;
- captive portal;
- HTTPS;
- user accounts;
- remote shell/WebSocket terminal;
- public Network/HTTP API;
- changes to FT8/USB ownership;
- automatic deletion of stale temp files from previous power-loss events.

These remain T035 or later decisions.

## Acceptance criteria

- [ ] Existing T033 browse/download behavior remains unchanged.
- [ ] Browser can upload a new regular file to `/flash`.
- [ ] Browser can upload a new regular file to `/sd`.
- [ ] Browser can replace an existing regular file.
- [ ] Existing destination remains byte-for-byte unchanged until upload commit.
- [ ] Upload body is streamed through bounded memory; no whole-file allocation.
- [ ] File larger than the transfer buffer uploads correctly.
- [ ] Zero-byte file upload works.
- [ ] Partial FS writes are handled correctly.
- [ ] Receive abort/write failure/sync failure/close failure prevents commit.
- [ ] Pre-commit failure cleans the request temp file when cleanup succeeds.
- [ ] Pre-commit failure does not damage an existing destination.
- [ ] No-space failure leaves existing destination intact.
- [ ] Temp-name collision is handled with bounded retry.
- [ ] Browser can create a directory.
- [ ] Browser can rename a regular file within the same directory.
- [ ] Rename can replace an existing regular file after confirmation.
- [ ] Directory rename is not exposed/supported.
- [ ] Browser can delete a regular file after confirmation.
- [ ] Browser can remove an empty directory after confirmation.
- [ ] Non-empty directory delete fails without changing its contents.
- [ ] Volume roots cannot be mutated.
- [ ] Cross-directory/cross-volume rename through the WebFS endpoint is rejected.
- [ ] Long and UTF-8 names remain safe/bounded.
- [ ] Q/Esc during an active upload does not commit a partial replacement.
- [ ] Mutation errors map to useful HTTP status/messages.
- [ ] WebFS still exits cleanly to MiniShell.
- [ ] FT8/QMX still starts after WebFS in the same boot.
- [ ] No public API/version change.

## Automated tests

Extend the pure/fake-FS WebFS tests.

Required upload transaction coverage:

1. new file success;
2. replace existing file success;
3. zero-byte file;
4. body larger than transfer buffer;
5. partial `fs->write()` completions;
6. receive/cancel failure at each chunk boundary;
7. open/create failure;
8. temp collision followed by retry;
9. bounded collision exhaustion;
10. write failure;
11. no-space failure;
12. sync failure;
13. close failure;
14. rename failure;
15. cleanup remove failure;
16. destination directory rejection;
17. existing destination unchanged on every pre-commit failure;
18. exactly one close for each acquired handle.

Required namespace/mutation coverage:

- root mutation rejection;
- mkdir success/existing/failure;
- file delete success/type error;
- empty rmdir success;
- non-empty rmdir propagation;
- rename same parent success;
- rename replacement;
- same-name no-op;
- directory source rejection;
- cross-directory rejection;
- cross-volume rejection;
- UTF-8 names;
- maximum/overflow path and temp-name geometry;
- malformed/duplicate/unknown rename query parameters.

Keep/extend the T033 tests for path decoding, streaming listing/download and
password sampling.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T034-build-unit
cmake --build /tmp/T034-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T034-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

Also extract/check the embedded JavaScript syntax as T033 did.

Record:

- final firmware size;
- WebFS context/buffer size changes;
- HTTP handler-count change;
- any new static SRAM;
- any stack-size change (none expected).

## Manual / hardware validation

After supervisor review/merge, validate on Cardputer ADV.

### Basic mutation sequence

1. Fresh boot; run `webfs`.
2. Confirm T033 browsing/download still works.
3. Upload a small known text file to `/flash`; download/cat and compare.
4. Upload a file larger than 2048 bytes; verify exact size/content.
5. Upload an empty file.
6. Replace the small file with different known content; verify new content.
7. Create a directory, upload a file inside it, browse it.
8. Rename a regular file within that directory.
9. Delete the regular file.
10. Remove the now-empty directory.
11. Create a non-empty directory and confirm directory delete is rejected.
12. Repeat representative upload/rename/delete on `/sd` when present.

### Interrupted replacement

Create a destination with known content.

Start replacing it with a sufficiently large upload (SD is suitable for a
multi-MiB test) and press Q/Esc while transfer is active.

After returning to MiniShell:

- destination must still contain the old complete content;
- no partial destination is accepted;
- request temp should be absent if cleanup succeeded;
- if a cleanup fault leaves a temp file, it must have the reserved WebFS name and
  the original destination must still be intact.

### Integration smoke

After WebFS mutations and exit:

```text
M$> ft8
```

QMX/FT8 must start normally in the same boot.

## Codex implementation notes

### Implementation summary

Implemented on `codex/T034-webfs-mutations`, starting at
`02f6b3fe667a9cd1629ea4dee857e01ad0ebeb24`. No scope deviations.

- Added the five specified PUT/DELETE registrations (nine handlers total).
  Raw upload bodies are received synchronously in the existing 2048-byte buffer.
- Uploads create an exclusive same-parent `.webfs-upload-%08x.tmp` using
  `esp_random()`, with at most 16 candidates. They loop on partial writes, sync,
  close exactly once, then call Filesystem rename as the commit. A temp candidate
  matching the destination case-insensitively is skipped. No destination open,
  truncate, or remove occurs in the upload path.
- Every pre-commit error prevents rename. Cleanup attempts temp removal after
  close, preserves the primary error, and leaves any cleanup residue visible.
  Receive errors/timeouts and the existing stopping flag take this same path;
  cancellation is checked between partial writes, before sync, and after close.
- Added descendant-only mutation validation and strict decode-once two-parameter
  rename parsing. Same-parent policy is checked before calling Filesystem rename;
  MiniShell retains regular-file/type, writer-exclusion, and empty-directory policy.
- Browser supports sequential raw-file uploads, mkdir, regular-file rename and
  replacement, file delete and empty-directory delete. Confirmations precede
  deletes and replacement of destinations present in the listing. Filenames use
  DOM text nodes; successful operations refresh listing and capacity. No directory
  rename or root delete controls are offered.
- Added the specified mutation error statuses. Failed mutations close the HTTP
  session after the error response, so unread upload bytes cannot be reused as a
  subsequent request.

### Files changed

- `platform/adv/adv_webfs_logic.[ch]`: mutation validation/query parsing and
  streaming upload transaction through `mini_fs_api_t` only.
- `platform/adv/adv_webfs_http.c`: raw receive adapter, RNG adapter, PUT/DELETE
  registrations, error mapping, and stopping/error integration.
- `platform/adv/adv_webfs_page.h`: self-contained file-manager controls.
- `tests/adv_webfs_mutations_test.c`: upload fault injection and namespace tests.
- `tests/adv_webfs_http_test.py`: compiled production HTTP adapter with fake
  requests/Filesystem; registration, method and boundary checks.
- `tests/adv_webfs_page_test.py`: extracted JavaScript syntax and DOM/fetch tests.
- `CMakeLists.txt`: three additional tests (page test enabled when Node is found).
- This task packet: implementation and validation evidence; status REVIEW.

### Invariants preserved

All logical filesystem operations use the existing MiniShell Filesystem API.
No core/backend/API changes, no API version bump, and no `apps/ft8/**` changes.
T033 SoftAP/start-stop/credentials, eight-uppercase-letter password, Wi-Fi memory
configuration, foreground ownership, CPU1 USB Host lifetime, LEVEL1 policy,
91/18/91 FIFO, capture affinity and FT8 profile remain unchanged. No POST mutation,
OPTIONS/CORS, recursive delete, directory rename, cross-parent move, editing, or
T035 feature was added.

### Memory / firmware evidence

Measured real ESP32-S3 builds using the installed ESP-IDF v5.5.4 toolchain.
The baseline was built and its ELF/BIN copied before implementation. Section
sizes were read with `xtensa-esp32s3-elf-size -A`; BIN lengths with `wc -c`.

| Measurement (bytes) | Baseline | T034 | Delta |
| --- | ---: | ---: | ---: |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,000 | 27,000 | 0 |
| `.dram0.bss` | 38,864 | 38,864 | 0 |
| Firmware BIN | 1,369,664 | 1,374,432 | +4,768 |

Total static internal-SRAM delta: **0 bytes**. IRAM vectors/end padding and
DRAM heap-start address (`1070219088`) are unchanged; RTC sections also unchanged.
Final BIN is `0x14f8e0`, leaving `0x4a0720` bytes (78%) of the `0x5f0000` app
partition free.

Compiled `sizeof` probes using actual ADV compile flags and inspected the object
with `xtensa-esp32s3-elf-nm -S`:

- `webfs_buffers_t`: 4,608 bytes, unchanged; transfer buffer: 2,048 bytes.
- `webfs_http_t`: 4,616 bytes, unchanged; `webfs_wifi_t`: 32 bytes, unchanged.
- Handler count: 4 -> 9. SDK registrations are session allocations: five extra
  16-byte `httpd_uri_t` objects, 20 extra pointer-table bytes, and 50 URI-string
  bytes = 150 additional requested heap bytes, excluding allocator overhead.
  They are released by the existing HTTP stop lifecycle; no permanent buffer was
  added. Socket limit remains two.
- Foreground stack stays 16 KiB; HTTP stack stays 6 KiB. Compiler `-fstack-usage`
  reports 96 bytes for `webfs_upload`, 48 for `handle_mutation`, and 208 for
  `webfs_http_start`. These are individual frames, not measured runtime
  high-water marks; hardware stack/heap observations remain pending.

### Local tests run

All gates passed after implementation:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 71/71 passed
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R adv_webfs
# 4/4 passed (existing streaming/password tests plus mutations, HTTP, browser)

cmake -S tests/unit -B /tmp/T034-build-unit
cmake --build /tmp/T034-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T034-build-unit --output-on-failure
# 15/15 passed, including existing Filesystem rename/type/empty-rmdir semantics

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
# all passed

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# passed; real firmware size/section evidence above

PYTHONDONTWRITEBYTECODE=1 python3 tests/adv_webfs_page_test.py .
# extracted embedded JavaScript: node --check and DOM/fetch assertions passed

git diff --check
# passed
```

Additional fault suite run with AddressSanitizer/UndefinedBehaviorSanitizer:

```bash
cc -std=c11 -g -O1 -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -Iinclude -Iplatform/adv \
  tests/adv_webfs_mutations_test.c platform/adv/adv_webfs_logic.c \
  -o /tmp/T034-mutations-sanitize
ASAN_OPTIONS=detect_leaks=0 /tmp/T034-mutations-sanitize
# passed (not a runtime leak measurement)
```

Coverage includes new/replacement/empty uploads on both volume namespaces,
6,001-byte bodies, short receives and partial writes, every receive/cancellation
boundary, every FS operation failure, no-space, sync/close/rename failure,
cleanup failure retaining the primary error, exactly-once close, unchanged old
bytes before commit, bounded collisions, destination directory, absent volume,
UTF-8, exact 511-byte temp-path boundary and overflow, malformed/duplicate/unknown
rename parameters, root/cross-parent/cross-volume rejection, regular-file
replacement/same-name behavior, directory type errors and non-empty rejection.
HTTP tests execute production status/dispatch/receive code; browser tests exercise
confirm/cancel, error presentation, encoded filenames, sequential uploads,
refresh after success, busy serialization, and absence of directory rename.

### Manual/hardware validation still required

No hardware testing or flashing performed. Await supervisor review before the
packet's basic `/flash` and `/sd` mutation sequence, content comparison,
interrupted replacement with Q/Esc, repeated lifecycle checks, and same-boot
FT8/QMX smoke. Software/fake-FS evidence does not replace hardware acceptance.

### Known limitations / risks

- Existing ADV rename replacement retains its documented non-crash-atomic FAT
  backup/rollback behavior. No power-loss guarantee or new transaction journal.
- Upload needs free storage for the complete temporary file while retaining the
  old destination. A cleanup failure can leave a reserved-prefix temp; no stale
  temp scan/removal was added.
- Existing bounded URI/query limits remain: the combined encoded rename query
  must fit the 1,536-byte query buffer, even when individual decoded paths fit.
- Network loss after a completed rename can hide the success response; refresh
  the directory before retrying. Cancellation before commit discards the temp;
  cancellation after commit cannot undo the completed replacement.

### Commit

One implementation commit on `codex/T034-webfs-mutations`, parent
`02f6b3fe667a9cd1629ea4dee857e01ad0ebeb24`, titled
`Add safe ADV WebFS file mutations`. Exact pushed SHA is returned in the handoff.
No PR.

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and evidence. Special
attention:

- old destination is untouched before upload commit;
- temp handle close/cleanup on every failure path;
- no direct FATFS/VFS file mutations from WebFS;
- no whole-file upload buffer;
- rename restricted to regular file + same parent;
- roots/recursive delete protected;
- mutation methods are PUT/DELETE, with no accidental POST/CORS expansion;
- CPU1 USB Host/T033 lifecycle remains unchanged.

No PR is required.

## Supervisor review — implementation

Reviewed implementation commit:

```text
67a19a7654af45a6e7fd14a94c0933f5183350bc
```

Result: **PASS — ready for ADV hardware validation.**

The implementation satisfies the T034 safety boundary:

- upload uses an exclusive same-parent temporary file and bounded streaming;
- partial writes are completed before the next receive chunk;
- sync and close occur before the commit rename;
- the destination is never opened, truncated, or removed by the upload path;
- every pre-commit failure prevents rename and best-effort removes only the temp;
- acquired temp handles are closed exactly once;
- existing destinations remain intact across tested receive/write/sync/close/
  no-space/rename failures;
- temp-name collision retry is bounded;
- zero-byte upload is supported;
- mkdir/file-delete/empty-rmdir use MiniShell Filesystem directly;
- rename is regular-file policy through MiniShell and is restricted by WebFS to
  the same parent;
- root mutation, directory rename, recursive delete and cross-parent/cross-volume
  rename are rejected/not exposed;
- mutation endpoints are PUT/DELETE only; no POST, OPTIONS or CORS expansion;
- no direct FATFS/VFS mutation calls were added to WebFS;
- T033 SoftAP lifecycle, uppercase credential policy, CPU1 USB Host ownership,
  FT8 profile and public API v3 are unchanged.

Browser review also passes: mutation paths are constructed from one filename
component, untrusted names use DOM text nodes, destructive/replacement operations
have confirmations, sequential uploads refresh state between files, and
directory rename is absent.

Evidence is sufficient for hardware testing: Linux **71/71**, portable **15/15**,
focused WebFS tests, architecture checks, real ADV build and diff check pass.
Firmware is **1,374,432 B** with **0 B static SRAM delta** from the T034 baseline;
WebFS buffers and task stack sizes are unchanged.

The existing ADV FAT replace helper remains intentionally non-crash-atomic. T034
acceptance is for normal network/I/O interruption safety before the commit point,
not arbitrary power-loss atomicity.

Hardware validation should prioritize:

1. create/upload/replace/download exact-content checks on `/flash`;
2. mkdir -> upload -> rename -> delete -> empty-rmdir;
3. non-empty rmdir rejection;
4. representative `/sd` mutations;
5. interrupted large replacement with Q/Esc, proving the old destination remains
   intact;
6. same-boot FT8/QMX startup after WebFS exit.

## Architect test result

PASS on Cardputer ADV, 2026-09-20.

Hardware validation confirms:

- T033 browsing/download behavior remains intact;
- upload/create works;
- regular-file replacement works;
- mkdir, same-directory regular-file rename, file delete and empty-directory
  delete work;
- non-empty-directory protection works;
- representative `/sd` mutations work;
- interrupted replacement preserves the previous destination as designed;
- WebFS exits cleanly;
- FT8/QMX starts normally afterward in the same boot.

T034 is COMPLETE. The existing ADV FAT replacement helper retains its documented
non-crash-atomic power-loss limitation.
