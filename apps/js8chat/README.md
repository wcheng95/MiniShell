# JS8Chat MiniShell app

T064 adds the first Linux receive-only app slice. The public MiniShell Audio
service supplies 12 kHz S16 stereo; the app averages L/R and retains phase-0
12k-to-6k decimation across reads. Every converted chunk is freshly referenced to
MiniShell UTC and backdated before scheduling its capture boundary.

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
These are measurement aids, not real-time guarantees. Real pc-1/QMX acceptance,
including 20 consecutive slots without decode-induced discontinuity, remains
required after supervisor review.

The frozen reference is JS8Call-improved v3.0.3. The T052-T063 engine and semantics
are unchanged. Shared JSON serialization lives in `src/activity_json`; the host
FILE sink and MiniShell FS sink use the same `js8-activity-v1` formatter. See
`docs/js8/activity-log.md` and the T064 task for timing, ownership and test evidence.
No TX, live network source, final UI, conversations or ADIF are implemented.
