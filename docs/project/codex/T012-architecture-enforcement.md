# T012 — Strengthen architecture enforcement

Status: READY

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

- [ ] one authoritative FT8/Keyer application rule catalog exists;
- [ ] quoted and angle local includes are enforced;
- [ ] normalized relative local includes are enforced;
- [ ] ambiguous local headers fail closed;
- [ ] private headers cannot escape via alternate include syntax/path;
- [ ] FT8 platform/purity rules still apply;
- [ ] equivalent Keyer platform/purity rules now apply;
- [ ] pure FT8/Keyer modules cannot include MiniShell API outside their allow-list;
- [ ] representative native platform calls are rejected;
- [ ] negative fixtures prove each new enforcement class;
- [ ] both app dependency checks run in root CTest;
- [ ] both app platform checks run in root CTest;
- [ ] checker self-tests run in root CTest;
- [ ] real current FT8 and Keyer trees pass;
- [ ] no production behavior/source changes;
- [ ] unit suite passes;
- [ ] full Linux suite result recorded;
- [ ] limitations of textual/static enforcement are documented;
- [ ] no unrelated cleanup.

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

### Shared rule model

### Include resolution / fail-closed behavior

### Platform/purity enforcement

### Negative fixtures

### CTest integration

### Files changed

### Local tests run and results

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and local evidence.

## Architect test result

No hardware validation is required for this test-only architecture-enforcement task.
