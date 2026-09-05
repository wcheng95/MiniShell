# MiniShell `mkdir`

`mkdir` creates one directory through the MiniShell Filesystem ABI.

```text
mkdir <directory>
```

V1 does not implement `-p`; the parent directory must already exist.

## Build

```bash
cd apps/mkdir
idf.py set-target esp32p4
idf.py elf
```

Install the resulting `build/mkdir.app.elf` as `/sd/apps/mkdir.elf`.
