# Task 2 - Resident File Transfer

## Goal

Task 2 adds a bootstrap/recovery file-transfer facility to resident MiniShell so
files can move between a host computer and MiniShell storage without removing the
SD card.

This is intentionally resident infrastructure, not a runtime-loaded application.
See `docs/resident-vs-app.md` for the placement policy.

## V1 user interface

MiniShell exposes two shell commands:

```text
put <remote-path>    receive a host file into MiniShell storage
get <remote-path>    send a MiniShell file to the host
```

A companion host tool drives the binary protocol and supplies the local path.
Typical host usage:

```text
python3 tools/minishell_transfer.py /dev/ttyACM0 put local.elf /sd/apps/local.elf
python3 tools/minishell_transfer.py /dev/ttyACM0 get /sd/log.txt log.txt
```

The host tool sends the shell command itself; the user does not manually paste
binary data into a terminal.

## Ownership and layering

```text
shell command
    |
    v
resident minishell_transfer
    |                 |
    |                 +-- proven MiniShell Filesystem service
    |
    `-- private byte-stream transport callbacks
                         |
                         v
                  USB Serial/JTAG
```

The shell only parses/dispatches `put` and `get`. Protocol state, framing, CRC,
timeouts, and transfer loops belong to the transfer module.

The platform owns the physical console transport and provides narrow private raw
read/write callbacks. Those callbacks are not part of the application ABI.

## Protocol V1

Protocol identifier: `MFT1`.

### Put

1. Host sends shell command `put <remote-path>\n`.
2. MiniShell drains any pending line-ending bytes and replies:

```text
MFT1 PUT READY
```

3. Host sends a 16-byte little-endian header:

```text
bytes 0..3    ASCII "MFT1"
bytes 4..11   uint64 file size
bytes 12..15  uint32 IEEE CRC-32 of payload
```

4. MiniShell validates the header and successfully opens the temporary destination.
   Only then it replies:

```text
MFT1 DATA READY
```

5. Host sends exactly `file size` raw payload bytes.
6. MiniShell writes to a temporary file, verifies CRC, syncs/closes it, then asks
   the platform to replace the destination with the completed temporary file.
7. MiniShell replies with either:

```text
MFT1 OK <size> <crc32>
```

or:

```text
MFT1 ERROR <reason>
```

The second ready handshake is deliberate: if the path is invalid, storage is
unavailable, or the temporary file cannot be opened, MiniShell reports the error
before the host starts streaming binary payload. That prevents leftover payload
bytes from spilling into the shell after an early failure.

An interrupted or corrupt transfer must not intentionally publish the incomplete
temporary file as the destination.

### Get

1. Host sends shell command `get <remote-path>\n`.
2. MiniShell opens/stats the file and computes its CRC.
3. MiniShell replies:

```text
MFT1 GET <size> <crc32>
```

4. MiniShell sends exactly `size` raw bytes.
5. MiniShell terminates with:

```text
MFT1 OK
```

6. Host verifies byte count and CRC before publishing its local output file.

## V1 design choices

- USB Serial/JTAG is the first transport, but transfer protocol logic does not
  depend on ESP-IDF types.
- File payload is binary-safe.
- Whole-file CRC-32 detects corruption.
- V1 retries the whole transfer after an error rather than implementing block
  retransmission.
- Paths are MiniShell absolute paths and V1 does not support spaces in shell
  arguments.
- Put uses separate header-ready and data-ready handshakes.
- V1 is synchronous. While a transfer is active, the shell does not process other
  commands.
- File transfer does not expand the public application ABI.

## Verification plan

1. Run the host unit suite, including `abi_transfer_unit`.
2. Build MiniShell with the resident module.
3. Put a small text file and compare its contents.
4. Put a separately built `.elf`, then execute it.
5. Get the same file back and compare SHA-256 on the host.
6. Put over an existing destination and confirm FATFS backup/replace behavior.
7. Transfer a larger binary file.
8. Interrupt a put and verify the old destination remains usable where possible.
9. Run ordinary ABI apps after transfer to verify shell/transport handoff.

## Success criteria

Task 2 V1 is complete when:

- `put` transfers arbitrary binary files from host to MiniShell storage;
- `get` transfers arbitrary binary files back to the host;
- early path/storage errors occur before payload transmission;
- CRC verification detects incomplete/corrupt transfers;
- a transferred ELF can be executed normally;
- replacing an existing FATFS destination works with rollback protection;
- transfer state returns cleanly to `M$>`;
- existing Task 1 ABI tests still pass;
- the transfer implementation remains resident and modular rather than growing
  into shell parser code.
