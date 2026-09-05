# MiniShell `rmdir`

`rmdir` removes one empty directory through the MiniShell Filesystem ABI.

```text
rmdir <directory>
```

Non-empty directories are rejected with `MINI_ERR_NOT_EMPTY`. Recursive deletion
is intentionally out of scope.

## Build

```bash
cd apps/rmdir
idf.py set-target esp32p4
idf.py elf
```

Install the resulting `build/rmdir.app.elf` as `/sd/apps/rmdir.elf`.
