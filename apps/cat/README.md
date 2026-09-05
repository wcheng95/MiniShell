# cat

`cat` is the first ordinary MiniShell utility application.

It demonstrates the intended separation between resident MiniShell facilities
and runtime-loaded commands:

```text
resident MiniShell     file transfer, shell, loader, ABI services
cat.elf                ordinary user utility built only against public ABI
```

## Current V1 behavior

```text
M$> cat /sd/test.txt
```

V1 reads the file through the Filesystem ABI and emits text through
`system.write()`.

Because the current System ABI exposes a NUL-terminated text sink rather than a
raw stdout byte stream, this V1 `cat` is intentionally a text-file utility. It
rejects files containing NUL bytes instead of pretending to be binary-safe.

## Build

```bash
cd apps/cat
idf.py set-target esp32p4
idf.py elf
```

Output:

```text
apps/cat/build/cat.app.elf
```

Upload it with MiniShell file transfer:

```bash
python3 tools/minishell_transfer.py /dev/ttyACM0 \
    put apps/cat/build/cat.app.elf /sd/apps/cat.elf
```

Then run:

```text
M$> cat /sd/test.txt
```

`cat` is architecture-specific as a compiled ELF, while its source depends only
on the portable MiniShell ABI.
