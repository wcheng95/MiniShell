# Historical ABI Foundation Note

This file is retained temporarily because older MiniShell documentation refers to it.

The current architectural policy is now defined in:

```text
../api/api-foundation.md
```

MiniShell currently defines a public **API**, not a frozen long-term MiniShell binary ABI.

The earlier append-only/backward-compatibility policy is no longer active. Breaking API changes are allowed while the architecture is still being established. A formal binary ABI and compatibility policy may be introduced later if independently built `.so` or `.elf` applications need to remain compatible across MiniShell releases.

The `docs/abi/` directory name and remaining ABI terminology are historical and may be cleaned up during the current ADV/backend portability work.
