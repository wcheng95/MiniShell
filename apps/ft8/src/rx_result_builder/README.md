# rx_result_builder

`rx_result_builder` is the MiniFT8 application boundary immediately above the
pure `Ft8Engine` protocol output.

It converts one typed `Ft8ProtocolSlot` into a caller-owned `RxBatch` without
adding QSO or TX policy.

Owned responsibilities:

```text
Ft8ProtocolSlot
    -> copy exact payload identity
    -> preserve protocol/parse status
    -> preserve factual decoder diagnostics
    -> expose type-dependent call fields
    -> classify factual is_cq
    -> classify factual is_to_me using caller-provided local callsign
    -> RxBatch
```

It does **not** own:

```text
reply selection
AutoSeq state
TX stage / TX armed state
IgnoreList policy
UI ordering/coloring
logging policy
MiniShell services
clock/timing
memory allocation
```

The builder has no hidden allocator and no process-global parser state. Output
storage is supplied by the caller.

## FREE_TEXT CQ exception

The locked MiniFT8-V3 rule is preserved: protocol type remains `FREE_TEXT`, but
a message is additionally classified as logical CQ only when its canonical text
matches:

```text
CQ <nnn|AAAA> <valid-callsign> [grid]
```

For example:

```text
CQ POTA K7XYZ     -> logical CQ
CQ HELLO WORLD    -> not logical CQ
```

This is factual classification only. It does not imply that MiniFT8 should
reply.
