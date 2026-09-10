# MiniShell Progress Log

## Current baseline

- Linux Mint on `pc-1` is the reference/full production target.
- Cardputer ADV is the second real MiniShell backend.
- Linux runtime app discovery/loading uses `.so` modules.
- ADV V1 proved the same application model with a compiled-in registry; runtime `/sd/<app>.elf` is now the active next packaging target.
- The first planned field-usable ADV external application is `/sd/keyer.elf`.
- System, Console, Memory, Filesystem, Time/Location, Display, Input, and Audio public contracts have automated coverage. Digital I/O is not yet public and will be driven by the Keyer requirement.
- MiniFT8 is runtime app `ft8` and is FT8-only. Future Keyer/FT4/RTTY/JS8 functionality remains separate applications rather than an FT8-internal protocol selector.
- MiniFT8 RX integration has advanced through RX-7 on Linux, including the production decoded-UI golden test.
- MiniFT8 architecture cleanup C0-C3 is complete: dependency/no-side-talk enforcement, opaque `AppController`, lifecycle-only `ft8_main`, and active ADV runtime-ELF architecture direction.
- The application-facing contract is the **MiniShell API**. Backward source/binary compatibility is not frozen during this early architecture phase; external apps may need rebuilding for the matching API generation.

## Current priority

Finish the architecture-cleanup gate, then begin the external Keyer path:

```text
C4  configuration ownership/file naming
    |
    v
ADV runtime ELF loader bring-up
    |
    v
/sd/keyer.elf
```

C4 records the ownership split already agreed in design:

```text
/flash/config.txt
    MiniShell-owned resident/platform configuration

/flash/<app>/setting.txt
    application-owned configuration/deployment settings
```

After C4, Keyer becomes the first practical application to drive the remaining MiniShell facilities such as Digital I/O and real ADV audio output while also validating runtime ELF lifecycle in field use.

## Architecture cleanup

Current status:

```text
C0  application dependency/no-side-talk enforcement   COMPLETE
C1  opaque AppController                              COMPLETE
C2  ft8_main lifecycle/wiring only                    COMPLETE
C3  ADV runtime /sd/<app>.elf active target           COMPLETE
C4  configuration ownership/naming                    NEXT
```

Canonical plan:

```text
docs/project/architecture-cleanup.md
```

## MiniFT8 RX integration

The Linux RX reference path now reaches the production application boundary. RX-7 has passed with the golden WAV and expected decoded UI output.

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

The checkpoint proved:

```text
portable minishell_run()
private backend boundary
ADV display/keyboard services
ADV filesystem/time services
MiniFT8 shared source across Linux and ADV
ADV 20x7 presentation
foreground app lifecycle/return to M$>
platform-dependency boundary
```

ADV V1 used static application composition deliberately. C3 now makes runtime `/sd/<app>.elf` the active post-V1 direction while retaining static composition as a transition/testing mechanism.

## ADV current storage/runtime baseline

Current ADV storage namespace:

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
docs/architecture/resident-vs-app.md
docs/project/adv-backend-plan.md
docs/MiniFT8/v1-validation.md
docs/MiniFT8/development.md
docs/MiniFT8/rx.md
docs/api/api-foundation.md
platform/adv/README.md
```
