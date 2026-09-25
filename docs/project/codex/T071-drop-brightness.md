# T071 — Remove resident brightness; preserve startup

Status: READY

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

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor fills this after reviewing the actual `main..<commit>` diff and local test evidence.

## Architect test result

Record final ADV startup re-check here.