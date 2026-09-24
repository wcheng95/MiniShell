# JS8Chat MiniShell app

T064 adds the first Linux receive-only app slice. The public MiniShell Audio
service supplies 12 kHz S16 stereo; the app averages L/R and retains phase-0
12k-to-6k decimation across reads. Every converted chunk is freshly referenced to
MiniShell UTC and backdated before scheduling its capture start 1.6 seconds
before the target slot. Initial scheduling allows 40 ms lateness, like MiniFT8;
a scheduled start stepped over by a chunk uses its first available sample.

Build with the normal Linux CMake build, then run in MiniShell:

```text
js8chat --rx alsa:hw:CARD=QMX,DEV=0 --dial-hz 14078000 --cat serial:/dev/ttyACM0 --log /flash/js8chat/activity.jsonl
```

Choose actual Audio/CDC endpoints for the host. `--cat` requires a positive dial
frequency fitting uint32 Hz; metadata alone supports the T063 integer range.
CAT sends only `MD6;FR0;FT0;FA%011u;` on startup. Audio and CAT are independent.
Omit CAT to leave the radio controls alone. Omit `--log` to monitor without a log.
Use `--slots N` for N completed decode windows, or q/Escape to quit. The app
consumes MiniShell Input while active, so do not prequeue subsequent shell
commands during an interactive run.

For compressed JSC text, place the checked-in `resources/jsc.dict` at the logical
MiniShell path `/flash/js8chat/jsc.dict`. Create the log directory before launch.
JSC opens lazily via MiniShell Filesystem; missing/corrupt resources and log errors
are explicit failures. No whole-slot PCM or dictionary is loaded into RAM.

The capture thread calls only MiniShell services. Linux composition owns one
decode worker and joins it before app cleanup/unload. One immutable waterfall
snapshot is handed off at 93 blocks; later capture continues while it decodes.
A busy worker costs a reported decode window, never an unbounded audio queue.
Activity/reassembly and FS/console publication occur on the app thread.

`JS8 decoded` diagnostics include per-job decode microseconds, maximum interval
between serviced frontend chunks, candidate/unique counts, dropped windows and
Audio discontinuities. `JS8 stopped` reports completed slots and cleanup status.
These are measurement aids, not real-time guarantees. T064 records accepted
real pc-1/QMX reception; T066 records accepted real pc-1 KFS/browser-monitor reception.

The frozen reference is JS8Call-improved v3.0.3. The T052-T063 engine and semantics
are unchanged. Shared JSON serialization lives in `src/activity_json`; the host
FILE sink and MiniShell FS sink use the same `js8-activity-v1` formatter. See
`docs/js8/activity-log.md` and the T064 task for timing, ownership and test evidence.
No TX, live network source, final UI, conversations or ADIF are implemented.

## Linux browser / WebSDR audio (T066)

The browser owns WebSDR networking, tuning and audio playback. MiniShell captures
its desktop playback monitor through the Linux Audio provider:

```text
js8chat --rx pulse:@DEFAULT_MONITOR@ --rx-delay-ms 800 --dial-hz 7078000 --log /flash/js8chat/activity.jsonl
```

The example 800 ms is a trial value, not a default or calibrated KFS delay.
`--rx-delay-ms N` accepts integer 0..5000, defaults to zero, and works with any RX
endpoint. Positive N means arriving PCM is N milliseconds old: the app subtracts
it from each fresh UTC reading before produced-sample backdating. The target RF
slot identity, 1.6-second pre-roll, 93-block capture, decoder and log schema remain
unchanged. Discontinuity resets capture/reassembly while retaining this option.

On pc-1, open KFS in the browser, tune an active JS8 Normal frequency (for example
7078 kHz USB), choose bandwidth covering roughly 200..2900 Hz audio, and click
Audio Start. Inspect sources with `pactl list short sources`. Select the default
monitor above or `pulse:alsa_output.pci-0000_00_1f.3.analog-stereo.monitor` using
the actual source name. Keep other desktop sounds quiet. A dedicated monitor
sink is optional operator configuration; MiniShell does not create or reroute it.

Do not supply `--cat` for remote WebSDR receive. `--dial-hz` describes the remote
dial for RF logging; it does not tune the browser. Try delay values such as
0/250/500/750/1000/1250/1500 ms if needed, then record the selected value and run
`--slots 20`. Record drops/discontinuities and decoded activity, quit and reopen.
A quiet band is not itself an Audio-provider failure. Real pc-1 KFS testing is accepted using the explicit analog-stereo monitor source with `--rx-delay-ms 0`; multiple HB/directed frames decoded with zero observed drops/discontinuities.

The Linux provider maps `pulse:<source>` to ALSA `pulse:DEVICE=<source>` using the
[ALSA Pulse plugin configuration](https://github.com/alsa-project/alsa-plugins/blob/master/pulse/50-pulseaudio.conf).
It requires ALSA development headers at build time and the ALSA Pulse plugin plus
a running PulseAudio or PipeWire-Pulse server at runtime. Source names are 1..255
ASCII letters/digits or `_`, `-`, `.`, `@`; empty names and ALSA argument syntax
are rejected. The plugin supplies requested S16 mono/stereo PCM at the requested
rate (JS8 requests 12 kHz stereo). There is no QMX decimation in this mode.
The existing `alsa:` QMX path remains 48 kHz S24_3LE stereo, phase-0 /4 conversion,
and 10 ms target latency. No direct WebSDR or native libpulse client is added.


## Human-readable activity log

Keep `js8-activity-v1` JSONL as the canonical lossless log. For reading band
activity, convert it with the checked-in formatter:

```sh
python3 apps/js8chat/tools/js8_log_text.py /path/to/activity.jsonl
```

The formatter prints one compact line per event with UTC, RF/audio frequency,
semantic content, candidate score and LDPC hard-error count. Examples:

```text
2026-09-24 04:45:45    7.078706 MHz  +706.250 Hz  HB         KC0CYR AP90 [score=16 err=15]
2026-09-24 04:46:00    7.078753 MHz  +753.125 Hz  DIRECTED   W8RAY -> K1CF  HEARTBEAT SNR -10 [score=22 err=2]
```

Use `--no-quality` for a cleaner operator view or `--messages-only` to show
only completed multi-frame MESSAGE events. The tool also accepts stdin:

```sh
cat activity.jsonl | python3 apps/js8chat/tools/js8_log_text.py --no-quality
```
