# T011 — Narrow Linux runtime exports

Status: READY

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

- [ ] Linux executable no longer uses broad `ENABLE_EXPORTS ON`.
- [ ] `mini_api_get` remains dynamically resolvable by runtime modules.
- [ ] representative private MiniShell symbols are absent from dynamic exports.
- [ ] deliberate private-import module fails at runtime load.
- [ ] normal public-API module still loads/runs.
- [ ] focused symbol-table regression is registered in root CTest.
- [ ] no public API or loader behavior changes.
- [ ] architecture checks pass.
- [ ] unit suite passes.
- [ ] full Linux suite result recorded.
- [ ] no unrelated cleanup.

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

### Linux linker/export mechanism

### Negative private-import regression

### Dynamic symbol evidence

### Files changed

### Local tests run and results

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and local evidence.

## Architect test result

No hardware validation is expected for this Linux-only runtime-boundary task.
