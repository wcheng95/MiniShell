# MiniShell Applications

This directory contains applications that use only the public MiniShell API.

```text
application source
      |
      v
MiniShell API
      |
      v
MiniShell runtime + platform backend
```

The same source can be built for different MiniShell targets. Loader/container format is platform-specific and is not part of the application contract.

On Linux, the reference build produces runtime-loadable `.so` modules under:

```text
build-linux/runtime/apps/
```

Cardputer ADV V1 will compile selected applications into the firmware through a static registry. Runtime `.elf` loading may be explored later without changing the application source-level API model.

Current applications:

```text
hello    minimal API example
cat      text-file display
cp       binary-safe file copy
date     show/set MiniShell UTC
df       MiniShell storage usage
free     MiniShell memory usage
ls       directory listing
mkdir    create one directory
mv       regular-file rename
nano     interactive text editor
rm       remove one regular file
rmdir    remove one empty directory
```

Applications use MiniShell logical paths such as `/sd/notes.txt`; they do not know the host filesystem path behind that namespace.

A portable application must not depend on POSIX, NuttX, ESP-IDF, FreeRTOS, or board-specific types.

The public API is still under active development. In-tree applications are rebuilt when the API changes; backward source or binary compatibility is not yet promised.
