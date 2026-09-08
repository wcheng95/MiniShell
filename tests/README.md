# MiniShell Tests

MiniShell uses two active Linux test layers:

1. host unit tests for portable service/module semantics;
2. Linux runtime/integration tests for the real shell, loader, terminal handoff, and portable applications.

## Host unit tests

From the repository root:

```bash
cmake -S tests/unit -B build-unit
cmake --build build-unit
ctest --test-dir build-unit --output-on-failure
```

Current portable unit groups cover:

```text
abi_system_unit
abi_memory_unit
abi_filesystem_unit
abi_time_location_unit
abi_display_unit
abi_input_unit
abi_audio_unit
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
