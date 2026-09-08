# MiniShell API Foundation

Status: **current public application contract; compatibility not frozen**

MiniShell provides a platform-neutral **API** between applications and resident MiniShell services.

## Current policy

MiniShell is still in active architectural development. The public API should be optimized for clarity, ownership, portability, and real application needs before compatibility is frozen.

Therefore:

- breaking API changes are allowed when they improve the architecture;
- backward source compatibility is **not** currently guaranteed;
- backward binary compatibility is **not** currently guaranteed;
- applications built from the MiniShell tree are expected to be rebuilt against the matching API when the API changes;
- Linux `.so` packaging does not, by itself, imply a stable MiniShell binary ABI;
- Cardputer ADV V1 may compile MiniShell and applications together statically;
- a formal MiniShell ABI may be introduced later if independently built `.so` or `.elf` applications need to remain compatible across MiniShell releases.

Compatibility should be frozen deliberately, not accidentally.

## Public dependency direction

```text
application
    -> MiniShell public API
    -> resident MiniShell services
    -> private platform/backend boundary
    -> OS / RTOS / SDK / drivers / hardware
```

Applications never depend directly on Linux, ESP-IDF, NuttX, board support code, or backend-private types.

## API design rules

Even without a backward-compatibility promise, the API keeps disciplined contracts:

- explicit ownership and lifetime;
- platform-neutral types and errors;
- capability reporting for optional services/features;
- synchronous calls unless explicitly documented otherwise;
- caller-owned buffers unless explicitly documented otherwise;
- one authoritative owner for mutable resources/state;
- application-visible behavior defined by MiniShell, not by a particular backend.

`struct_size` fields and capability bits may remain useful for validation, optional features, testing, and future evolution. Their presence does **not** currently promise append-only or permanent binary compatibility.

## API versus ABI

For current architecture discussions, use **MiniShell API**.

A machine/toolchain always has an underlying binary calling convention, but MiniShell does not currently define a separate long-term binary compatibility promise of its own.

If future runtime-loaded applications need to survive MiniShell upgrades without recompilation, define that compatibility contract explicitly at that time. It may include:

```text
symbol names
calling convention assumptions
structure layout and field offsets
integer/pointer sizes
version negotiation
compatible evolution rules
```

That future contract may then be called the **MiniShell ABI**.

## Verification

```text
unit/service tests (primary)
    -> application integration tests
    -> cross-backend validation
    -> platform/hardware validation
```

Linux remains the reference behavior. ADV provides the first materially different embedded backend to test whether the API boundary is genuinely platform-independent.
