# MiniShell `cp`

`cp` is the Task-5 binary-safe file copy utility for MiniShell.

Initial scope:

```text
cp <absolute-source> <absolute-destination>
```

V1 copies one regular file to another regular-file path. The destination is
created if missing and overwritten if it already exists. Directory operands and
recursive copying are intentionally out of scope.

The implementation uses only the public MiniShell System and Filesystem ABIs. It
handles partial reads and partial writes explicitly, synchronizes the destination
before close, and treats file contents as arbitrary bytes.

## Build

```bash
cd apps/cp
idf.py set-target esp32p4
idf.py elf
```

Install with the resident transfer tool:

```bash
python3 tools/minishell_transfer.py /dev/ttyACM0 \
    put apps/cp/build/cp.app.elf /sd/apps/cp.elf
```

Then run:

```text
M$> cp /sd/source.bin /sd/copy.bin
```

See `docs/task5.md` for the milestone contract and validation plan.
