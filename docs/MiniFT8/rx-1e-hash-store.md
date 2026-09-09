# MiniFT8-V3 RX-1E — Explicit Callsign Hash Store

Status: **COMPLETE**

RX-1E moves hashed-callsign knowledge out of MiniFT8-V2 global/static storage and into an explicit MiniFT8-owned `Ft8HashStore` instance suitable for ownership by one future `Ft8Engine` instance.

This remains a structural ownership stage. It does not migrate message unpacking/rendering, does not change FT8 callsign-hash mathematics, and does not introduce AutoSeq, UI, MiniShell, storage, or platform dependencies.

## 1. Scope

Implemented production boundary:

```text
validated FT8 payload
      |
      | later RX-1F message codec
      v
Ft8HashStore
    save full 22-bit callsign knowledge
    lookup 22 / 12 / 10-bit hashes
    refresh age on hit/save
    age once per FT8 slot
    trim oldest entries
```

RX-1E establishes the state owner before RX-1F connects the protocol message codec to it.

## 2. Source

Implemented in:

```text
apps/ft8/src/ft8_engine/ft8_hash_store.h
apps/ft8/src/ft8_engine/ft8_hash_store.c
```

Unit coverage:

```text
tests/ft8_hash_store_rx1e_test.c
```

The implementation has no dependency on:

```text
MiniShell
Linux
ESP-IDF
NuttX
UI
storage
AutoSeq
TX
V2 ftx_message_t
V2 ftx_callsign_hash_interface_t
```

## 3. Ownership

V2 production storage is a process-global/static table.

RX-1E replaces that ownership with:

```text
future Ft8Engine instance A
    `-- Ft8HashStore A

future Ft8Engine instance B
    `-- Ft8HashStore B
```

The store contains all mutable callsign-hash state. No global table or hidden singleton remains in the cleaned module.

The current RX stages do not yet need a full aggregate `Ft8Engine` object, so RX-1E does not create one merely to hold this member. The `Ft8HashStore` instance is explicitly caller-owned now and is intended to become one member of the assembled engine state when that aggregate is introduced.

Two-store independence is covered directly by the unit test.

## 4. Capacity and compact storage

The pinned MiniFT8-V2 production baseline uses:

```text
capacity        128 entries
callsign        up to 11 characters + NUL
entry storage    16 bytes
```

RX-1E preserves this compact representation:

```text
char callsign[12]
uint32_t hash_age
```

Inside `hash_age`:

```text
bits  0..21   canonical 22-bit FT8 callsign hash
bits 22..23   unused
bits 24..31   age, 0..255
```

A compile-time assertion keeps each entry at 16 bytes.

Therefore the 128 entries consume exactly:

```text
128 x 16 = 2048 bytes
```

plus the small store count/alignment field.

This preserves the V2 Cardputer-oriented memory choice without exposing platform behavior to the protocol module.

## 5. Explicit API

The MiniFT8-owned hash widths are:

```text
FT8_HASH_22_BITS
FT8_HASH_12_BITS
FT8_HASH_10_BITS
```

The store exposes:

```text
init / clear
age_slot
save
lookup
trim
count
```

Important ownership/lifecycle rule:

> `ft8_hash_store_age_slot()` is an explicit lifecycle operation and must be invoked exactly once per FT8 slot by the eventual engine/slot coordinator.

The hash store itself does not read time, UTC, RTC, or slot counters.

This removes V2's hidden dependency on a global `s_last_aged_slot` guard in `main.cpp` while preserving the age algorithm itself.

## 6. V2 hash lookup behavior preserved

A saved entry always stores the full 22-bit FT8 callsign hash.

For lookup:

```text
22-bit request -> compare full stored 22 bits
12-bit request -> compare stored_hash >> 10
10-bit request -> compare stored_hash >> 12
```

The start bucket is derived from the top 10 bits exactly as in V2:

```text
bucket = (hash10 * 23) % 128
```

where:

```text
10-bit lookup: hash10 = requested hash
12-bit lookup: hash10 = requested hash >> 2
22-bit lookup: hash10 = requested hash >> 12
```

This matters because 12/10-bit lookups must enter the same linear-probe region as the original full 22-bit save.

## 7. Trim holes and full-table probing

V2 trimming clears entries in place rather than rehashing later entries. That can create holes inside a linear-probe chain.

Therefore lookup must **not** stop at the first empty bucket.

RX-1E preserves the production V2 fix:

```text
start at derived bucket
scan all 128 positions
skip empty holes
return first matching shortened/full hash
```

The unit test deliberately creates two hashes with the same start bucket, trims away the first entry, and verifies that the second remains resolvable through the hole.

This behavior is important enough to keep as a direct regression.

## 8. Age semantics

Age remains an 8-bit saturating counter.

Explicit slot aging:

```text
occupied entry age < 255 -> age + 1
age == 255               -> remain 255
```

Age resets to zero when:

```text
a matching callsign/hash is saved again
an existing full 22-bit hash is replaced by a new callsign
a 22/12/10-bit lookup succeeds
```

The unit test verifies successful lookup refresh by making one entry older, looking it up, then trimming to one entry and proving the refreshed entry survives.

## 9. Full-hash collision behavior

V2 treats an identical 22-bit hash as one table identity.

If the same 22-bit hash is saved with:

```text
same callsign      -> refresh age
new callsign       -> replace old callsign, refresh age
```

The entry count does not increase.

RX-1E preserves this behavior.

This is distinct from 12/10-bit shortened-hash ambiguity, where several stored 22-bit entries can share the shortened value and lookup returns the first matching entry in the V2 probe order.

## 10. Full-table policy

The pinned V2 production table is intentionally not allowed to remain completely full when another callsign must be inserted.

When count reaches 128 and a new save arrives:

```text
trim target = 128 - 50 = 78
insert new entry
final count = 79
```

RX-1E preserves this exact policy.

The unit test fills all 128 entries, ages them, saves one additional callsign, and requires the final count to be 79 with the new callsign resolvable.

No attempt was made to optimize or redesign eviction during this structural stage.

## 11. Interface cleanup versus V2

The production V2 algorithm is retained, but the ownership/interface is cleaned in several ways:

```text
V2 global table                 -> explicit Ft8HashStore * context
V2 global age side effect       -> explicit age_slot() lifecycle call
V2 bool lookup                  -> explicit MiniFT8 status
V2 raw output with hidden size  -> output pointer + capacity
V2 silent valid-domain assumptions -> explicit input validation
```

Valid FT8 callsigns remain limited to 11 stored characters, matching the V2/protocol limit used by the production table.

The store accepts a complete 22-bit hash from the protocol codec. RX-1E does not independently hash callsign text; the codec/hash mathematics remain RX-1F/shared-protocol work.

## 12. Unit tests

`ft8_hash_store_rx1e_unit` covers:

```text
empty-store behavior
independent store instances
22-bit lookup
12-bit lookup
10-bit lookup
same-22-bit replacement
lookup age refresh
oldest-entry trimming
probe-chain hole survival
128-entry capacity
V2 128 -> 78 -> 79 full-table policy
invalid callsign/hash/type/capacity inputs
```

Current Linux result:

```text
ft8_hash_store_rx1e_unit  PASS
Linux full suite          15/15 PASS
RX-1C pinned regression   PASS
RX-1D pinned regression   PASS
```

RX-1E has no external fixture because its state-machine/hash-table behavior is fully deterministic and independently unit-testable. The V2 production implementation at the pinned baseline is the algorithm/source reference.

## 13. RX-1E exit criteria

```text
[done] no global/static callsign table in cleaned module
[done] explicit caller-owned Ft8HashStore instance
[done] 128-entry V2 capacity preserved
[done] compact 16-byte V2 entry preserved
[done] full 22-bit hash is canonical stored value
[done] 22/12/10-bit lookup behavior preserved
[done] V2 bucket calculation preserved
[done] lookup scans through trim-created holes
[done] successful save/lookup refreshes age
[done] age is explicit once-per-slot lifecycle behavior
[done] age saturates at 255
[done] full-hash replacement behavior preserved
[done] V2 full-table trim policy preserved
[done] two instances proven independent
[done] no MiniShell/platform/UI/AutoSeq/TX dependency
[done] message unpack/rendering not pulled into RX-1E
```

## 14. Next stage

Next active stage:

**RX-1F — migrate the supported RX protocol message codec to typed MiniFT8-owned results and connect it to `Ft8HashStore`.**

RX-1F will consume validated payloads from RX-1D and use the explicit store from RX-1E for hashed-callsign resolution/saving. It must preserve supported V2 protocol behavior while replacing `ftx_message_t`, three-generic-field output, and global callback context with typed MiniFT8-owned protocol results.
