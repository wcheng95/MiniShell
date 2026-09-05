# Task 2 - Resident File Transfer — COMPLETE

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
timeouts, pacing, and transfer loops belong to the transfer module.

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

5. Host sends the payload in blocks of at most 1024 bytes.
6. After each non-final block MiniShell writes the block to storage and replies:

```text
MFT1 NEXT
```

   The host does not send the next block until this acknowledgement arrives.
7. After the final block MiniShell verifies the whole-file CRC, syncs/closes the
   temporary file, and asks the platform to replace the destination.
8. MiniShell replies with either:

```text
MFT1 OK <size> <crc32>
```

or:

```text
MFT1 ERROR <reason>
```

The second ready handshake prevents binary payload from being sent before the
storage path is known usable. Block-level pacing prevents a fast host from
outrunning the finite USB Serial/JTAG receive buffer while MiniShell is writing
to SD.

An interrupted or corrupt transfer does not publish the incomplete temporary
file as the destination.

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
- Put uses 1024-byte block acknowledgement for flow control.
- V1 retries the whole transfer after an error rather than implementing block
  retransmission.
- Paths are MiniShell absolute paths and V1 does not support spaces in shell
  arguments.
- Put uses separate header-ready and data-ready handshakes.
- V1 is synchronous. While a transfer is active, the shell does not process other
  commands.
- File transfer does not expand the public application ABI.
- The Tab5 platform explicitly allocates 4096-byte USB Serial/JTAG RX and TX
  driver buffers instead of relying on ESP-IDF's smaller default buffers.

## Development finding: transport flow control

The first small-file upload passed, but an early ELF upload exposed a
`payload-timeout`. The root cause was a fast host streaming into ESP-IDF's small
default USB Serial/JTAG receive buffer while MiniShell was intermittently writing
to SD.

MFT1 was corrected to use 1024-byte block pacing plus explicit 4096-byte platform
RX/TX buffers. The transfer unit test now uses a multi-block payload and requires
MiniShell to emit `MFT1 NEXT` before the fake sender exposes the next block.

## Real-hardware validation

The corrected protocol successfully installed and executed the first normal
utility application:

```text
python3 tools/minishell_transfer.py /dev/ttyACM0 \
    put apps/cat/build/cat.app.elf /sd/apps/cat.elf
put: apps/cat/build/cat.app.elf -> /sd/apps/cat.elf \
     (1568 bytes, crc32=89be16c8)
```

`cat.elf` then launched normally and read `/sd/test.txt`, proving independent ELF
installation without reflashing MiniShell.

Final hardware validation used `tools/task2_validate.py` and passed all checks:

```text
round-trip put/get                     PASS
host SHA-256 equality                  PASS
replace existing destination           PASS
524325-byte multi-block binary         PASS
intentional stalled upload             PASS
payload timeout handling               PASS
.mft.part cleanup                      PASS
old destination survives interruption  PASS
```

The large replacement file was downloaded after publication with matching
SHA-256, and the same published file remained unchanged after the intentionally
interrupted replacement attempt.

## Regression validation

After hardware validation, the complete host suite passed:

```text
abi_system_unit          PASS
abi_memory_unit          PASS
abi_filesystem_unit      PASS
abi_time_location_unit   PASS
abi_display_unit         PASS
abi_input_unit           PASS
abi_transfer_unit        PASS

7/7 PASS
```

Real-hardware reconnect checks also passed for the normal shell/application path,
including `hello`, Filesystem ABI operation, and interactive Input ABI operation.

## Completion criteria

Task 2 V1 is complete because:

- `put` transfers arbitrary binary files from host to MiniShell storage;
- `get` transfers arbitrary binary files back to the host;
- early path/storage errors occur before payload transmission;
- transfer pacing prevents transport-buffer overrun during storage writes;
- CRC verification detects incomplete/corrupt transfers;
- a transferred ELF executes normally;
- replacing an existing FATFS destination works with rollback protection;
- interrupted transfers do not replace the last verified destination;
- incomplete temporary files are cleaned up on timeout;
- transfer state returns cleanly to `M$>`;
- existing Task 1 ABI tests still pass;
- the transfer implementation remains resident and modular rather than growing
  into shell parser code.

**Task 2 is complete.**
