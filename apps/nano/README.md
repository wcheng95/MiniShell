# MiniShell `nano`

`nano` is MiniShell's small interactive text editor. It is intentionally a familiar subset rather than GNU nano in full.

```text
nano <absolute-path>
```

Controls:

```text
Ctrl-O   save current file
Ctrl-W   search forward
Ctrl-X   exit
```

Navigation uses arrows, Home, End, PageUp, and PageDown. Enter inserts a newline; Backspace/Delete remove text. Tab inserts four spaces.

The editor is ASCII-oriented and limits an editable file to 64 KiB. Existing CRLF/CR line endings are normalized internally to LF when loaded. Embedded NUL, non-ASCII, and other control bytes are rejected as unsupported text.

`nano` uses only the public MiniShell Memory, Filesystem, Display, and Input ABIs. It contains no Linux/POSIX, NuttX, ESP-IDF, board, FATFS, or terminal-specific code.

On the Linux reference target it is built with MiniShell as:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
./build-linux/minishell
```

Then:

```text
M$> nano /sd/notes.txt
```

The Linux build emits `build-linux/runtime/apps/nano.so`; other targets may use a different loader format without changing the application source.
