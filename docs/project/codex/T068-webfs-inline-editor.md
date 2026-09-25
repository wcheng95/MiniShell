# T068 — WebFS explicit download and inline configuration editor

Status: READY

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
