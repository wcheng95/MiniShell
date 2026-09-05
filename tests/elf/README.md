# MiniShell ELF integration tests

These are thin runtime-loaded integration tests for the six Task 1 ABIs plus a
lifecycle stress workload. Behavioral edge cases belong primarily in
`tests/unit`; these ELF programs verify that a separately built application can
cross the public binary ABI, call the resident service table, and return cleanly
to the shell.

Build the complete ELF test batch from the repository root, with ESP-IDF
exported:

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
tests/elf/out/abi_stress.elf
```

To build and copy them directly to a mounted SD-card `apps` directory:

```bash
./tests/elf/build_all.sh /path/to/sd/apps
```

The six focused ABI tests are expected to pass on the current Tab5 serial-terminal
backend:

```text
abi_system         PASS
abi_memory         PASS
abi_fs             PASS
abi_time_location  PASS
abi_display        PASS
abi_input          PASS (interactive key press)
```

Optional UTC/default-location state is only read by these tests. The tests do not
change the RTC or persistent default location.

## Lifecycle stress test

MiniShell also provides a diagnostic shell command:

```text
repeat <count> <app> [args...]
```

`abi_stress.elf` is intentionally different from the focused tests. Each run:

- verifies all six service tables are present;
- verifies app Memory accounting starts at zero;
- allocates eight MiniShell-managed blocks and intentionally does not free them;
- opens two MiniShell-managed files and intentionally does not close them;
- exercises monotonic time and sleep;
- exercises Display discovery/present;
- performs a nonblocking Input poll;
- returns normally so MiniShell teardown must reclaim the intentional leftovers.

Recommended first stress run:

```text
M$> repeat 20 abi_stress
```

Why 20 is meaningful: without per-app filesystem teardown, two leaked handles per
invocation would exceed the service's 32-handle table before the run completed.
Memory cleanup is checked directly at the beginning of every invocation, so stale
allocations fail immediately on the next cycle.

After a successful stress run, verify ordinary shell/app operation still works:

```text
M$> hello
M$> abi_memory
M$> abi_fs
```

For a longer soak after the first pass:

```text
M$> repeat 100 abi_stress
```
