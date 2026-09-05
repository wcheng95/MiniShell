# MiniShell `rm`

`rm` removes one regular file through the MiniShell Filesystem ABI.

```text
rm <file>
```

V1 deliberately excludes recursive deletion, wildcard expansion, and directory
removal. Use `rmdir` for an empty directory.

## Build

```bash
cd apps/rm
idf.py set-target esp32p4
idf.py elf
```

Install the resulting `build/rm.app.elf` as `/sd/apps/rm.elf`.
