# MiniShell `nano`

`nano` is the Task-4 interactive text editor for MiniShell.

It intentionally implements a small, familiar subset rather than GNU nano in
full:

```text
nano <absolute-path>

^O  save current file
^W  search forward
^X  exit
```

Navigation uses arrows, Home, End, PageUp, and PageDown. Enter inserts a newline;
Backspace/Delete remove text. Tab inserts four spaces.

V1 is ASCII-oriented and limits an editable file to 64 KiB. Existing CRLF/CR line
endings are normalized internally to LF when loaded. Embedded NUL, non-ASCII, and
other control bytes are rejected as unsupported text.

The application uses only the public MiniShell Memory, Filesystem, Display, and
Input ABIs. It contains no ESP-IDF, Tab5, FATFS, or terminal-specific code.

## Build

```bash
cd apps/nano
idf.py set-target esp32p4
idf.py elf
```

The resulting ELF is expected at:

```text
apps/nano/build/nano.app.elf
```

Install with resident MiniShell file transfer:

```bash
python3 tools/minishell_transfer.py /dev/ttyACM0 \
    put apps/nano/build/nano.app.elf /sd/apps/nano.elf
```

Then run:

```text
M$> nano /sd/notes.txt
```

See `docs/task4.md` for the milestone contract and validation plan.
