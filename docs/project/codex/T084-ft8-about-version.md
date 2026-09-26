# T084 — MiniFT8 About version label

Status: READY

## Intent

Expose the accepted MiniFT8-V3 baseline version on the existing read-only
`V -> 6 About` screen.

Current accepted runtime version:

```text
MiniFT8-V3.083
```

The `.083` suffix identifies the accepted T083 CAT-fault-containment baseline.

This task does **not** rename the architectural generation from "MiniFT8-V3" to
"MiniFT8-V3.083" throughout historical/design documentation. "V3" remains the
generation name; `MiniFT8-V3.083` is the current runtime/product version label.

Version bumps remain explicit architectural decisions. Do not automatically
change the version for every future T-number.

## Current state

`V -> 6 About` already exists.

Current About content begins with:

```text
MiniFT8-V3
MiniShell application
Runtime app: ft8
...
```

No new menu entry, submenu, navigation path, controller state, or platform API is
needed.

## Required change

Change the first About line to exactly:

```text
MiniFT8-V3.083
```

Preserve the rest of `V -> 6 About` behavior unless a test requires only the
minimal expected-string update.

Expected screen content:

```text
MiniFT8-V3.083
MiniShell application
Runtime app: ft8
FT8 protocol only
V is strictly read-only
```

Existing footer/back/quit behavior remains unchanged.

## Version ownership / four principles

### Modularity

The version label is MiniFT8 application metadata. Do not add MiniShell-platform
or ADV-specific version logic.

### Ownership

For T084 there is only one runtime consumer: `V -> 6 About`.

Keep one production source for the displayed string. Do not duplicate version
state across controller, configuration, platform, persisted files, or MiniShell
services.

A file-local constant or direct literal in the About renderer is acceptable and
preferred while there is only one consumer. Do **not** add a new public version
API or configuration key.

### Decoupling

The version label must not affect:

- RX/decode;
- CAT;
- Audio/UAC;
- AutoSeq;
- logging;
- configuration;
- application lifecycle.

### KISS

This should be a tiny UI/document/test change.

Do not add:

- a version parser;
- semantic-version machinery;
- build-number generation;
- Git SHA embedding;
- compile-time CMake version plumbing;
- a new controller/model field;
- a persisted version setting.

If another runtime consumer needs the version later, centralize then.

## Documentation

Update `docs/MiniFT8/ui.md` so `V -> 6 About` explicitly documents the current
first line as:

```text
MiniFT8-V3.083
```

Optionally add one concise note to the current MiniFT8 README/development baseline
that the displayed runtime version is `MiniFT8-V3.083`. Do not mass-replace
historical `MiniFT8-V3` wording.

## Tests

Update/add the narrow UI-shell assertion that entering:

```text
V -> 6
```

renders `MiniFT8-V3.083` on the first content row.

Preserve existing V-root entries:

```text
1 Memory >
2 GPS >
3 QSO / Log >
4 Performance >
5 System Info >
6 About >
```

Run the normal focused UI test plus:

```text
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T084-unit
cmake --build /tmp/T084-unit -j8
ctest --test-dir /tmp/T084-unit --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_platform_boundary.py . ft8
python3 tests/ft8_platform_boundary.py .
python3 tests/architecture_rules.py .

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

## Hardware acceptance

Minimal ADV confirmation:

```text
ft8
V
6
```

Verify first About row shows:

```text
MiniFT8-V3.083
```

and Back/Q behavior is unchanged.

## Non-goals

- no global repository rename from V3 to V3.083;
- no automatic task-number/version coupling;
- no new version API;
- no platform-specific behavior;
- no changes outside About/version documentation/tests unless strictly required.

## Codex branch / handoff

Work on:

```text
codex/T084-ft8-about-version
```

Start from current `main`.

Read:

```text
AGENTS.md
docs/README.md
docs/MiniFT8/ui.md
docs/MiniFT8/README.md
docs/project/codex/T084-ft8-about-version.md
```

Keep one small reviewable commit and record normal handoff evidence in this task
packet.
