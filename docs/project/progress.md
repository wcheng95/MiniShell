# MiniShell Progress Log

## Current baseline

- Linux Mint on `pc-1` is the reference/full production target.
- Cardputer ADV is the second real MiniShell backend.
- Linux runtime app discovery/loading uses `.so` modules.
- ADV V1 proved the same application model with a compiled-in registry; runtime external `.elf` loading is now the active next packaging target.
- ADV application resolution is: compiled-in first, then `/flash/<app>.elf`, then `/sd/<app>.elf`.
- The first planned field-usable ADV external application is `keyer.elf`, valid as either `/flash/keyer.elf` or `/sd/keyer.elf`.
- System, Console, Memory, Filesystem, Time/Location, Display, Input, and Audio public contracts have automated coverage. Digital I/O is not yet public and will be driven by the Keyer requirement.
- MiniFT8 is runtime app `ft8` and is FT8-only. Future Keyer/FT4/RTTY/JS8 functionality remains separate applications rather than an FT8-internal protocol selector.
- MiniFT8 RX integration has advanced through RX-7 on Linux, including the production decoded-UI golden test.
- The pre-Keyer architecture cleanup C0-C4 is complete.
- The application-facing contract is the **MiniShell API**. Backward source/binary compatibility is not frozen during this early architecture phase; external apps may need rebuilding for the matching API generation.

## Current priority

Review/finalize the Keyer plan, then begin the external application path:

```text
Keyer plan review
    |
    v
ADV runtime ELF loader proof
    |
    v
MiniShell Digital I/O
    |
    v
portable Keyer implementation
    |
    v
keyer.elf
    |-- /flash/keyer.elf
    `-- /sd/keyer.elf
```

Canonical configuration ownership is now fixed:

```text
/flash/config.txt
    MiniShell-owned resident/platform configuration

/flash/<app>/setting.txt
    application-owned configuration/deployment settings
```

Full rule:

```text
docs/architecture/configuration.md
```

Application settings may be hardware-specific. Keyer may own GPIO-number settings; MiniShell receives only generic Digital I/O operations and must not know `dit`, `dah`, paddle, or KeyOut semantics.

## Architecture cleanup — complete

```text
C0  application dependency/no-side-talk enforcement   COMPLETE
C1  opaque AppController                              COMPLETE
C2  ft8_main lifecycle/wiring only                    COMPLETE
C3  ADV ELF + compiled-in -> /flash -> /sd order     COMPLETE
C4  configuration ownership/naming                    COMPLETE
```

Canonical record:

```text
docs/project/architecture-cleanup.md
```

The code-bearing C0-C2 cleanup passed Linux build/CTest, strict units, AS-8, RX-1C through RX-7 including production RX7, and the Cardputer ADV ESP-IDF build. C3-C4 are architecture/documentation changes only.

## MiniFT8 configuration transition

The canonical application-settings namespace is `/flash/<app>/setting.txt`.

MiniFT8 currently still uses:

```text
/flash/ft8/station.txt
```

A later MiniFT8 migration may move this to:

```text
/flash/ft8/setting.txt
```

That rename is not part of C4 and must not be mixed into the Keyer/ELF work. MiniFT8 continues to own the current `station.txt` contents until such a migration is deliberately implemented.

Keyer starts directly with:

```text
/flash/keyer/setting.txt
```

## MiniFT8 RX integration

The Linux RX reference path reaches the production application boundary. RX-7 has passed with the golden WAV and expected decoded UI output.

The reference workflow covers RX-sensitive changes through:

```text
RX-1C / RX-1D / RX-1G focused decoder references
RX-2 host decoder
RX-3 frontend
RX-4 slot framer
RX-5 pure assembly
RX-6 MiniShell Audio path
RX-7 production decoded UI
```

The C2 work also strengthened the workflow gate so FT8 main/UI/model-boundary changes run the reference suite when appropriate.

## V1 cross-backend/profile checkpoint — complete

The earlier platform/profile validation remains complete:

```text
Linux backend + DESKTOP presentation   PASS
Linux backend + ADV presentation       PASS
ADV backend   + ADV presentation       PASS
```

Canonical historical record:

```text
docs/project/adv-backend-plan.md
docs/MiniFT8/v1-validation.md
```

ADV V1 used static application composition deliberately. C3 adds runtime external `.elf` loading while preserving compiled-in applications as the first resolution tier. For external applications, `/flash` is searched before `/sd`.

## ADV current storage/runtime baseline

```text
/flash    FATFS on internal flash through wear levelling
/sd       optional FATFS on MicroSD
```

Application code sees only MiniShell logical paths and does not know FATFS, wear levelling, SPI, or board details.

Foreground applications run in a MiniShell-managed ADV application task rather than borrowing ESP-IDF `app_main` state directly.

## Housekeeping paydown — complete

The earlier H1-H5 architecture-audit debt remains resolved:

```text
H1 Linux backend split
H2 POSIX loader semantics removed from portable core
H3 Filesystem private helpers split while preserving one owner
H4 legacy Tab5 active tree removed and archived
H5 terminal ANSI/CSI + UTF-8 parser made stateful across reads
```

## Canonical plans/policy

```text
docs/project/architecture-cleanup.md
docs/keyer/README.md
docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/configuration.md
docs/architecture/resident-vs-app.md
docs/project/adv-backend-plan.md
docs/MiniFT8/v1-validation.md
docs/MiniFT8/development.md
docs/MiniFT8/rx.md
docs/api/api-foundation.md
platform/adv/README.md
```
