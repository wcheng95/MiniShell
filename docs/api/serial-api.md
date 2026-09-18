# MiniShell Serial API

The optional `mini_api_t.serial` field is appended after `digital_io`. API
generation remains v3. Applications must check that `mini_api_t.struct_size`
includes this field before reading it, then check the pointer, service
`struct_size`, capabilities, and required callbacks. See
`include/minishell/api.h` for the authoritative declarations.

Serial transports raw bytes. Applications own framing and protocol semantics.
The resident service owns public handles and closes any remaining stream at
foreground app exit, including abnormal app return. V1 supports one open stream
at a time. Serial is independent of Audio and Digital I/O.

## Operations

- `open(endpoint, out_serial)` requires a nonempty explicit endpoint. Failure
  leaves the output invalid. A second open returns `MINI_ERR_TOO_MANY_OPEN`.
- `read(serial, buffer, size, out_read, timeout_ms)` requires READ capability.
- `write(serial, buffer, size, out_written, timeout_ms)` requires WRITE capability.
- `close(serial)` consumes the handle on success **and on cleanup error**.
  A second close or subsequent transfer returns `MINI_ERR_BAD_HANDLE`.

Read/write always require a count output. Size zero is a no-op and permits a
NULL buffer; nonzero size requires a buffer. Success may transfer fewer bytes
than requested. Error results return count zero. Callers must inspect both the
result and count; successful write means transport acceptance, not a device
response or physical drain guarantee.

`MINI_WAIT_NONE` makes one nonblocking attempt. Finite timeouts bound waiting for
progress; `MINI_WAIT_FOREVER` waits without a deadline. Timeout with no progress
returns `MINI_ERR_TIMEOUT`; disconnection/EOF is an I/O error. A timeout/error
does not itself close the stream. Public handles are renewed on reopen so a
recently closed handle cannot access the replacement stream.

## Linux provider

The endpoint is `serial:<absolute tty path>`, for example
`serial:/dev/ttyACM0` or a PTY slave used in tests. Relative paths, missing paths,
and non-tty files are rejected. The provider configures raw 115200/8N1 with no
software or hardware flow control, and uses nonblocking I/O with poll and a
monotonic timeout deadline. It saves the previous termios state, attempts to
restore it on close, and releases the fd even if restoration fails after
disconnect. No Linux fd is public.

There is no device discovery, protocol parsing, cross-process tty arbitration,
or reconnect policy. ADV has no public Serial provider in T019 and exposes a
NULL service; its private UAC companion CDC handle is not reused.
