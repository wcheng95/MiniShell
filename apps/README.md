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

The maintained targets are Linux Mint and Cardputer ADV. The same application source may be packaged differently on each target; loader/container format is not part of the application contract.

Code-facing application names are lowercase. Project names may remain proper-case in prose; for example, the MiniFT8 project provides runtime app `ft8` from `apps/ft8/`.

Protocol selection is application selection. The current implementation provides `ft8`; future protocol apps such as `ft4`, `cw`, `rtty`, and `js8` are added only when their real implementation begins.

On Linux, the reference build produces runtime-loadable `.so` modules under:

```text
build-linux/runtime/apps/
```

Cardputer ADV V1 proved the application model with selected applications compiled into the firmware through a static registry. Runtime ELF loading is now an active target.

ADV application resolution is:

```text
1. compiled-in application
2. /flash/<app>.elf
3. /sd/<app>.elf
```

The same external ELF may be installed in either filesystem location. If both external copies exist, `/flash/<app>.elf` wins.

For Keyer, both are valid:

```text
/flash/keyer.elf
/sd/keyer.elf
```

`/sd/keyer.elf` is convenient for development/removable distribution; copying the same binary to `/flash/keyer.elf` must work without rebuilding it.

Application discovery/loading remains a private backend/runtime responsibility. Application source must use only the MiniShell public API and its own modules; it must not include POSIX, ESP-IDF, FreeRTOS, M5/Cardputer, board-driver, or loader-specific interfaces.

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

Command-style utilities such as `date`, `ls`, `cat`, `cp`, `df`, `free`, `mkdir`, `mv`, `rm`, and `rmdir` use Console. `hello`, `nano`, and `ft8` use Display/Input where appropriate.

Applications use MiniShell logical paths such as `/sd/notes.txt`; they do not know the host filesystem path or FATFS implementation behind that namespace.

Persistent application settings use the canonical namespace:

```text
/flash/<app>/setting.txt
```

The application owns the meaning of those settings; MiniShell Filesystem owns path/handle/storage semantics.

The public API is still under active development. External `.elf` work does **not** freeze a long-term binary ABI yet. During this phase an external application may need to be rebuilt for the matching MiniShell API generation.
