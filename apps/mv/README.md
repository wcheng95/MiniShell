# MiniShell `mv`

`mv` is a small runtime-loaded file rename utility.

```text
mv <source> <destination>
```

V1 moves exactly one regular file by calling the MiniShell Filesystem ABI
`rename()` operation.

Safety policy:

- the source must be a regular file;
- the destination must not already exist;
- no copy-and-delete fallback is attempted;
- cross-filesystem moves are therefore unsupported unless a future backend can
  provide a true rename operation.

This deliberately avoids silent replacement on the non-journaled FAT filesystem.

## Build

```bash
cd apps/mv
idf.py set-target esp32p4
idf.py elf
```

Install the resulting `build/mv.app.elf` as `/sd/apps/mv.elf`.
