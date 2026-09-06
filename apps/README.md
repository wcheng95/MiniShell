# MiniShell Applications

This directory contains applications that use only the public MiniShell ABI.

Applications are intentionally separated from the resident MiniShell runtime:

```text
application source
      |
      v
MiniShell ABI
      |
      v
MiniShell runtime + platform backend
```

The same application source can be built for different MiniShell targets. The loader/container format is platform-specific and is not part of the application contract.

On Linux, the reference build produces runtime-loadable `.so` modules under:

```text
build-linux/runtime/apps/
```

Current applications:

```text
hello    minimal ABI example
cat      simple text-file display utility
cp       binary-safe file copy utility
mv       no-overwrite regular-file rename
rm       remove one regular file
mkdir    create one directory
rmdir    remove one empty directory
nano     interactive text editor
```

Applications use MiniShell paths such as `/sd/notes.txt`; they do not know the host filesystem path behind that namespace.

A portable application must not depend on POSIX, NuttX, ESP-IDF, FreeRTOS, or board-specific types. Platform-specific applications are possible, but they must be explicitly documented as such.
