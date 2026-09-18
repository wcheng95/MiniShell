# T011 — Narrow Linux runtime exports

Status: COMPLETE

## Objective

Resolve T001 F08: Linux runtime modules must see only the public MiniShell entry point, not private service/backend helpers.

Current Linux build uses:

```cmake
set_target_properties(minishell PROPERTIES ENABLE_EXPORTS ON)
```

which makes the executable's broad global symbol table visible to `dlopen()` modules.

Target invariant:

```text
external Linux app module
    may import: mini_api_get
    must not import: private MiniShell runtime/service/backend symbols
```

This is architectural enforcement for trusted apps, not a security-sandbox claim.

## Required design

Replace broad executable exporting with an explicit Linux export allow-list containing only:

```text
mini_api_get
```

Preferred direction on the current GNU/Linux toolchain:

- remove `ENABLE_EXPORTS ON`;
- use a linker mechanism that exports only `mini_api_get`, such as
  `-Wl,--export-dynamic-symbol=mini_api_get`, or an equivalent explicit export list/version-script approach;
- do not rely on source visibility attributes alone if they still leave the executable broadly exported.

Choose the smallest mechanism supported by the current Linux build and record the exact linker command/effect.

Do not change the ADV export model.

## Positive runtime requirement

All existing Linux runtime apps must continue to load normally through:

```text
dlopen(..., RTLD_NOW | RTLD_LOCAL)
```

and resolve:

```c
mini_api_get()
```

Existing smoke/service/audio/FT8 tests must remain functional.

## Negative runtime regression

Add a test-only module that deliberately imports a known private runtime symbol without including a private header, for example:

```c
extern void filesystem_handles_reset(void);
```

Its `main()` may reference/call that symbol so the relocation is retained.

Build it as a MODULE/shared runtime fixture with unresolved symbols permitted as normal for Linux shared objects.

Because the loader uses `RTLD_NOW`, attempting to run this fixture through MiniShell must fail at `dlopen()` due to the unresolved private symbol.

The test must prove:

1. the bad module exists and contains an undefined import for the chosen private symbol;
2. MiniShell refuses to load/run it;
3. a normal public-API module still loads successfully;
4. `mini_api_get` remains exported by the executable;
5. the chosen private symbol is not in the executable's dynamic export table.

Do not expose any private symbol merely to make the negative fixture load.

## Symbol-table regression

Add a focused host test, preferred:

```text
tests/linux_export_boundary.py
```

Use local toolchain utilities such as `nm -D` and/or `readelf -Ws`.

At minimum assert:

- `mini_api_get` is present in `build-linux/minishell` dynamic defined symbols;
- `filesystem_handles_reset` is absent;
- representative private families are absent, including at least:
  - `filesystem_handles_`
  - `filesystem_path_`
  - `filesystem_quota_`
  - `minishell_services_`
  - `minishell_*_service_`
  - `linux_` backend helpers where applicable;
- the intentionally bad fixture has the expected unresolved private import;
- the normal app fixture still imports/resolves `mini_api_get`.

Do not require the executable dynamic table to contain literally one symbol total; libc/toolchain-generated executable symbols may exist. The invariant is that project-private MiniShell implementation symbols are not exported, while `mini_api_get` is.

## Source/API scope

Expected production change should be very small:

```text
CMakeLists.txt
```

Expected test additions:

```text
tests/apps/private_import_probe.c
tests/linux_export_boundary.py
```

or equivalent narrowly scoped names.

Do not change:

- public MiniShell API layout/version;
- service implementations;
- Linux loader behavior;
- application source to avoid imports;
- ADV ELF loader/export table;
- architecture rules beyond this export boundary.

## Existing public declaration

`mini_api_get` is already declared with `MINI_IMPORT` in:

```text
include/minishell/api.h
```

Do not rename it or add another app entry point.

## Local test gate

Run locally:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux -R 'linux_export|linux_smoke|linux_services|linux_audio' --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer
python3 tests/ft8_platform_boundary.py .

nm -D --defined-only build-linux/minishell
readelf -Ws build-linux/minishell

cmake -S tests/unit -B /tmp/T011-build-unit
cmake --build /tmp/T011-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T011-build-unit --output-on-failure

git diff --check

ctest --test-dir build-linux --output-on-failure
```

The two accepted baseline failures `linux_audio` and `linux_ft8` may remain unless the export change reveals a real new failure. Do not modify those expectations in T011.

No GitHub Actions wait.

## Acceptance criteria

- [x] Linux executable no longer uses broad `ENABLE_EXPORTS ON`.
- [x] `mini_api_get` remains dynamically resolvable by runtime modules.
- [x] representative private MiniShell symbols are absent from dynamic exports.
- [x] deliberate private-import module fails at runtime load.
- [x] normal public-API module still loads/runs.
- [x] focused symbol-table regression is registered in root CTest.
- [x] no public API or loader behavior changes.
- [x] architecture checks pass.
- [x] unit suite passes.
- [x] full Linux suite result recorded.
- [x] no unrelated cleanup.

## Branch workflow

Use:

```text
codex/T011-linux-export-boundary
```

Before handoff:

1. set Status to REVIEW;
2. record exact linker mechanism and dynamic-symbol evidence;
3. run all local tests;
4. commit and push the bounded implementation;
5. return the commit SHA;
6. do not open a PR;
7. do not wait for GitHub Actions.

Supervisor reviews `main..<SHA>`. If clean, fast-forward/merge to `main`, then delete local and remote T011 branches.

## Codex implementation notes

### Implementation summary

Replaced broad Linux executable exports with an explicit `mini_api_get` allow-list.
Added a test-only private-import module and a root CTest regression covering dynamic
symbols, rejected private linkage, and successful public-API app execution.
No task deviations. Public API layout/version, services, application sources,
`dlopen(..., RTLD_NOW | RTLD_LOCAL)`, and ADV behavior are unchanged.

### Linux linker/export mechanism

Removed `ENABLE_EXPORTS ON`; added
`target_link_options(minishell PRIVATE "-Wl,--export-dynamic-symbol=mini_api_get")`.
The locally generated GNU link command in
`build-linux/CMakeFiles/minishell.dir/link.txt` is exactly:

```text
/usr/bin/cc -Wl,--export-dynamic-symbol=mini_api_get CMakeFiles/minishell.dir/core/minishell_runtime.c.o CMakeFiles/minishell.dir/core/shell.c.o CMakeFiles/minishell.dir/core/app_manager.c.o CMakeFiles/minishell.dir/core/minishell_services/services.c.o CMakeFiles/minishell.dir/core/minishell_services/system_service.c.o CMakeFiles/minishell.dir/core/minishell_services/console_service.c.o CMakeFiles/minishell.dir/core/minishell_services/memory_service.c.o CMakeFiles/minishell.dir/core/minishell_services/filesystem_service.c.o CMakeFiles/minishell.dir/core/minishell_services/filesystem_paths.c.o CMakeFiles/minishell.dir/core/minishell_services/filesystem_handles.c.o CMakeFiles/minishell.dir/core/minishell_services/filesystem_quota.c.o CMakeFiles/minishell.dir/core/minishell_services/time_location_service.c.o CMakeFiles/minishell.dir/core/minishell_services/display_service.c.o CMakeFiles/minishell.dir/core/minishell_services/input_service.c.o CMakeFiles/minishell.dir/core/minishell_services/audio_service.c.o CMakeFiles/minishell.dir/core/minishell_services/digital_io_service.c.o CMakeFiles/minishell.dir/platform/linux/main.c.o CMakeFiles/minishell.dir/platform/linux/linux_backend.c.o CMakeFiles/minishell.dir/platform/linux/linux_common.c.o CMakeFiles/minishell.dir/platform/linux/linux_console.c.o CMakeFiles/minishell.dir/platform/linux/linux_filesystem.c.o CMakeFiles/minishell.dir/platform/linux/linux_time_location.c.o CMakeFiles/minishell.dir/platform/linux/linux_terminal.c.o CMakeFiles/minishell.dir/platform/linux/linux_terminal_parser.c.o CMakeFiles/minishell.dir/platform/linux/linux_loader.c.o CMakeFiles/minishell.dir/platform/linux/linux_services.c.o CMakeFiles/minishell.dir/platform/linux/linux_audio_wav.c.o CMakeFiles/minishell.dir/platform/linux/linux_digital_io.c.o CMakeFiles/minishell.dir/platform/linux/platform_info.c.o CMakeFiles/minishell.dir/platform/linux/resource_config.c.o -o minishell  -ldl
```

There is no broad `-rdynamic`/`--export-dynamic` option. The linker retains private
symbols in the ordinary symbol table without exposing them to module resolution.

### Negative private-import regression

`private_import_probe.so` is built under `build-linux/test-apps`, outside production
app discovery. It declares and calls `filesystem_handles_reset` without a private
header; `nm -D --undefined-only` confirms the unresolved import. The test copies it
and the existing `hello.so` into an isolated temporary app directory, then launches
both through MiniShell. It requires the fixture-specific `app: dlopen ... undefined
symbol: filesystem_handles_reset` diagnostic and shell launch failure, followed by
exactly one hello greeting, the normal exit prompt, and no hello error.
The positive module's undefined `mini_api_get` import is checked explicitly.

### Dynamic symbol evidence

`nm -D --defined-only build-linux/minishell` returned:

```text
000000000000348c T mini_api_get
00000000000160a0 B stderr@GLIBC_2.2.5
0000000000016090 B stdin@GLIBC_2.2.5
0000000000016080 B stdout@GLIBC_2.2.5
```

`readelf -Ws build-linux/minishell` confirms `mini_api_get` in `.dynsym` and
`.symtab`; `filesystem_handles_reset` appears only in `.symtab` (address `5a96`).
The regression rejects dynamically exported `filesystem_handles_`,
`filesystem_path_`, `filesystem_quota_`, `minishell_services_`,
`minishell_*_service_`, and `linux_` families. Standard libc stream symbols are
allowed; the only project-defined dynamic export is `mini_api_get`.

### Files changed

- `CMakeLists.txt`: export allow-list, private fixture target, regression registration.
- `tests/apps/private_import_probe.c`: intentionally invalid runtime import.
- `tests/linux_export_boundary.py`: symbol and runtime regression.
- `docs/project/codex/T011-linux-export-boundary.md`: review status and handoff evidence.

### Local tests run and results

Executed locally:

```bash
git status --short
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux -R 'linux_export|linux_smoke|linux_services|linux_audio' --output-on-failure
python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer
python3 tests/ft8_platform_boundary.py .
nm -D --defined-only build-linux/minishell
readelf -Ws build-linux/minishell
cmake -S tests/unit -B /tmp/T011-build-unit
cmake --build /tmp/T011-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T011-build-unit --output-on-failure
git diff --check
ctest --test-dir build-linux --output-on-failure
```

Linux configure/build passed. Focused CTest: 4/5 passed, including the new export
regression, smoke, services, and audio discontinuity unit test. `linux_audio`
retains its accepted baseline failure: the expected frames/hash substring omits
the intervening rate/peak/mean diagnostics; both actual probe runs report PASS.
All three architecture checks passed. Unit configure/build and 14/14 tests passed.
Full Linux suite: 28/30 passed; only the accepted `linux_audio` and `linux_ft8`
baseline failures remain. FT8 loads and runs, then its stale queue-order assertion
times out waiting for `N5CH     RPLY 0/3` on the rotated first page.
`git diff --check` passed. No test expectations were changed.

### Known limitations / risks

This is architectural enforcement for trusted modules, not a security sandbox.
The linker option targets the current GNU/Linux toolchain; the regression requires
local `nm`. The two accepted baseline test failures remain outside T011 scope.
No hardware/manual validation is required for this Linux-only task. No GitHub
Actions wait or PR is required or performed.

### Commit

Single implementation commit on `codex/T011-linux-export-boundary`, titled
`T011: narrow Linux runtime exports`. The pushed commit SHA is returned in the
handoff; this section is part of that commit.

## Supervisor review

PASS. Reviewed `faa836a70cd3950d85f62e55337e99343c681658` against `main`. Linux no longer uses broad executable exports; the runtime explicitly exports `mini_api_get` only through the GNU linker export-dynamic-symbol mechanism. The negative module imports `filesystem_handles_reset` without a private header and fails at `dlopen(RTLD_NOW)`, while the normal hello module still resolves `mini_api_get` and runs. Dynamic-symbol checks confirm representative private service/backend families are absent from executable exports.

Local export regression, architecture checks, unit suite 14/14, and full Linux 28/30 results are accepted with only the two known baseline failures. Commit was fast-forwarded directly to `main`.

## Architect test result

ACCEPTED. No hardware validation required.
