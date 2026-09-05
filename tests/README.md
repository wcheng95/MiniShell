# MiniShell Tests

MiniShell uses a unit-first test hierarchy:

1. host unit tests for service/module semantics and edge cases;
2. separately built ELF tests for the application ABI/runtime boundary;
3. real-hardware checks for platform backends and resident facilities.

The `tests/unit` suite covers all six foundational ABIs with fake backends and
also includes resident-module tests such as file transfer and power/system.
Error paths, timing/resource edge cases, framing, cleanup, and resident routing
can therefore remain deterministic without hardware.

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

CTest currently registers:

```text
abi_system_unit
abi_memory_unit
abi_filesystem_unit
abi_time_location_unit
abi_display_unit
abi_input_unit
abi_transfer_unit
resident_power_unit
```

The first six are the Task 1 application-ABI service groups. `abi_transfer_unit`
exercises the resident MFT1 file-transfer module over a fake byte stream and fake
filesystem, including successful put/get and CRC-failure cleanup.

`resident_power_unit` covers the private resident power-module contract. It is
intentionally not named `abi_power_unit` because Task 3 has not introduced a
public application-facing Power ABI.
