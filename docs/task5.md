# Task 5 - `cp` Binary File Copy — ACTIVE

## Goal

Task 5 adds the next ordinary runtime application after Nano:

```text
M$> cp /sd/source.bin /sd/copy.bin
```

The milestone proves that the existing Filesystem ABI can support a useful
binary-safe streaming utility without expanding the public ABI.

## V1 scope

```text
cp <absolute-source> <absolute-destination>
```

V1 behavior:

- copy exactly one regular file;
- source and destination paths are absolute MiniShell paths;
- destination is created if absent;
- existing regular-file destination is overwritten;
- file contents are treated as arbitrary bytes;
- embedded NUL and non-text data are copied unchanged;
- partial reads and partial writes are handled correctly;
- destination is synchronized before close;
- exact textual source/destination equality is rejected;
- directory source or destination operands are rejected;
- recursive copy, options, metadata preservation, wildcard expansion, and
  destination-directory basename behavior are out of scope.

This is intentionally the smallest useful `cp`, not GNU coreutils compatibility.

## ABI decision

No public ABI change is required.

The existing Filesystem ABI already provides:

```text
stat
open
read
write
sync
close
```

That is sufficient for a streaming file-to-file copy. Task 5 therefore follows
the project rule that applications drive ABI growth rather than adding
filesystem operations speculatively.

The later `mv`, `rm`, `mkdir`, and `rmdir` milestones are still expected to drive
namespace-changing Filesystem ABI extensions.

## Architecture

```text
cp.c
  controller / ABI discovery / user-facing errors
       |
       v
cp_copy.c
  binary streaming copy core
       |
       v
MiniShell Filesystem ABI
```

`cp_copy.c` has no ESP-IDF, FATFS, Tab5, or resident/private MiniShell dependency.

## Copy algorithm

V1 uses a fixed 1024-byte buffer on the foreground app task stack:

```text
source --read--> [ 1024-byte buffer ] --write--> destination
```

The loop is deliberately explicit:

1. `stat` source and reject non-file objects;
2. inspect destination and reject directories;
3. open source for READ;
4. open destination with WRITE | CREATE | TRUNC;
5. repeatedly read up to 1024 bytes;
6. loop until every byte from each read has been written;
7. stop on EOF (`MINI_OK` + zero-byte read);
8. sync destination;
9. close destination and source.

The app allocates no heap memory.

## Error/cleanup behavior

On failure, `cp` reports a short stage-specific message through the System ABI.
Open handles are closed on every normal error path.

Because Filesystem ABI v0 does not yet provide rename/remove, V1 cannot use an
atomic temporary-file replacement scheme. An interrupted or failed overwrite may
therefore leave the destination partially written. Do not add rename/remove only
for this workaround; those operations belong to the later file-management ABI
review where `mv` and `rm` provide direct use cases.

## Testing

### Host unit test

Task 5 adds:

```text
cp_copy_unit
```

Coverage should include:

- create new destination;
- overwrite existing destination;
- partial read + partial write behavior;
- binary data including embedded NUL;
- zero-length file;
- exact same-path rejection;
- missing source;
- directory source rejection;
- directory destination rejection;
- destination sync and handle cleanup.

### Hardware validation

On Tab5:

1. build `cp.elf` independently;
2. upload it through MFT1;
3. copy a text file and verify with `cat`;
4. copy a binary file and verify source/destination equality by transferring one
   copy back to the host or comparing host hashes;
5. overwrite an existing destination and verify new contents;
6. copy a zero-length file;
7. confirm Nano and Cat still run after Cp exits.

## Completion criteria

Task 5 is complete when:

- `cp.elf` builds independently and loads through the existing runtime;
- host `cp_copy_unit` passes;
- text and binary file copies pass on real Tab5 hardware;
- overwrite and zero-length behavior pass;
- partial-I/O behavior is covered by host tests;
- shell/app lifecycle remains clean;
- no public ABI expansion is required.
