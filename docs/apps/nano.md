# Nano Application

`nano` is MiniShell's small interactive text editor and a useful reference for a non-trivial portable application.

It uses only MiniShell services. Terminal escape sequences, POSIX file descriptors, and platform input objects do not appear in the application.

## Module layout

```text
nano.c          orchestration and input policy
nano_buffer.c   text-buffer state and editing
nano_file.c     persistence through Filesystem ABI
nano_ui.c       rendering through Display ABI
nano_util.c     small string helpers
```

This is also the preferred interpretation of the one-owner rule: one coherent application can use several focused implementation files without splitting semantic ownership.

## Current controls

```text
Ctrl-O   save
Ctrl-W   search
Ctrl-X   exit
arrows   move cursor
```

The editor uses normalized MiniShell key events and the logical text Display API.

## Cursor rendering

When `write_at_attr()` and `MINI_TEXT_ATTR_INVERSE` are available, nano renders the character under the cursor in inverse video. Backends implement that semantic effect natively; Linux maps it to terminal reverse video below MiniShell.

If text attributes are unavailable, nano falls back to a simpler cursor representation.

## Persistence

File reads and writes use the Filesystem ABI. Open handles are owned by the foreground application and are reclaimed by MiniShell at application teardown if the editor exits abnormally without closing one itself.

## Why nano has its own document

Unlike the tiny utility applications, nano has enough internal state, UI behavior, editing rules, and persistence logic to benefit from separate documentation. It remains much smaller than domain applications such as MiniFT8, which may maintain their own documentation subtree.
