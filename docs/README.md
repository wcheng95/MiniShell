# MiniShell Documentation Map

Canonical priority:

1. `include/minishell/api.h`
2. `docs/api/api-foundation.md`
3. service contracts under `docs/api/`
4. architecture/design-principle docs
5. runtime/application placement docs
6. application docs (`docs/MiniFT8/`, `docs/keyer/`, etc.)
7. project audit/progress records

Current public API covers App, System, Console, Memory, Filesystem, Time/Location, Display, Input, Audio, and Digital I/O.

MiniShell's public contract is currently an **API**, not a frozen long-term binary ABI. Breaking API changes are allowed while the architecture is still being established. Backward source or binary compatibility will be introduced deliberately only when independently built applications make it necessary.

Current service contracts:

```text
docs/api/api-foundation.md
docs/api/app-api.md
docs/api/system-api.md
docs/api/console-api.md
docs/api/memory-api.md
docs/api/filesystem-api.md
docs/api/time-location-api.md
docs/api/display-api.md
docs/api/input-api.md
docs/api/audio-api.md
docs/api/digital-io-api.md
```

Canonical architecture rules:

```text
docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/resident-vs-app.md
docs/architecture/configuration.md
```

Configuration ownership is:

```text
/flash/config.txt          MiniShell-owned resident/platform configuration
/flash/<app>/setting.txt   application-owned configuration/deployment settings
```

Hardware-specific application settings are allowed. An application such as Keyer may own GPIO-number settings and request generic MiniShell Digital I/O operations; MiniShell must not interpret Keyer-domain meaning.

Application and exploratory notes:

```text
docs/MiniFT8/
docs/keyer/      # Keyer porting + ADV external-ELF field-application plan
docs/js8/       # JS8 DSP research and future JS8Chat project
```

Completed architecture cleanup record:

```text
docs/project/architecture-cleanup.md   # C0-C4 complete
```

ADV application resolution is compiled-in first, then `/flash/apps/<app>.elf`, then `/sd/apps/<app>.elf`.

The maintained MiniShell targets are Linux Mint on `pc-1` and Cardputer ADV.
