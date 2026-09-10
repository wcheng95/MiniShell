# MiniShell Documentation Map

Canonical priority:

1. `include/minishell/api.h`
2. `docs/api/api-foundation.md`
3. service contracts under `docs/api/`
4. architecture/design-principle docs
5. runtime/application placement docs
6. application docs (`docs/MiniFT8/`, `docs/keyer/`, etc.)
7. project audit/progress records

Current public API covers App, System, Memory, Filesystem, Time/Location, Display, Input, and Audio.

MiniShell's public contract is currently an **API**, not a frozen long-term binary ABI. Breaking API changes are allowed while the architecture is still being established. Backward source or binary compatibility will be introduced deliberately only when independently built applications make it necessary.

Current service contracts:

```text
docs/api/api-foundation.md
docs/api/app-api.md
docs/api/system-api.md
docs/api/memory-api.md
docs/api/filesystem-api.md
docs/api/time-location-api.md
docs/api/display-api.md
docs/api/input-api.md
docs/api/audio-api.md
```

Application and exploratory notes:

```text
docs/MiniFT8/
docs/keyer/      # Keyer porting + ADV external-ELF field-application plan
docs/js8/       # JS8 DSP research and future JS8Chat project
```

Current architecture/project planning:

```text
docs/project/architecture-cleanup.md   # pre-Keyer modularity/no-side-talk gate
```

ADV application resolution is compiled-in first, then `/flash/<app>.elf`, then `/sd/<app>.elf`.

Linux Mint on `pc-1` is the reference/full production target. Historical Tab5/ESP-IDF work is retained on `archive/tab5-legacy`.
