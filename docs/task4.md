# Task 4 - `nano` Interactive Editor — ACTIVE

## Goal

Task 4 proves that a genuinely useful interactive application can live entirely
above the stable MiniShell application ABI.

The milestone is one independently built runtime application:

```text
M$> nano /sd/notes.txt
```

`nano.elf` uses MiniShell application services for memory, filesystem, display,
and input. It must not depend on ESP-IDF, Tab5 hardware, terminal escape sequences,
FATFS internals, or resident/private MiniShell interfaces.

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

The bottom help line shows these bindings.

## Text model

V1 is intentionally conservative:

- maximum editable file size is 64 KiB;
- text editing is ASCII-oriented because Display ABI v0 guarantees printable
  7-bit ASCII and the first terminal Input backend guarantees ASCII;
- LF is the internal newline representation;
- CRLF/CR input is normalized to LF when loading;
- embedded NUL, non-ASCII, and other control bytes are rejected;
- existing Tab bytes are preserved but displayed as a single blank cell;
- Tab input inserts four spaces rather than relying on terminal tab semantics;
- search is simple forward ASCII substring search with one wrap.

UTF-8-aware editing can be added later when the Display/Input portability contract
for Unicode is strong enough to justify it.

## Editor data structure

V1 uses a dynamically growing contiguous byte buffer:

```text
[data.........................]
          ^ cursor
```

Insert/delete use `memmove()` semantics. This is intentionally preferred over a
gap buffer, rope, piece table, tree, or other editor-oriented structure because
MiniShell's normal configuration and note files are expected to be small. Change
the representation later only if measurement shows a real problem.

Vertical movement retains a preferred column across short intervening lines.

## Modular structure

```text
apps/nano/
  README.md
  CMakeLists.txt
  main/
    nano.c             controller / ABI discovery / event loop
    nano_buffer.c/.h   text data, cursor, navigation, search
    nano_file.c/.h     Filesystem ABI load/save
    nano_ui.c/.h       Display ABI rendering
    nano_util.c/.h     self-contained tiny string/libc support
```

`nano_buffer` contains no MiniShell or ESP-IDF types and is tested as ordinary
host C.

## Display model

Display ABI v0 has explicit character-cell drawing but no hardware cursor or text
style. Task 4 does not expand the ABI merely for that reason.

Nano V1 renders the logical cursor portably as an inserted visible `|` marker in
the current display line. The marker is not stored in the file.

Initial screen layout:

```text
row 0        title / path / modified marker
rows 1..N    text viewport
row N+1      status or prompt
last row     ^O Save   ^W Search   ^X Exit
```

The application queries display geometry and does not assume 80x24. V1 requires
at least 20 columns and 5 rows.

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

The current MiniShell ABI is sufficient for Nano V1:

```text
Memory      alloc / realloc / free
Filesystem open / close / read / write / sync / stat
Display     get_info / clear / write_at / present
Input       normalized key events including Ctrl characters and navigation keys
```

Task 4 therefore makes **no public ABI change**.

## ELF self-containment

MiniShell currently intentionally exports only:

```text
mini_api_get
```

to runtime applications. Nano must not accidentally depend on resident libc or
ESP-IDF symbols.

The app therefore includes a tiny internal implementation of the small memory/
string primitives its compiled code may need (`memcpy`, `memmove`, `memset`,
`memcmp`, `strlen`) and does not use formatted libc output such as `snprintf`.
The Nano component is compiled freestanding with builtins disabled.

Before installation, inspect the generated ELF:

```bash
riscv32-esp-elf-nm -u build/nano.app.elf
```

The intended unresolved MiniShell symbol is `mini_api_get`; unexpected libc,
ESP-IDF, or platform symbols are a Task-4 boundary failure.

## Testing

### Host unit tests

Task 4 adds:

```text
nano_editor_unit
```

It covers the pure `nano_buffer` implementation, including:

- empty initialization;
- insert/delete;
- navigation and preferred-column behavior;
- Home/End;
- line/column reporting;
- forward search and wrap;
- dynamic growth;
- dirty flag behavior;
- teardown.

### ELF / hardware validation

On the Tab5 terminal backend:

1. build `nano.elf` independently;
2. inspect undefined symbols;
3. upload it through resident MFT1 without rebuilding MiniShell;
4. create a new text file;
5. type several lines and edit in the middle;
6. navigate with arrows/Home/End/PageUp/PageDown;
7. search with Ctrl-W;
8. save with Ctrl-O;
9. exit with Ctrl-X and return cleanly to `M$>`;
10. verify the file with existing `cat.elf`;
11. reopen it, modify it, exercise the dirty-exit save prompt, and verify
    persistence again.

## Implementation status

Source implementation is complete enough for the first validation pass:

```text
nano app/build skeleton        implemented
nano_buffer                    implemented
nano_editor_unit               implemented
nano_file                      implemented
nano_ui                        implemented
nano controller                implemented
self-contained mini libc       implemented
public ABI changes             none
host test                      pending
ELF build/symbol check         pending
Tab5 interactive validation    pending
```

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
