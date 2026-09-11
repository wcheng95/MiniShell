# MiniShell Progress Log

## Current baseline

- Linux Mint on `pc-1` is the reference/full production target.
- Cardputer ADV is the second real MiniShell backend.
- Linux runtime app discovery/loading uses `.so` modules.
- ADV now has hardware-validated runtime external `.elf` loading using Espressif's ELF loader.
- ADV application resolution is: compiled-in first, then `/flash/apps/<app>.elf`, then `/sd/apps/<app>.elf`.
- K1 proved a real Xtensa `elfhello.elf` from both SD and internal flash, including MiniShell API resolution, clean return/unload, repeated execution, and flash priority when both external copies exist. Canonical external app directories are now `/flash/apps` and `/sd/apps`.
- The first planned field-usable ADV external application is `keyer.elf`, valid as either `/flash/apps/keyer.elf` or `/sd/apps/keyer.elf`.
- System, Console, Memory, Filesystem, Time/Location, Display, Input, and Audio public contracts have automated coverage. Digital I/O is not yet public and is now the active Keyer requirement.
- MiniFT8 is runtime app `ft8` and is FT8-only. Future Keyer/FT4/RTTY/JS8 functionality remains separate applications rather than an FT8-internal protocol selector.
- MiniFT8 RX integration has advanced through RX-7 on Linux, including the production decoded-UI golden test.
- The pre-Keyer architecture cleanup C0-C4 is complete.
- The application-facing contract is the **MiniShell API**. Backward source/binary compatibility is not frozen during this early architecture phase; external apps may need rebuilding for the matching API generation.

## Current priority

K0 and K1 are complete. Begin K2, the generic MiniShell Digital I/O service needed by Keyer:

```text
K0 architecture gate          COMPLETE
        |
        v
K1 ADV runtime ELF proof      COMPLETE
        |
        v
K2 MiniShell Digital I/O      NEXT
        |
        v
K3 portable Keyer engine
        |
        v
...
        |
        v
keyer.elf
    |-- /flash/apps/keyer.elf
    `-- /sd/apps/keyer.elf
```

Canonical configuration ownership is fixed:

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

## Keyer runtime ELF proof — K1 complete

Cardputer ADV hardware validated the non-colliding `elfhello.elf` runtime application.

Canonical path after the K1 directory cleanup is:

```text
/sd/apps/elfhello.elf
    -> discovered by apps
    -> Xtensa ELF load/relocation
    -> mini_api_get()
    -> MiniShell Console API
    -> return
    -> unload
    -> M$>
```

The exact same ELF may be copied to `/flash/apps/elfhello.elf`; with both external copies present MiniShell selects the flash copy. Static-registry tests protect the complete rule:

```text
compiled-in > /flash/apps > /sd/apps
```

Repeated SD executions showed stable executable-heap behavior. A representative run was:

```text
before load: exec free=300828, largest=286720
after unload: exec free=301984, largest=286720
```

No K1 loader leak was observed.

ESP-IDF 5.5.1 uses `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE`. Cardputer ADV has no PSRAM, so Espressif's ELF loader needs executable internal SRAM for relocated `.text`; ADV therefore disables `MEMPROT_FEATURE`. With it enabled the capability allocator exposes zero `MALLOC_CAP_EXEC` heap and even the tiny 92-byte `elfhello` text section cannot load. The ADV build has a compile-time guard for this requirement.

External ADV applications are trusted code, not sandboxed applications. The ELF loader exports only `mini_api_get`; broad libc and ESP-IDF symbol tables are disabled so portable application services flow through the MiniShell API table.

## Architecture cleanup — complete

```text
C0  application dependency/no-side-talk enforcement   COMPLETE
C1  opaque AppController                              COMPLETE
C2  ft8_main lifecycle/wiring only                    COMPLETE
C3  ADV ELF + compiled-in -> /flash/apps -> /sd/apps  COMPLETE
C4  configuration ownership/naming                    COMPLETE
```

Canonical record:

```text
docs/project/architecture-cleanup.md
```

The code-bearing C0-C2 cleanup passed Linux build/CTest, strict units, AS-8, RX-1C through RX-7 including production RX7, and the Cardputer ADV ESP-IDF build. C3-C4 established the architecture that K1 has now validated on actual ADV hardware.

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

ADV V1 used static application composition deliberately. C3 added runtime external `.elf` loading while preserving compiled-in applications as the first resolution tier. K1 has now validated the external flash and SD tiers on actual Cardputer ADV hardware; binaries are searched only under `/flash/apps` and `/sd/apps`.

## ADV current storage/runtime baseline

```text
/flash    FATFS on internal flash through wear levelling
/sd       optional FATFS on MicroSD
```

External ADV application binaries are kept under:

```text
/flash/apps
/sd/apps
```

Application code sees only MiniShell logical paths and does not know FATFS, wear levelling, SPI, or board details.

Foreground applications run in a MiniShell-managed ADV application task rather than borrowing ESP-IDF `app_main` state directly.

## Housekeeping paydown — complete

The earlier H1-H5 architecture-audit debt remains resolved:

```text
H1 Linux backend split
H2 POSIX loader semantics removed from portable core
H3 Filesystem private helpers split while preserving one owner
H4 legacy Tab5 active tree removed
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
