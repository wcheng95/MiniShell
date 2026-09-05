# MiniShell Tests

Task 1 uses a unit-first test hierarchy:

1. host unit tests for service semantics and edge cases;
2. separately built ELF tests for the application ABI/runtime boundary;
3. real-hardware checks for platform backends.

The current `tests/unit` suite covers all six foundational ABIs with fake
backends so error paths and timing/resource edge cases are deterministic.

## Run the host unit tests

From the repository root:

```bash
cmake -S tests/unit -B build-unit
cmake --build build-unit
ctest --test-dir build-unit --output-on-failure
```

The suite builds as ordinary C and deliberately does not require ESP-IDF.

## Sanitizer run

On a host compiler that supports sanitizers:

```bash
cmake -S tests/unit -B build-unit-asan \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build-unit-asan
ASAN_OPTIONS=detect_leaks=1 \
  ctest --test-dir build-unit-asan --output-on-failure
```

## Test groups

CTest registers one group per foundational ABI:

```text
abi_system_unit
abi_memory_unit
abi_filesystem_unit
abi_time_location_unit
abi_display_unit
abi_input_unit
```
