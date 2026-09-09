# rx_audio_adapter

`rx_audio_adapter` is the MiniFT8-owned edge to the MiniShell RX Audio service.

It does **not** own the physical audio device, Linux WAV reader, QMX/UAC transport, driver, or backend resource. Those remain below the MiniShell public API.

Ownership:

```text
MiniShell Audio
    owns provider/device/backend resource

rx_audio_adapter
    owns MiniFT8's public RX stream handle
    owns open/start/read/stop/close lifecycle
    exposes bounded interleaved S16 frame reads

RxFrontend
    owns channel meaning and 12 kHz -> 6 kHz adaptation
```

V1 transport is fixed:

```text
sample rate  12000 Hz
format       signed 16-bit PCM
channels     2
```

The endpoint string is provider-defined. On the Linux WAV provider it is a logical MiniShell filesystem path such as `/flash/rx6.wav`. A future live provider may define another endpoint without changing this module's transport contract or any pure RX module below it.

`MINI_ERR_END_OF_STREAM` is translated into the distinct `RX_AUDIO_ADAPTER_END_OF_STREAM` status. Other MiniShell failures are surfaced as `RX_AUDIO_ADAPTER_ERR_MINISHELL`, while the raw MiniShell result remains available as a diagnostic.

The adapter owns no allocator and retains no full-slot PCM. Caller-owned bounded frame buffers are passed directly to MiniShell Audio reads.

RX-6 tests cover:

```text
Audio capability/interface validation
fixed 12 kHz/S16/2-channel open format
open/start/read/stop/close state
end-of-stream mapping
cleanup-safe close
real Linux MiniShell WAV provider -> full FT8 decode chain
```
