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

## Verification status

The first real-hardware `put` test passed on the Tab5 reference platform:

```text
python3 tools/minishell_transfer.py /dev/ttyACM0 put test.txt /sd/test.txt
put: test.txt -> /sd/test.txt (34 bytes, crc32=fd90e9b8)
```

A subsequent ELF upload exposed a flow-control bug in the original continuous
streaming implementation:

```text
MFT1 ERROR payload-timeout
```

The small text file succeeded because it fit comfortably within the transport
buffer. ESP-IDF's default USB Serial/JTAG driver configuration uses only a
256-byte RX ring buffer, while the original host helper streamed much larger
chunks without waiting for storage progress. MFT1 now uses 1024-byte block pacing
plus explicit 4096-byte platform RX/TX buffers. The host unit test uses a
1500-byte payload and withholds the second block until `MFT1 NEXT` is emitted, so
multi-block pacing is covered by regression testing.

The corrected block-paced protocol then successfully installed the first normal
utility application:

```text
python3 tools/minishell_transfer.py /dev/ttyACM0 \
    put apps/cat/build/cat.app.elf /sd/apps/cat.elf
put: apps/cat/build/cat.app.elf -> /sd/apps/cat.elf \
     (1568 bytes, crc32=89be16c8)
```

`cat.elf` was then launched normally by the MiniShell app manager and successfully
read the previously uploaded `/sd/test.txt`. This proves multi-block binary
upload, CRC verification, publication into `/sd/apps`, runtime discovery/load,
and execution of an independently developed application.

## Final hardware validation

`tools/task2_validate.py` automates the remaining Task-2 checks against a real
MiniShell device. Close `idf.py monitor` first so the validator has exclusive
access to the USB Serial/JTAG port.

Run:

```bash
python3 tools/task2_validate.py /dev/ttyACM0
```

The validator uses `/sd/task2_validate.bin` as a scratch destination and checks:

```text
1. put + get round trip, verified by host SHA-256
2. replacement of an existing destination
3. a 512 KiB-class multi-block binary transfer
4. an intentionally stalled put after one acknowledged 1 KiB block
5. MFT1 payload timeout on that interrupted transfer
6. removal of the incomplete .mft.part file
7. survival of the previously published destination after interruption
```

The intentional interruption keeps the serial port open but stops sending data
for longer than MiniShell's payload timeout. This exercises MiniShell's timeout
and cleanup path without relying on USB disconnect/reconnect behavior.

The remote scratch file is intentionally left in place because MiniShell does not
yet have the planned `rm` utility.

After the validator passes, run the normal host unit suite and a short real-hardware
ABI sanity check. Task 2 can then be marked complete.

## Verification plan

1. Host unit suite including `abi_transfer_unit`. **Implemented.**
2. Small text-file put. **PASS on real hardware.**
3. Separately built ELF put + execute. **PASS on real hardware.**
4. Automated get/replacement/large-file/interruption validation. **Ready to run.**
5. Final Task-1 regression sanity check. **Pending final closeout.**

## Success criteria

Task 2 V1 is complete when:

- `put` transfers arbitrary binary files from host to MiniShell storage;
- `get` transfers arbitrary binary files back to the host;
- early path/storage errors occur before payload transmission;
- transfer pacing prevents transport-buffer overrun during storage writes;
- CRC verification detects incomplete/corrupt transfers;
- a transferred ELF can be executed normally;
- replacing an existing FATFS destination works with rollback protection;
- interrupted transfers do not replace the last verified destination;
- incomplete temporary files are cleaned up on timeout;
- transfer state returns cleanly to `M$>`;
- existing Task 1 ABI tests still pass;
- the transfer implementation remains resident and modular rather than growing
  into shell parser code.
