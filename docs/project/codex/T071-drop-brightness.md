# T071 — Remove resident brightness; preserve startup

Status: REVIEW

## Architect intent

The architect has decided to drop MiniShell resident display brightness control.

T069 startup sequencing is hardware-accepted and must remain. The failed ADV brightness path is not worth further implementation effort.

Final resident settings scope after this task:

```text
SSID=<WebFS SoftAP name>
PW=<WebFS WPA2 passphrase>
startup=<MiniShell command>[;<MiniShell command>...]
```

`brightness=` is no longer a supported MiniShell setting.

## Objective

Remove all T069 brightness implementation, tests, private platform hooks, and canonical documentation while preserving the accepted `startup=` behavior exactly.

## Current context

Read before editing:

```text
AGENTS.md
docs/README.md
docs/architecture/configuration.md
docs/project/codex/T069-resident-startup-brightness.md
docs/project/codex/T070-adv-backlight-pwm.md
core/minishell_runtime.c
core/resident_settings.[ch]
core/platform_backend.h
platform/adv/adv_backend.c
platform/adv/adv_display.cpp
platform/linux/linux_backend.c
tests/resident_settings_test.c
tests/resident_boot_test.c
tests/adv_display_color_test.py
tests/adv_console_scrollback_test.py
tests/adv_webfs_settings_test.c
```

Hardware evidence:

```text
startup=       PASS
brightness=    FAIL / dropped by architect
```

## Architectural constraints

- Preserve `MINISHELL_API_VERSION 3`; do not edit `include/minishell/api.h`.
- Preserve T069 `startup=` parsing and sequencing semantics exactly.
- Preserve alias behavior, foreground blocking, failure-continue behavior, and prompt return.
- Preserve the 1,024-byte bounded resident settings file behavior.
- Preserve WebFS SSID/PW parsing and T068 editor behavior.
- Do not add any replacement brightness mechanism.
- Do not add an interactive brightness command.
- Keep changes limited to removing brightness-specific code/tests/docs.

## Implementation scope

Remove brightness-specific state and behavior from:

```text
core/resident_settings.[ch]
core/minishell_runtime.c
core/platform_backend.h
platform/adv/adv_backend.c
platform/adv/adv_display.cpp
platform/adv/adv_internal.h
platform/linux/linux_backend.c
tests/
README.md
docs/README.md
docs/architecture/configuration.md
```

Specific expectations:

1. `minishell_resident_settings_t` retains only startup state needed by T069.
2. The resident parser ignores `brightness=` as an unknown key after this task.
3. `apply_boot_settings()` executes startup only; no brightness platform call remains.
4. Remove the private `minishell_platform_display_brightness()` hook completely.
5. Remove ADV brightness wrapper/code and Linux no-op brightness code.
6. Remove brightness-only test stubs/assertions while retaining startup and existing display/scrollback coverage.
7. Keep the resident settings loader bounded and no-heap.
8. Update canonical docs so `brightness=` is not advertised as supported.

Historical task packets T069/T070 may retain their implementation/failure record, but add clear final-state notes:

- T069: startup accepted; brightness portion superseded/removed by T071.
- T070: cancelled; no implementation required.

## Non-goals

- startup redesign;
- relative paths / `cd` / `pwd`;
- `cp` / `mv` destination semantics;
- editable history;
- pathname completion / Tab choices;
- `clear`;
- display dimming/sleep policy;
- RGB LED work;
- M5 library changes;
- any other resident setting.

## Acceptance criteria

- [ ] `startup=ft8;b` behavior is unchanged.
- [ ] Missing/empty startup still does nothing.
- [ ] Startup aliases still use the normal alias path.
- [ ] Failed startup commands still continue to later commands.
- [ ] Foreground apps still block later startup commands until exit.
- [ ] Prompt appears normally after startup sequence.
- [ ] `brightness=` has no MiniShell runtime effect and is treated as an unknown resident key.
- [ ] No brightness private platform hook remains.
- [ ] No brightness-specific ADV display code remains.
- [ ] Public API is unchanged.
- [ ] WebFS SSID/PW behavior remains unchanged.
- [ ] Full Linux CTest passes.
- [ ] ADV firmware builds successfully.

## Automated tests

Update focused resident tests so they cover startup-only behavior and confirm an unrelated `brightness=` line is harmless/ignored.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
idf.py -C platform/adv build
git diff --check
```

Also run focused:

```text
resident_settings_unit
resident_boot_unit
linux_startup
adv_webfs_settings_unit
adv_display_color
adv_console_scrollback
```

## Manual / hardware validation

After supervisor review, only re-confirm the accepted startup path on ADV:

```text
startup=ft8;b
```

Expected: FT8 starts during boot, exiting FT8 runs `b`, then normal `M$>` prompt appears.

No brightness hardware validation is required.

## Codex branch / handoff

Work on:

```text
codex/T071-drop-brightness
```

Start from current `main`.

Before coding, read `AGENTS.md`, `docs/README.md`, T069, T070, and this task packet.

Keep T071 to one reviewable implementation commit. Do not merge to `main` and do not open a PR unless asked.

## Codex implementation notes

### Implementation summary

Implemented from current `origin/main` at
`7f6b8d3175014ff480231b430874a8ee1576e053` on `codex/T071-drop-brightness`.
Removed the resident brightness field, decimal parser, boot call, private platform
hook, ADV wrapper/display mapping and Linux no-op. Resident settings now retain
only the existing startup buffer. No replacement brightness mechanism or scope
deviation.

### Files changed

- `core/resident_settings.[ch]`, `core/minishell_runtime.c`,
  `core/platform_backend.h`: remove brightness state, parsing, call and hook.
- `platform/adv/adv_backend.c`, `adv_internal.h`, `adv_display.cpp`, and
  `platform/linux/linux_backend.c`: remove brightness implementations/declaration.
- `tests/resident_settings_test.c`, `resident_boot_test.c`: remove brightness
  assertions/stubs; retain startup and loader regressions, and verify retired
  `brightness=` lines are harmless unknown keys.
- `tests/adv_display_color_test.py`, `adv_console_scrollback_test.py`: remove
  brightness-only M5 stubs/assertions; retain all existing display/scrollback checks.
- `tests/adv_webfs_settings_test.c`: retain combined startup/credential coverage.
- `README.md`, `docs/README.md`, `docs/architecture/configuration.md`: remove
  brightness from supported settings and examples.
- T069/T070 task packets: record startup acceptance/brightness supersession and
  T070 cancellation; historical implementation/failure evidence remains.
- This task packet: implementation and validation evidence.

### Invariants preserved

The startup parser branch and complete bounded FS loader remain unchanged except
for removal of the preceding brightness branch. File limit remains 1,024 bytes,
with exact-limit EOF probing, no heap allocation, and no prefix execution on
read/close/oversize failure. Parsed settings state shrinks from 1,028 to 1,024 bytes.

`core/shell.[ch]`, aliases, synchronous app execution, diagnostics, startup exit
handling and prompt return are unchanged. No public API changes; WebFS production
code, SSID/PW rules and T068 editor are unchanged. No display dimming/sleep,
PWM, RGB, or M5 library changes. Remaining brightness strings in resident/Linux
startup tests exercise ignored legacy keys; no production brightness hook remains.

### Local tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure -R 'resident_|linux_startup|adv_webfs_settings|adv_display_color|adv_console_scrollback'
# PASS: 6/6 required focused tests.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: 122/122 (59.21 seconds).
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: real firmware 0x152360 bytes; 78% app partition free. No flashing.
git diff --check
# PASS
```

ADV build retained SDK/M5/ELF-loader pedantic warnings and completed successfully.

Brightness-only validation is deliberately retired per T071, not relaxed to
hide a product failure. Startup tests retain missing/empty values, aliases and
live reload, app-return ordering, failure continuation, prompt behavior,
size bounds, partial reads, failed reads/closes and NUL handling. Existing Linux
startup integration still runs with legacy brightness lines, now ignored.

### Manual/hardware validation still required

After supervisor review, re-confirm only `startup=ft8;b` on ADV: FT8 starts
at boot, exiting FT8 executes `b`, then the normal interactive prompt appears.
No device flashed or hardware tested here. No brightness validation is required.

### Known limitations / risks

Existing startup file/command bounds and foreground blocking remain as accepted.
Legacy brightness lines need no migration; they are ignored, while still counting
toward the unchanged 1,024-byte file limit. Hardware startup re-check is pending.

### Commit

One implementation commit titled `T071: remove resident brightness and preserve startup`,
parent `7f6b8d3175014ff480231b430874a8ee1576e053`, on
`codex/T071-drop-brightness`. Exact pushed SHA is returned in the handoff.
No merge or PR.

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and local test evidence.

## Architect test result

Record final ADV startup re-check here.