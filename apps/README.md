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

Code-facing application names are lowercase. Project names may remain proper-case in prose; for example, the MiniFT8 project currently provides runtime app `ft8` from `apps/ft8/`.

Protocol selection is application selection. The current implementation provides `ft8`; future protocol apps such as `ft4`, `cw`, `rtty`, and `js8` are added only when their real implementation begins.

On Linux, the reference build produces runtime-loadable `.so` modules under:

```text
build-linux/runtime/apps/
```

Cardputer ADV V1 will compile selected applications into the firmware through a static registry. Runtime `.elf` loading may be explored later without changing the application source-level API model.

Current applications:

```text
ft8      MiniFT8 FT8 application
hello    minimal foreground Display/Input lifecycle example
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

`hello` is intentionally a normal foreground application rather than a diagnostic print. It owns the application Display/Input surfaces while running, remains visible until the user presses `q`, Enter, or Escape, and then returns those surfaces to the resident shell. It does not use `System.write()` for user-facing output.

Applications use MiniShell logical paths such as `/sd/notes.txt`; they do not know the host filesystem path behind that namespace.

A portable application must not depend on POSIX, NuttX, ESP-IDF, FreeRTOS, or board-specific types.

The public API is still under active development. In-tree applications are rebuilt when the API changes; backward source or binary compatibility is not yet promised.
