# MiniShell ELF integration tests

These are thin runtime-loaded integration tests for the six Task 1 ABIs.
Behavioral edge cases belong primarily in `tests/unit`; these ELF programs verify
that a separately built application can cross the public binary ABI, call the
resident service table, and return cleanly to the shell.

Build all six from the repository root, with ESP-IDF exported:

```bash
./tests/elf/build_all.sh
```

The collected binaries are:

```text
tests/elf/out/abi_system.elf
tests/elf/out/abi_memory.elf
tests/elf/out/abi_fs.elf
tests/elf/out/abi_time_location.elf
tests/elf/out/abi_display.elf
tests/elf/out/abi_input.elf
```

To build and copy them directly to a mounted SD-card `apps` directory:

```bash
./tests/elf/build_all.sh /path/to/sd/apps
```

On the current Tab5 backend, the expected first-pass results are:

```text
abi_system         PASS
abi_memory         PASS
abi_fs             PASS
abi_time_location  PASS
abi_display        SKIP until the Tab5 Display backend is connected
abi_input          SKIP until the Tab5 Input backend is connected
```

Optional UTC/default-location state is only read by these tests. The tests do not
change the RTC or persistent default location.
