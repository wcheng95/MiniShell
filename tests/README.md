# MiniShell Tests

MiniShell uses two active Linux test layers:

1. host unit tests for portable service/module semantics;
2. Linux runtime/integration tests for the real shell, loader, terminal handoff, and portable applications.

Architecture boundary checks are also run in Linux CI before the build.

## Host unit tests

From the repository root:

```bash
cmake -S tests/unit -B build-unit
cmake --build build-unit
ctest --test-dir build-unit --output-on-failure
```

Current portable unit groups cover:

```text
api_system_unit
api_console_unit
api_memory_unit
api_filesystem_unit
api_time_location_unit
api_display_unit
api_input_unit
api_audio_unit
nano_editor_unit
cp_copy_unit
```

The suite is ordinary host C and does not require ESP-IDF.

## Linux integration tests

The root Linux build registers the runtime/integration suite:

```bash
cmake -S . -B build-linux
cmake --build build-linux
ctest --test-dir build-linux --output-on-failure
```

These tests exercise application discovery/loading, service behavior, terminal input, portable utilities, nano PTY editing, directory iteration, resource/date behavior, deterministic WAV Audio RX, and MiniFT8 integration.

The root build also includes `linux_terminal_parser_unit`, a strict pure-C test for Linux terminal byte-stream parsing. It covers supported CSI sequences split at every byte boundary, split UTF-8, standalone Escape, partial-CSI timeout flush behavior, malformed-input recovery, and parser reset. `linux_input` complements it with real PTY split writes through MiniShell.

## Architecture boundary checks

MiniFT8 currently has two complementary source-boundary checks:

```bash
python3 tests/ft8_platform_boundary.py "$PWD"
python3 tests/app_dependency_boundary.py "$PWD" ft8
```

`ft8_platform_boundary.py` rejects platform leakage into the portable application, including Linux/POSIX, ESP-IDF, FreeRTOS, M5/Cardputer, and board-specific interfaces.

`app_dependency_boundary.py` enforces the application-local logical-module dependency map. It is intentionally reusable: future applications such as Keyer add their own small rule map rather than introducing a different dependency framework.

The dependency checker also has a self-test:

```bash
python3 tests/app_dependency_boundary.py --self-test
```

Linux CI runs the self-test and the real MiniFT8 dependency scan before compiling. Source/header files added under an enforced application root (`main`, `include`, or `src` for MiniFT8) must belong to a declared logical module, so a new directory cannot silently bypass the checker.

## Sanitizers

For the unit suite on a compatible compiler:

```bash
cmake -S tests/unit -B build-unit-asan \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build-unit-asan
ASAN_OPTIONS=detect_leaks=1 \
  ctest --test-dir build-unit-asan --output-on-failure
```

Historical Tab5 ELF/hardware validation material is preserved in `archive/tab5-legacy`, not in active `main`.
