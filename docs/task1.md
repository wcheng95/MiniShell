# Task 1 - MiniEditor (`med`)

## Goal

Task 1 builds the first genuinely useful MiniShell application: a small
nano-like terminal text editor named `med`.

The purpose is not to clone nano. The purpose is to let a real application drive
the design of MiniShell's first reusable filesystem and console/input services.

Expected usage:

```text
M$> med /sd/notes.txt
```

`med` runs as a separately built native ELF application, edits the file in a
full-screen terminal UI, saves through MiniShell services, returns normally, and
leaves the resident shell healthy.

## Architectural Rule

Task 1 keeps the dependency direction strict:

```text
med.elf
   |
   | MiniShell ABI only
   v
+-------------------------------+
| MiniShell services            |
|                               |
| console/input    filesystem   |
+-------------------------------+
              |
              v
+-------------------------------+
| platform implementation       |
| USB Serial/JTAG   FATFS/VFS   |
+-------------------------------+
              |
              v
          ESP32-P4 / Tab5
```

The application must not depend on ESP-IDF, M5Stack BSP, FATFS internals, USB
Serial/JTAG driver APIs, or platform-specific handles.

The service interfaces are the contract. Implementations behind those contracts
may change without requiring changes to unrelated modules.

## Application Structure

The initial `med` source should remain small and understandable:

```text
examples/med/
|-- CMakeLists.txt
|-- main/
|   |-- CMakeLists.txt
|   |-- main.c
|   |-- editor.c
|   |-- editor.h
|   |-- document.c
|   |-- document.h
|   |-- view.c
|   `-- view.h
`-- idf_component.yml
```

Responsibilities:

```text
main.c       application lifecycle and argument handling
editor.c     editor state machine, cursor movement, commands
document.c   text storage and editing operations
view.c       terminal rendering through MiniShell console API
```

`document.c` must know nothing about MiniShell console rendering, USB, FATFS,
or ESP-IDF. It operates only on the in-memory document model.

`view.c` must not read or write files.

`editor.c` coordinates document operations and user commands, but does not own
hardware.

## Task 1 V1 Features

Included:

- open an existing text file
- create a new text file
- display text in a full-screen terminal UI
- insert printable characters
- Backspace
- Delete
- Enter / split line
- cursor movement with arrow keys
- vertical scrolling
- save with `Ctrl-S`
- exit with `Ctrl-X`
- short help/status line
- return cleanly to `M$>`

Explicitly deferred:

- syntax highlighting
- search/replace
- copy/paste engine
- undo/redo
- line numbers
- mouse support
- multiple buffers
- very-large-file optimization
- background applications
- terminal-size negotiation beyond a simple initial fixed/default geometry

## Console/Input Boundary

Applications should not parse raw USB Serial/JTAG hardware or driver state.
MiniShell owns the console.

Task 1 should extend the ABI with a platform-neutral console service. Conceptual
operations include:

```text
write text
read normalized key event
clear screen
move cursor(row, column)
erase line
show/hide cursor
```

The exact C signatures are not frozen by this document.

MiniShell should normalize common terminal escape sequences before they reach the
application. `med` should see logical key events such as:

```text
MINI_KEY_UP
MINI_KEY_DOWN
MINI_KEY_LEFT
MINI_KEY_RIGHT
MINI_KEY_DELETE
MINI_KEY_BACKSPACE
MINI_KEY_ENTER
MINI_KEY_CTRL_S
MINI_KEY_CTRL_X
printable character
```

This keeps ANSI/VT100 byte-sequence parsing inside the console service rather
than duplicating it in every application.

For Task 1 the Tab5 implementation may use ANSI/VT100 sequences internally for
screen control. Those escape sequences are a platform/console implementation
detail, not part of the portable application contract.

## Filesystem Boundary

Applications should use MiniShell filesystem services rather than newlib/FATFS
file functions directly.

Conceptually:

```text
med
 |
 | mini filesystem API
 v
MiniShell filesystem service
 |
 | private FILE*/VFS/FATFS state
 v
microSD
```

Application-visible file handles should be MiniShell-owned opaque values, not
`FILE *`, FATFS objects, or ESP-IDF handles.

Task 1 initially needs enough operations to:

```text
open/create
read
write
close
```

Additional operations should be added only when a real application needs them.

## Resource Ownership

MiniShell remains the owner of resources acquired through its services.

Conceptually:

```text
application receives: mini_file_t = opaque handle

MiniShell owns:
    handle slot
      -> underlying platform file object
      -> owning foreground application
      -> state
```

When the foreground application returns normally, the app manager must reclaim
any MiniShell-managed resources still owned by that application before unloading
the ELF.

This cleanup is cooperative lifecycle management, not memory protection.

## Initial Document Model

Task 1 deliberately avoids sophisticated editor data structures.

Use an understandable line-oriented representation first:

```text
document
  -> line 0 character buffer
  -> line 1 character buffer
  -> line 2 character buffer
  -> ...
```

This should make insert, delete, split-line, join-line, cursor movement, loading,
and saving easy to understand and test.

If later workloads justify a gap buffer, piece table, rope, or another
representation, `document.c` can change without changing the MiniShell ABI or
console/filesystem services.

That replaceability is part of the Task 1 design goal.

## Initial Screen Model

Start with a simple terminal geometry, initially 80 columns by 24 rows unless the
implementation proves another default more useful.

Suggested layout:

```text
row 0       title / filename
rows 1-21   document viewport
row 22      status/message
row 23      shortcuts
```

Example:

```text
 MiniEditor 0.1                         /sd/notes.txt
------------------------------------------------------
This is a text file.
The cursor can move around.



------------------------------------------------------
^S Save    ^X Exit    ^G Help
```

The editor tracks logical document position separately from viewport position:

```text
cursor_line
cursor_column
screen_top_line
```

## Development Steps

### 1. Define minimal Task 1 ABI additions

Add only the console/input and filesystem operations required by `med` V1.
Do not expose ESP-IDF or libc file handles.

### 2. Implement resident services

Implement the MiniShell-side console/input and filesystem services behind the
new ABI tables.

### 3. Add resource bookkeeping

Track app-owned opaque filesystem handles so normal app teardown can reclaim
leaked handles.

### 4. Build a minimal `med` ELF

First milestone inside the application:

```text
M$> med /sd/notes.txt
```

The app should be able to open/load the file, render it, and exit back to the
shell before editing behavior is added.

### 5. Add editing operations

Add character insertion, Backspace/Delete, Enter, cursor motion, and scrolling.

### 6. Add save

`Ctrl-S` writes the current in-memory document through the MiniShell filesystem
service.

### 7. Validate lifecycle repeatedly

Open, edit, save, exit, reopen, and repeat without rebooting or exhausting
MiniShell-managed resources.

## Success Criteria

Task 1 is complete when real Tab5 hardware demonstrates all of the following:

1. `med` is built separately and runtime-loaded as an ELF application.
2. `med` includes no ESP-IDF or M5Stack platform dependency.
3. An existing text file can be opened through MiniShell filesystem services.
4. A new text file can be created through MiniShell filesystem services.
5. Text is displayed through the MiniShell console service.
6. Arrow-key movement works through normalized MiniShell key events.
7. Insert, Backspace, Delete, and Enter work.
8. Vertical scrolling works.
9. `Ctrl-S` saves the edited document correctly.
10. `Ctrl-X` exits normally and restores `M$>`.
11. Reopening the file shows the saved content.
12. MiniShell reclaims any application-owned file handles during normal teardown.
13. Repeated editor launch/edit/save/exit cycles do not exhaust resources or
    destabilize the resident shell.

## Design Principle Proven by Task 1

Task 0 proved that MiniShell can load an application.

Task 1 should prove that a useful application can be built on stable MiniShell
services while its own internals and the platform implementations remain
independently replaceable:

```text
change document representation  -> filesystem service unaffected
change FATFS implementation      -> med unaffected
change terminal driver           -> med unaffected
change editor rendering strategy -> app loader unaffected
```

That isolation is a core MiniShell objective, not merely a coding style.
