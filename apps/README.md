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

Cardputer ADV V1 proved the application model with selected applications compiled into the firmware through a static registry. That remains a valid transition/testing mechanism, but runtime ELF loading is now an **active MiniShell target**, not a future experiment.

The initial ADV external-application convention is:

```text
/sd/<app>.elf
```

The first field-usable target is:

```text
/sd/keyer.elf
```

Application discovery/loading remains a private backend/runtime responsibility. The Keyer source must use only the same MiniShell public API boundary as any other portable application; it must not include ESP-IDF, FreeRTOS, M5/Cardputer, or loader-specific interfaces. Static and external applications may coexist while the loader is brought up. Collision/precedence behavior between a compiled-in app and an external app of the same name is intentionally not frozen yet.

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

`keyer` is planned next as the first field-usable ADV external application. It is not listed as a current application until implementation begins.

Application output is chosen by intent:

```text
Console.write()   command-style results, usage, and user-visible errors
Display           interactive/full-screen application UI
System.write()    diagnostics/debugging
```

Command-style utilities such as `date`, `ls`, `cat`, `cp`, `df`, `free`, `mkdir`, `mv`, `rm`, and `rmdir` use Console. This lets the same utility print naturally in the Linux terminal and in the Cardputer ADV resident text console without becoming a fake full-screen app.

`hello` is intentionally a normal foreground application rather than a console utility. It owns the application Display/Input surfaces while running, remains visible until the user presses `q`, Enter, or Escape, and then returns those surfaces to the resident shell. It does not use `System.write()` or Console for its user-facing UI.

Applications use MiniShell logical paths such as `/sd/notes.txt`; they do not know the host filesystem path behind that namespace.

A portable application must not depend on POSIX, NuttX, ESP-IDF, FreeRTOS, or board-specific types.

The public API is still under active development. External `.elf` work does **not** freeze a long-term binary ABI yet. During this phase an external application may need to be rebuilt for the matching MiniShell API generation; backward source or binary compatibility is not yet promised.
