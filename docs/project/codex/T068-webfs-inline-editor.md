# T068 — WebFS explicit download and inline configuration editor

Status: REVIEW

## Purpose

Improve the existing ADV WebFS UI so small persistent configuration can be edited conveniently from a phone, while keeping WebFS a simple file manager rather than a general browser editor.

This task supersedes the current experimental `codex/T068-webfs-inline-editor` work. Inspect that branch for context if useful, but do not assume its implementation or tests are correct. Start from current `main` unless the coordinator explicitly tells you otherwise.

## Required behavior

### File listing

For every regular file, show explicit actions:

```text
Download  Rename  Delete
```

The filename itself must no longer mean Download.

Directories retain their existing navigation behavior and existing applicable actions.

### Editable filenames

Only regular files whose basename is exactly:

```text
setting.txt
alias.txt
```

are editable by clicking the filename.

All other regular filenames are plain, non-clickable text regardless of extension or contents.

Apply a defensive maximum editable size of 64 KiB. If `setting.txt` or `alias.txt` exceeds that limit, display its filename as non-clickable; Download/Rename/Delete must still work.

This is filename-based convenience, not content sniffing and not a general text-editor framework.

### Editor page/view

Clicking an editable filename opens a simple browser editor containing:

- the full file path;
- a plain `<textarea>`;
- Save;
- Cancel.

No CodeMirror, Monaco, CDN, external JavaScript, or other editor dependency.

Save replaces the complete file contents and returns to the WebFS directory view. Use the existing safe WebFS file-replacement/upload path rather than creating a second persistence mechanism.

Cancel discards browser-side edits, performs no filesystem mutation, and returns to the directory view.

If opening or saving fails, show a useful error and do not silently discard the editor contents.

## Existing architecture/invariants to preserve

Read before editing:

```text
AGENTS.md
docs/README.md
docs/project/webfs.md
docs/project/codex/T033-adv-webfs-readonly.md
docs/project/codex/T034-webfs-mutations.md
docs/project/codex/T035-webfs-softap-settings.md
```

Preserve:

- ADV-only WebFS foreground utility;
- self-contained browser UI;
- only `/flash` and `/sd` exposed;
- MiniShell Filesystem API ownership;
- streamed file transfer;
- existing path validation/traversal protection;
- existing temporary-file + sync + close + rename safe replacement behavior;
- bounded ADV memory;
- Q/Esc WebFS shutdown behavior;
- existing upload/mkdir/rename/delete behavior.

Do not change MiniShell public APIs or configuration formats.

## Implementation guidance

The existing backend already has the primitives required:

- `GET /api/file?path=...` streams a file;
- `PUT /api/file?path=...` performs safe complete replacement.

Prefer implementing this as browser-page behavior around those existing endpoints. Backend changes should only be made if genuinely required and must preserve streaming/download behavior.

One subtle requirement: normal file GETs used by the editor must not force browser download semantics. Explicit Download must still download as an attachment. Choose a minimal clean mechanism to distinguish editor-read from explicit download if the current HTTP handler always sets `Content-Disposition: attachment`. Do not duplicate file-reading logic.

## Tests

Update/add focused tests for at least:

1. ordinary regular filename is not clickable;
2. every regular file has explicit Download/Rename/Delete;
3. `setting.txt` is clickable when <=64 KiB;
4. `alias.txt` is clickable when <=64 KiB;
5. other similar names such as `setting.txt.bak` are not editable;
6. oversized `setting.txt`/`alias.txt` is not clickable;
7. editor GET loads exact file contents into textarea;
8. Save sends complete edited contents through the safe file PUT path and returns to listing;
9. Cancel performs no mutation and returns to listing;
10. existing upload/rename/delete/mkdir behavior remains covered;
11. page JavaScript passes `node --check`.

Do not weaken an existing test merely to make the new implementation pass. The page test DOM/fetch stub must model the browser behavior needed by the production code deterministically. Avoid stale DOM references after a listing rebuild.

Run the focused WebFS tests first, then the full Linux CTest suite.

## Documentation

Update `docs/project/webfs.md` to describe the accepted design. Do not broaden the task into changing application settings files; MiniFT8 `station.txt` -> `setting.txt` is separate work.

## Non-goals

- general-purpose text editing;
- arbitrary text-file detection;
- editing CSV/JSON/Markdown merely because they are text;
- application Settings-screen removal;
- MiniFT8 `station.txt` migration;
- startup-command support;
- STA mode, mDNS, captive portal, HTTPS, authentication, or remote WebFS stop;
- unrelated WebFS cleanup.

## Required Codex handoff

Work on:

```text
codex/T068-webfs-inline-editor
```

Before coding, reset/recreate that branch from current `main` so the prior experimental ChatGPT commits are not part of the implementation history.

When complete, record in this task file:

```text
Implementation summary
Files changed
Behavior/invariants preserved
Tests run and results
Hardware/manual validation still required
Known limitations or risks
Commit reference
```

Push one reviewable implementation commit. Do not merge to `main` and do not open a PR unless asked.

## Codex implementation notes

### Implementation summary

Recreated `codex/T068-webfs-inline-editor` from current `origin/main`,
`81d1ed8d35c28a73befd6010dfbd40f5c14bacbb`. Prior experimental commits are
excluded. No scope deviations.

Regular files now have explicit Download/Rename/Delete actions. Exact
`setting.txt` and `alias.txt` basenames up to 65,536 bytes open the inline
textarea; other regular filenames are plain text. Save sends the complete
UTF-8 text through the existing safe PUT replacement. Cancel clears browser
edits without a request. Failed opens show the server/network error; failed
saves retain the editor and its contents. A listing failure after a successful
save is reported separately in the directory view.

Editor GETs carry `X-WebFS-Read: 1`, suppressing attachment headers in the
existing streaming HTTP reader. All ordinary file GETs retain attachment
semantics, including empty files. No additional route or file-reading logic.
The browser checks actual downloaded bytes and encoded save bytes against the
limit; controls and textarea are disabled during requests.

### Files changed

- `platform/adv/adv_webfs_page.h`: listing actions, inline editor, errors and bounds.
- `platform/adv/adv_webfs_http.c`: distinguish editor reads from attachment GETs.
- `tests/adv_webfs_page_test.py`: DOM tags/hidden state/selectors from page IDs,
  asynchronous file-fetch stub, editor and existing mutation regression coverage.
- `tests/adv_webfs_http_test.py`: execute production read adapter and streaming
  logic as well as existing mutation adapter tests; check attachment behavior
  for empty/multichunk files and missing/valid/invalid editor headers.
- `docs/project/webfs.md`: document the T068 design.
- This task packet: implementation and validation evidence.

### Behavior/invariants preserved

No public API, configuration format, Filesystem logic, safe replacement protocol,
path/query validation, Wi-Fi, USB ownership or Q/Esc lifecycle changes. ADV
continues to stream using its existing bounded buffers, routes, socket count
and stack settings. Browser uploads/mkdir/rename/delete remain covered. No
external browser dependency or application settings migration.

### Tests run and results

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R adv_webfs
# PASS: 6/6, including node --check and page/HTTP regressions
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: 119/119 (58.58 seconds)
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: firmware 0x1520f0 bytes; app partition 78% free. No flashing.
git diff --check
# PASS
```

The first HTTP harness run exposed the ISO minimum 4,095-character string
warning after including the firmware page (already larger than that limit at
baseline). The harness now disables only `-Woverlength-strings`, retaining
`-Wall -Wextra -Werror -Wpedantic`; the final focused run passes. ADV build
emitted SDK `#include_next` pedantic warnings but completed successfully.

Tests include both editable names at 0/65,536/65,537 bytes, similar names,
explicit actions on oversized files, exact UTF-8 text loading and complete PUT
bodies, empty saves, Cancel with no request, failed read/save, byte rather than
character limits, duplicate-save/Cancel guards during a pending save, directory
navigation, and post-save listing failure. DOM rows are reacquired after every
listing rebuild. Existing mutation assertions are preserved; the old filename
Download assertion is replaced by the required explicit-action assertion.

### Hardware/manual validation still required

On ADV with phone/desktop browser: verify Download attachment behavior, editing
both configuration filenames under `/flash` and `/sd`, Save and Cancel,
interrupted save with retained edits, oversized-file actions, and existing
upload/rename/delete/mkdir. Confirm Q/Esc shutdown and same-boot FT8/QMX startup.
Configured SoftAP edits still take effect only on the next WebFS launch.
No hardware was flashed or exercised here.

### Known limitations or risks

This is a UTF-8 text editor with normal textarea newline normalization, not a
byte-preserving editor for arbitrary encodings. The 64 KiB check is browser-side;
ordinary streamed downloads/uploads retain their prior size behavior. A lost
response after a committed PUT can leave save status uncertain, as with existing
uploads; browser edits remain available for retry. Existing FAT replacement is
safe against ordinary pre-commit failures, not arbitrary power loss. Real mobile
browser rendering and device runtime memory have not been measured.

### Commit reference

One implementation commit titled `T068: add WebFS inline configuration editor`
on `codex/T068-webfs-inline-editor`, parent
`81d1ed8d35c28a73befd6010dfbd40f5c14bacbb`. Exact pushed SHA is provided in the
handoff. No merge or PR.
