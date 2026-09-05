# Task 4 - `nano` Interactive Editor — ACTIVE

## Goal

Task 4 proves that a genuinely useful interactive application can live entirely
above the stable MiniShell application ABI.

The milestone is one independently built runtime application:

```text
M$> nano /sd/notes.txt
```

`nano.elf` must use only MiniShell application services for memory, filesystem,
display, and input. It must not depend on ESP-IDF, Tab5 hardware, terminal escape
sequences, FATFS internals, or resident/private MiniShell interfaces.

Task 4 deliberately does **not** include `cp`, `mv`, `rm`, `mkdir`, `rmdir`, or
other planned utilities. Those remain later application milestones.

## Why `nano`

`nano` is a stronger ABI exercise than another simple command because it combines:

```text
Filesystem ABI   load + save
Memory ABI       dynamic text buffer
Display ABI      full-screen logical text UI
Input ABI        normalized characters + navigation/control keys
app lifecycle    foreground ownership + clean return to shell
```

If the existing ABI is insufficient, Task 4 should expose the missing primitive
through a real use case rather than speculative ABI design.

## V1 scope

Task-4 `nano` implements the smallest useful familiar subset:

```text
nano <absolute-path>

open existing text file or create an empty buffer for a missing file
insert printable ASCII text
Enter                   insert newline
Backspace / Delete      delete text
arrows                   move cursor
Home / End               move within line
PageUp / PageDown        move by one visible page
Ctrl-O                   write/save current file
Ctrl-W                   search forward, wrapping once
Ctrl-X                   exit
```

When the buffer is modified, Ctrl-X asks whether to save before exiting.

The editor does not implement the full GNU nano option surface.

## Familiar key policy

Use nano's familiar command keys rather than inventing MiniShell-specific keys:

```text
^O  Write Out / Save
^W  Search
^X  Exit
```

The bottom help line should show these bindings so the application remains
self-explanatory.

## Text model

V1 is intentionally conservative:

- text editing is ASCII-oriented because Display ABI v0 guarantees printable
  7-bit ASCII and the first terminal Input backend guarantees ASCII;
- LF is the internal newline representation;
- CRLF/CR input may be normalized to LF when loading;
- embedded NUL bytes are rejected as non-text input;
- Tab input inserts spaces rather than relying on terminal tab semantics;
- search is simple forward ASCII substring search;
- the editor may impose a documented file-size limit appropriate for an MCU.

UTF-8-aware editing can be added later when the Display/Input portability contract
for Unicode is strong enough to justify it.

## Editor data structure

Use a simple dynamically growing contiguous byte buffer first:

```text
[data.........................]
          ^ cursor
```

Insert/delete may use `memmove()`.

This is intentionally preferred over a gap buffer, rope, piece table, tree, or
other editor-oriented structure for V1 because MiniShell's normal configuration
and note files are expected to be small. The implementation is easier to study
and test. Change the internal representation later only if measurement shows a
real problem.

## Modular structure

Keep the app understandable:

```text
apps/nano/
  README.md
  CMakeLists.txt
  main/
    nano.c             controller / ABI discovery / event loop
    nano_buffer.c/.h   text data, cursor, navigation, search
    nano_file.c/.h     Filesystem ABI load/save
    nano_ui.c/.h       Display ABI rendering and prompts
```

`nano_buffer` should contain no MiniShell or ESP-IDF types so its editing behavior
can be tested as ordinary host C.

## Display model

Display ABI v0 has explicit character-cell drawing but no hardware cursor or text
style. Task 4 does not expand the ABI merely for that reason.

Nano V1 renders the logical cursor portably as an inserted visible `|` marker in
the current display line. This is a presentation marker only; it is not stored in
the file.

Initial screen layout:

```text
row 0        title / path / modified marker
rows 1..N    text viewport
row N+1      status or prompt
last row     ^O Save   ^W Search   ^X Exit
```

The application queries display geometry and does not assume 80x24.

## Save semantics

V1 saves through the existing Filesystem ABI:

```text
open(path, WRITE | CREATE | TRUNC)
write until all bytes are written
sync
close
```

A failed write/sync/close leaves the buffer marked modified and reports an error.

Task 4 does not add filesystem rename/remove primitives merely to implement an
atomic-save temporary-file scheme. Safe atomic replacement can be revisited once
rename/remove are added for the later `mv`/`rm` milestone.

## ABI decision

The current MiniShell ABI appears sufficient for Nano V1:

```text
Memory      alloc / realloc / free
Filesystem open / close / read / write / sync / stat
Display     get_info / clear / clear_at / write_at / present
Input       normalized key events including Ctrl characters and navigation keys
```

Therefore Task 4 starts with **no public ABI change**.

## Testing

Testing remains layered.

### Host unit tests

Primary tests target `nano_buffer` as ordinary C:

- empty buffer initialization;
- insert and append;
- newline insertion;
- Backspace and Delete semantics;
- left/right bounds;
- Home/End;
- up/down while preserving a useful column;
- line/column reporting;
- forward search and wrap;
- growth/reallocation;
- dirty flag behavior;
- teardown.

### ELF / hardware validation

On the Tab5 terminal backend:

1. build `nano.elf` independently;
2. upload it through resident MFT1;
3. create a new text file;
4. type several lines and edit in the middle;
5. navigate with arrows/Home/End/PageUp/PageDown;
6. search with Ctrl-W;
7. save with Ctrl-O;
8. exit with Ctrl-X and return cleanly to `M$>`;
9. verify the file with existing `cat.elf`;
10. reopen it in nano, modify it, exercise the dirty-exit save prompt, and verify
    persistence again.

## Completion criteria

Task 4 is complete when:

- `nano.elf` is an independently built/loadable application;
- it uses only the public MiniShell ABI;
- new and existing ASCII text files can be edited;
- navigation, search, save, and dirty-exit handling work;
- host editor-buffer tests pass;
- the real terminal Display/Input backend works without app-side ANSI parsing;
- saved content is verified independently after editor exit;
- shell input ownership is clean after the app returns;
- no public ABI expansion was made unless hardware testing proves one genuinely
  necessary.
