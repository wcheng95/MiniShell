# T012 — Strengthen architecture enforcement

Status: COMPLETE

## Objective

Resolve the actionable parts of T001 F09 without changing production behavior.

The current architecture checks are useful but incomplete:

- `app_dependency_boundary.py` only recognizes quoted includes;
- relative/angle-bracket local includes can evade dependency rules;
- ambiguous local header basenames can become invisible;
- FT8 duplicates part of its module graph in `ft8_platform_boundary.py`;
- Keyer lacks equivalent platform/private-API purity enforcement;
- root CTest does not run the app dependency checks or their self-test;
- negative fixtures do not cover several documented escape paths.

T012 is **test/enforcement only**.

## Architectural goal

Keep one maintained application rule catalog for detailed FT8/Keyer ownership and use it from the checkers.

Enforce, with demonstrable negative fixtures:

```text
module ownership
private-header ownership
allowed local dependency direction
portable-app platform isolation
pure-module MiniShell-API exclusion
known native/platform symbol leakage
normal local test discovery
```

Do not claim that text/static checks prove arbitrary transitive call graphs, runtime state ownership, or security isolation.

T011 separately enforces the Linux runtime dynamic-export boundary.

## Required structure

Preferred:

```text
tests/architecture_rules.py
    shared FT8 + Keyer rule catalog

tests/app_dependency_boundary.py
    local module/include dependency checker

tests/app_platform_boundary.py
    generic platform/private-API purity checker

tests/ft8_platform_boundary.py
    compatibility wrapper OR narrowed FT8-specific checks only
```

Equivalent organization is acceptable if there is still one authoritative rule source and no duplicated FT8 dependency graph.

## 1. Shared FT8 / Keyer dependency rules

Move or otherwise centralize:

- module path ownership;
- allowed module-to-module dependencies;
- private headers;
- enforced roots;
- lifecycle/source-pattern checks;
- modules allowed to include `minishell/api.h`.

FT8 and Keyer must both use the same generic enforcement engine.

Do not change the architecture itself merely to make the checker simpler.

Current expected MiniShell API visibility:

### FT8

Allowed direct public MiniShell API users remain the current edge/coordinator modules:

```text
main
app_controller
storage_service
log_service
rx_audio_adapter
```

Pure policy/DSP modules remain MiniShell-agnostic.

### Keyer

Allowed direct public MiniShell API users are the application lifecycle/coordinator/edge modules that already own MiniShell service interactions:

```text
main
app_controller
config_service
keyin
keyout
sidetone
```

`keyer_engine` and shared pure types remain MiniShell/platform independent.

Verify actual current source before finalizing these sets; if current code contradicts the architecture, report rather than silently broadening the allow-list.

## 2. Local include resolution

Update the dependency checker so both forms are parsed:

```c
#include "header.h"
#include <header.h>
```

For app-local includes, resolve robustly:

1. normalized relative path from the including file for quoted includes, including `./` and `../`;
2. exact app-relative path;
3. declared application include roots / canonical local paths;
4. unique basename fallback only when unambiguous.

Requirements:

- normalized paths must not escape the application root;
- an ambiguous local basename is a violation, not silently ignored;
- an app-local header with no module owner is a violation;
- angle-bracket syntax must not bypass a local dependency/private-header rule;
- private headers remain owner-only regardless of include spelling.

Do not classify ordinary standard/system headers as app-local merely because their basename is common.

## 3. Generic platform/private-API checker

Add or generalize a checker that runs for **both FT8 and Keyer**.

At minimum reject application source references to known platform-private families such as:

### Headers / include prefixes

```text
platform/
linux/
alsa/
sys/
freertos/
driver/
soc/
hal/
esp_private/
esp_
M5 / m5
Arduino.h
unistd.h
fcntl.h
dirent.h
pthread.h
dlfcn.h
sdkconfig.h
platform_backend.h
adv_internal.h
```

Use careful matching so normal standard C headers remain allowed.

### Obvious native/platform symbol families

Detect direct source use of clear platform-native APIs where false-positive risk is low, including representative families such as:

```text
snd_
pthread_
dlopen / dlsym / dlclose
esp_
gpio_
i2s_
vTask / xTask
tud_ / tinyusb_
fopen / opendir
clock_gettime
usleep / nanosleep
```

This is not intended to recognize every POSIX call. Keep the list narrow, explicit, and covered by negative fixtures.

### Public MiniShell API purity

If a module is not in that app's allowed MiniShell-API set, reject direct inclusion of:

```text
minishell/api.h
```

Keep the existing FT8 AutoSeq no-heap rule if it remains in the platform/purity checker. Do not invent new no-heap requirements for other modules.

## 4. Remove duplicated dependency policy

`ft8_platform_boundary.py` currently maintains a second FT8 local dependency graph.

After T012 there must be only one authoritative FT8 module dependency rule set.

The FT8 platform checker may:

- import the shared rules; or
- drop local-graph enforcement and focus only on platform/purity checks.

Do not maintain two manually synchronized dependency tables.

## 5. Negative self-tests

Expand self-tests using temporary source trees. They must prove detection of at least:

1. forbidden FT8 module edge;
2. forbidden Keyer module edge;
3. private header escape;
4. angle-bracket local include escape attempt;
5. quoted relative `../` local include escape attempt;
6. ambiguous duplicate local header basename;
7. unowned module/source under an enforced root;
8. pure FT8 module including `minishell/api.h`;
9. pure Keyer engine including `minishell/api.h`;
10. forbidden Linux/POSIX/ALSA header;
11. forbidden ESP/FreeRTOS header;
12. representative forbidden native symbol call;
13. valid FT8 fixture passes;
14. valid Keyer fixture passes.

A checker with no negative self-test for a rule is not sufficient evidence that the rule works.

## 6. Root CTest integration

Register the architecture checks so a normal:

```bash
ctest --test-dir build-linux
```

runs them.

Required CTest coverage:

```text
architecture_app_ft8
architecture_app_keyer
architecture_app_selftest
architecture_platform_ft8
architecture_platform_keyer
architecture_platform_selftest
```

Naming may differ, but both apps and checker self-tests must be included.

Keep the existing `ft8_platform_boundary` test name only if compatibility is useful; avoid duplicate work unless intentional.

## 7. Current-tree enforcement

Run the strengthened checks against the real repository.

If they expose an actual production architecture violation:

- do not weaken the checker;
- do not silently change production code in T012;
- report the concrete violation and set the task BLOCKED unless it is clearly a stale test-only path/exemption already documented.

Narrow explicit exceptions are allowed only for:

- test fixtures;
- host-only tools;
- vendor code;
- platform composition files;

and must be documented in the rule source.

Do not create broad directory exemptions that hide production application code.

## Non-goals

Do not:

- change application/runtime behavior;
- change public MiniShell API;
- modify FT8/Keyer dependency direction;
- modify Linux/ADV loader behavior;
- claim full static-analysis/call-graph proof;
- add a compiler plugin or heavy external analyzer;
- fix F10/F11/F13 documentation/reference issues;
- fix the two stale Linux baseline tests;
- perform general production cleanup.

## Expected files

Likely:

```text
tests/architecture_rules.py
tests/app_dependency_boundary.py
tests/app_platform_boundary.py
tests/ft8_platform_boundary.py
CMakeLists.txt
```

plus small test helpers if necessary.

Production `apps/`, `core/`, and `platform/` source should remain unchanged.

## Local test gate

Run locally:

```bash
git status --short

python3 tests/app_dependency_boundary.py --self-test
python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer

# generic platform checker names may vary
python3 tests/app_platform_boundary.py --self-test
python3 tests/app_platform_boundary.py . ft8
python3 tests/app_platform_boundary.py . keyer

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux -R 'architecture|platform_boundary|dependency_boundary' --output-on-failure

cmake -S tests/unit -B /tmp/T012-build-unit
cmake --build /tmp/T012-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T012-build-unit --output-on-failure

git diff --check

ctest --test-dir build-linux --output-on-failure
```

Also run any compatibility wrapper directly if retained.

The accepted `linux_audio` and `linux_ft8` baseline failures may remain and must not be weakened.

No hardware validation and no GitHub Actions wait are required.

## Acceptance criteria

- [x] one authoritative FT8/Keyer application rule catalog exists;
- [x] quoted and angle local includes are enforced;
- [x] normalized relative local includes are enforced;
- [x] ambiguous local headers fail closed;
- [x] private headers cannot escape via alternate include syntax/path;
- [x] FT8 platform/purity rules still apply;
- [x] equivalent Keyer platform/purity rules now apply;
- [x] pure FT8/Keyer modules cannot include MiniShell API outside their allow-list;
- [x] representative native platform calls are rejected;
- [x] negative fixtures prove each new enforcement class;
- [x] both app dependency checks run in root CTest;
- [x] both app platform checks run in root CTest;
- [x] checker self-tests run in root CTest;
- [x] real current FT8 and Keyer trees pass;
- [x] no production behavior/source changes;
- [x] unit suite passes;
- [x] full Linux suite result recorded;
- [x] limitations of textual/static enforcement are documented;
- [x] no unrelated cleanup.

## Branch workflow

Use:

```text
codex/T012-architecture-enforcement
```

Before handoff:

1. set Status to REVIEW;
2. record the new rule-source/checker structure;
3. list negative fixtures added;
4. record current-tree and CTest evidence;
5. commit and push;
6. return commit SHA;
7. do not open a PR;
8. do not wait for GitHub Actions.

Supervisor reviews `main..<SHA>`. If clean, fast-forward/merge to `main`, then delete local and remote T012 branches.

## Codex implementation notes

### Implementation summary

Implemented T012 entirely in test/enforcement code and root CTest registration.
Both application trees pass the strengthened checkers. No production source,
public API, module dependency direction, loader, or runtime behavior changed.
No task deviations; two narrowly scoped permitted host/vendor exceptions are
recorded below. No hardware/manual validation is required.

### Shared rule model

`tests/architecture_rules.py` is the authoritative FT8/Keyer catalog: module
ownership, enforced roots, local dependencies, private headers, lifecycle
patterns, include roots, direct MiniShell API users, and AutoSeq no-heap policy.
The existing dependency catalog is preserved. The old FT8-only checker's host
`tools -> ft8_engine` rule is consolidated here as well. Both generic engines
consume this catalog; the compatibility wrapper has no independent policy.
Verified actual MiniShell API includes match the exact FT8 and Keyer module sets
specified in this task. Pure engine/shared types gain no API permission.

### Include resolution / fail-closed behavior

Both quoted and angle forms are parsed, including whitespace around `# include`.
Resolution tries the quoted source-relative path, exact app-relative path,
declared include roots, then unique basename fallback for bare header names.
Paths are normalized and checked for application-root escape. Ambiguous candidates
are reported rather than ignored. Resolved local headers must have owners;
private ownership and dependency direction apply to every spelling. `.hpp` is
indexed alongside `.h`. Standard C header names do not enter basename fallback.
Unowned sources under enforced roots fail; the platform checker also rejects
source files outside any catalogued module. Comments are stripped while retaining
diagnostic line numbers and include literals.

### Platform/purity enforcement

`app_platform_boundary.py` checks both apps for the listed Linux/POSIX/ALSA and
ESP/FreeRTOS/board headers, explicit native symbol families, platform tokens,
and disallowed direct MiniShell API includes. Relative/normalized header
components are checked too. Native identifiers in comments/string literals do
not count. The existing AutoSeq allocation-call prohibition is retained without
adding heap constraints elsewhere.

Two exact exceptions in the shared catalog reflect allowed task categories:

- Host-only `apps/ft8/tools/ft8_decode.c`: only the `fopen` symbol is allowed.
- Vendor `apps/ft8/src/ft8_engine/vendor/kissfft/kiss_fft.h`: only `sys/types.h`,
  used in its optional `FIXED_POINT` branch, is allowed.

Neither directory is excluded from scanning; other calls/headers, ownership,
dependency and purity checks remain active. No production architecture violation
was found.

### Negative fixtures

Dependency self-test: 49 isolated-tree cases across FT8 and Keyer, covering
forbidden module edges through quoted/angle and app-relative/`../` paths,
private headers through bare/angle/exact/relative paths, duplicate ambiguous
basenames, root escape, unowned local headers, unowned enforced-root sources,
and all existing FT8 lifecycle pattern classes. Positive cases exercise both
apps, owner-relative precedence, declared canonical roots, unique basename
fallback, standard headers, and owner access to private state.

Platform self-test: 143 cases across both apps, covering each forbidden header
and prefix in both include forms, pure FT8/Keyer API leakage, normalized API
spelling, relative platform headers, each native-symbol family, existing platform
tokens, and each AutoSeq heap-call family. Positive fixtures cover comments,
string literals and standard C headers. Additional assertions verify every
allowed API edge module and the exact host/vendor exception behavior, including
rejection of other calls/headers in those same files.

### CTest integration

Root CTest now registers `architecture_app_ft8`, `architecture_app_keyer`,
`architecture_app_selftest`, `architecture_platform_ft8`,
`architecture_platform_keyer`, and `architecture_platform_selftest`.
The former `ft8_platform_boundary` CTest entry is replaced to avoid duplicate
work. Its script remains a compatibility wrapper running both generic FT8 checks
and was also executed directly.

### Files changed

- `tests/architecture_rules.py`: shared application rule catalog.
- `tests/app_dependency_boundary.py`: include resolution and expanded self-tests.
- `tests/app_platform_boundary.py`: generic platform/purity checker and self-tests.
- `tests/ft8_platform_boundary.py`: policy-free compatibility wrapper.
- `CMakeLists.txt`: six architecture test registrations only.
- `docs/project/codex/T012-architecture-enforcement.md`: review status and evidence.

### Local tests run and results

```bash
git status --short
python3 tests/app_dependency_boundary.py --self-test
python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer
python3 tests/app_platform_boundary.py --self-test
python3 tests/app_platform_boundary.py . ft8
python3 tests/app_platform_boundary.py . keyer
python3 tests/ft8_platform_boundary.py .
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux -R 'architecture|platform_boundary|dependency_boundary' --output-on-failure
cmake -S tests/unit -B /tmp/T012-build-unit
cmake --build /tmp/T012-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T012-build-unit --output-on-failure
git diff --check
ctest --test-dir build-linux --output-on-failure
```

All checker invocations, both configure/builds, and diff whitespace checks passed.
Focused architecture CTest: **6/6 passed**. Unit suite: **14/14 passed**.
Full Linux CTest: **33/35 passed**. Only the accepted baseline failures remain:
`linux_audio` expects a frames/hash substring without the intervening diagnostic
fields (both probe executions report PASS); `linux_ft8` times out on its stale
rotated-queue expectation for `N5CH     RPLY 0/3`. Neither test was changed.

### Known limitations / risks

These are textual source checks, not a compiler/preprocessor or a security
boundary. They scan conditional source branches, resolve literal includes using
the catalog rather than a compilation database, and do not expand macros or
prove arbitrary transitive call graphs, runtime ownership, or macro-generated
native calls. The native list is deliberately explicit and bounded. Future
module/include-root changes need catalog review. No new broad exemption exists.
The accepted Linux baseline failures remain outside T012 scope. No hardware
validation, PR, or GitHub Actions wait is needed or performed.

### Commit

One bounded commit on `codex/T012-architecture-enforcement`, titled
`T012: strengthen application architecture enforcement`. The pushed SHA is
returned in the handoff; these notes are part of that implementation commit.

## Supervisor review

PASS. Reviewed `e966f9eb4af8435190205a93df1f0de235e38da3` against `main`. The change is enforcement-only and centralizes FT8/Keyer architecture policy in one rule catalog. Local include resolution now handles quoted/angle/relative forms, ambiguity fails closed, private-header ownership is preserved across alternate spellings, and generic platform/purity checks cover both FT8 and Keyer. The former FT8 checker is a policy-free compatibility wrapper.

Negative self-tests exercise the new rule classes, and root CTest now runs both applications' dependency/platform checks plus checker self-tests. Focused architecture CTest 6/6, unit suite 14/14, and full Linux 33/35 results are accepted with only the two known baseline failures. Commit was fast-forwarded directly to `main`.

## Architect test result

ACCEPTED. No hardware validation required.
