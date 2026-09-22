# Mini-CW Keyer — accepted field baseline (T042-T048)

Source: `wcheng95/Mini-CW` at
`3bfbf169b7c2d49a1be3e9a4c80f945edb32033e` (MiniCW V1.2).
This is a Keyer-mode extraction of that application. The superseded MiniShell
`apps/keyer` implementation has been removed. ADV audio uses the optional generic MiniShell continuous-tone
owner. Keyer settings load from `/flash/minicw/setting.txt` through MiniShell
Filesystem; missing or invalid files fall back to the pinned compiled defaults.

## Current accepted status

Cardputer ADV hardware acceptance is complete for the Mini-CW Keyer-mode application: clean paddle and M1 audio, settings persistence, fixed header/UI behavior, full callsign lookup, color/separator presentation, and compact daily transcript logging with safe note mode. The MiniShell/Mini-CW ownership boundary is frozen by `docs/MiniCW/baseline-audit.md`.

## Ownership and provenance

- `app_core`: pinned Keyer FIFO scheduling, TxDelay, M1 repeat, Tune timeout,
  settings application, event routing and quiet-point persistence coordination.
  Other modes are omitted; T048 adds Keyer transcript logging.
- `keyer_service`: pinned physical/automatic timing, adaptive straight-key
  decoding, KeyIn/KeyOut modes and cancellation. Raw GPIO and ticks are private
  port calls. The decoder source/header are unchanged from the pinned source.
  OP lookup borrows a static, session-long table loaded before Tone opens.
- `ui_service`: pinned Keyer normal screen, five-line decoded history, TX tail,
  memory overlay, Tune and three-page settings menu, numeric/text editors.
  `ui_screen` maps frames to 20x7 MiniShell Display: white header, green rows
  1–5, cyan row 6, and a green separator after the header. Optional generic
  Display capabilities carry colors and the separator; monochrome providers
  retain identical text. ADV renders the separator in its existing 2-pixel gap.
- `audio_service`: pinned domain calls and Morse table over MiniShell tone
  ownership. Each dit/dah enqueues one finite duration; straight/Tune uses hold
  and release; cancellation flushes queued work. No application PCM loop or
  RTOS object is created. Providers without the optional capability retain the
  T042 silent timing fallback. Linux simulates the resident renderer silently.
- `storage_service`: bounded Keyer-only parsing, validation and canonical
  serialization, plus bounded read-only callsign CSV parsing. No hardware or
  filesystem implementation access.
- `port`: the only MiniShell API adapter. Owns Digital I/O handles, logical Input,
  Display, Time/Location and transactional Filesystem calls; releases both output lines before closing
  handles on normal exit and failure.
- `runtime`: small allocation-free ASCII/format/string routines linked locally
  so the external ELF needs no resident libc imports.

The pinned `sdkconfig` has a 100 Hz tick. Domain deadlines retain that 10 ms
quantization (including truncation and minimum one-tick delays) and unsigned
32-bit wrap arithmetic, using MiniShell monotonic microseconds. The original
5 ms polling request likewise becomes a 10 ms sleep. WPM and gap constants are
unchanged. The pinned implementation intentionally applies the squeeze-release
extra element to its **Iambic A** selection, not B; this is preserved.

## Controls

- Type supported Morse characters to append automatic text; Enter starts pending
  text immediately; Backspace removes unsent tail text; backtick/Escape cancels.
- Alt toggles the M1–M5 overlay; plain 1–5 selects while it is visible. The overlay
  remains open, as in the pinned UI. M1 repeats at the configured interval.
- Opt toggles the Keyer Operation/settings menu; Ctrl alone does nothing. Digits select entries; Up/Down
  (or `;` / `.` without Fn) change menu pages. Numeric editors accept digits,
  Enter and left/right stepping. Text editors accept Enter, Backspace and
  Fn+Left/Right cursor movement. The pinned edit/cancel behavior is retained.
- `[` / `]` change WPM. `\` toggles mute state.
- Tab enters/leaves Tune; T toggles its latch. Physical input cancels a latched
  Tune and is consumed until release, as in Mini-CW.
- Fn+Up/Down scroll decoded history on the normal screen.
- Ctrl+C exits from every view, including Tune and pending TX.

MiniShell delivers logical key events, not raw held/released keyboard state.
Its repeat events can repeat cursor movements; raw-key hold timing and the
upstream one-second Backspace-hold clear gesture cannot be inferred reliably
and are not synthesized. Opt opens Operation instead of the upstream mode selector. These are the
Input/scope differences from the source; no public API is added.

KeyIn uses G13/G15 pull-ups; KeyOut uses G3/G6 open drain, active low. KeyIn options are PDN/PDR/SKT/SKR/SKB. SKB treats tip OR ring as one straight
key; both must release to end it. Only SKN, SKM and OFF are selectable KeyOut
modes; legacy Paddle outputs canonicalize to SKN. Even SKM's normally
asserted ring is released on exit. The physical press that cancels automatic TX
is consumed until release, not also decoded/sent as a new element.

## Build and validation

Linux builds `build-linux/runtime/apps/minicw.so` with the ordinary repository
CMake build. The domain/runtime regressions also belong to `tests/unit`.

```sh
source ~/projects/esp-idf/export.sh
idf.py -C platform/adv/elf_apps/minicw fullclean
idf.py -C platform/adv/elf_apps/minicw elf
python3 tests/minicw_elf_inspect.py platform/adv/elf_apps/minicw/build/minicw.app.elf
xtensa-esp32s3-elf-readelf -rW platform/adv/elf_apps/minicw/build/minicw.app.elf
```

The deployment artifact is `minicw.app.elf`, installed as `minicw.elf` by the
architect after review. The sole resident import must be `mini_api_get`.
Compiler division helpers are linked from the toolchain's libgcc. The linker
fragment pads `.data` and `.rodata` for the existing section loader; the inspector checks
packed-section alignment and relocation destinations as well as imports.

T043 adds the optional Audio tone capability and its resident ADV worker;
ordinary PCM APIs, Mini-CW Keyer timing/UI and FT8 remain unchanged.
T042 and T043 hardware acceptance is complete at the golden recovery point
`48a40d79c13ed60ef9f8444a060164852d226fcd`. T044A persistence is hardware accepted at
`da934b03bce4cc8f908fbc1a40afed501a37196d`. T045 UI/I/O is hardware accepted; the resident audio path remains frozen.

## Keyer settings persistence

`/flash/minicw/setting.txt` is a bounded (at most 4,095 bytes) INI-style file.
`[system]` holds volume, tone_hz, key_in and key_in_wpm; `[keyer]` holds key_out,
paddle, sk_wpm, tx_delay_s, tune_timeout_s, repeat_interval_s, mycall and m1–m5.
Mute remains session-only. GPIO assignments, GPS, date/time and other modes are
not persisted. Labels and defaults retain the pinned Mini-CW vocabulary.

Unknown sections/keys are ignored. Invalid known values reject the entire
snapshot; neither invalid nor missing files are rewritten at startup. Blank
lines and whole-line `#`/`;` comments are accepted. Message values retain spaces
and additional `=` characters after the first separator; there are no inline
comments. Parsing uses bounded 159-byte lines. Numeric values/modes trim outer
spaces, and modes accept the pinned case-insensitive aliases.

Startup reads finish before Tone opens. Existing audio initialization and setters
apply loaded volume/pitch before the first Keyer update. Edits take effect
immediately, but saves wait for 250 ms of quiet: no automatic active/pending/
queued TX, no M1 repeat cycle, no Tune, no asserted physical input or Audio busy.
The latest complete snapshot replaces `setting.txt` via `setting.tmp`, full
write, sync, close and commit rename. A failure preserves the old destination
and displays `Save failed`; retry waits for another settings change or clean
exit. Clean exit captures settings before output shutdown and makes one dirty
save attempt after Tone has closed, while Filesystem remains available.


T045 uses the exact 20-column header `HH:MM KIN KOUT WW Vnn` on normal, Tune
and Operation screens. UTC comes only from optional MiniShell Time/Location;
missing/failed UTC displays `--:--`. The header updates on UTC minute changes
while idle. PDN/PDR show KeyIn WPM; SKT/SKR/SKB show adaptive SK WPM. Tune and
transient status stay in the lower status line, and OP-name lookup cannot replace
the header. Operation uses long mode names with shortened `In:`/`Out:` prefixes.

Canonical settings labels are `Paddle-Normal`, `Paddle-Reverse`, `SK-Tip`,
`SK-Ring`, `SK-Both` for KeyIn and `SK-Normal`, `SK-Mono`, `OFF` for KeyOut.
T044A/standalone labels and numeric aliases still load, including
`Paddle_Reverse`; old Paddle/PaddleR outputs load as SKN. Existing files need no
manual migration. The next ordinary settings save uses canonical labels;
transaction and quiet-save mechanics are unchanged.


## Callsign lookup

T046 reads `/flash/minicw/qsocalls.csv` once per launch, after settings reads and
before Tone opens. No file is created or rewritten. Callsign input is streamed with 128-byte read
and line buffers, with no total file-size limit. The application retains every valid
`call,name` row using MiniShell Memory through private port wrappers. Capacity
starts at 64 entries and doubles; the 813-row reference scale uses 19,456 bytes
at 1024-entry capacity. No native heap calls are used. Allocation, open/read/close
errors or any NUL discard the whole table and show `Lookup unavailable`.
The Keyer borrows the completed table until shutdown detaches and frees it.
Missing files are normal and silent.

An optional `call,name` header, blank lines, and whole-line `#`/`;` comments are
accepted. Surrounding spaces/tabs are trimmed. Calls have 1–6 alphanumeric ASCII
characters and normalize to uppercase; names contain 1–11 printable ASCII
characters and retain case. Extra commas, invalid fields and overlong rows are
skipped through the next newline, including when split across reads. LF, CRLF
and a final line without a newline are supported. Duplicate calls retain the first loaded match.

The existing recognizer, slash/base-call rules, own-call exclusion and 72/73
clearing are unchanged. Its last matched base call and name appear as `<call>: <name>` on row 6
only when Tune, transient status and TX-tail text are absent. The fixed UTC
header never changes. Table edits take effect on the next launch; there is no
runtime reload. T046 hardware/audio acceptance is complete.


## Keyer transcript and safe notes

T048 captures one chronological transcript from decoded characters/spaces,
accepted keyboard TX text, appended M1–M5 messages and automatic M1 repeats.
As in standalone V1.2, TX text is recorded when appended, not when transmission
finishes; Backspace corrects the still-current minute. Each minute holds up to
1024 payload bytes. After overflow, finalization trims to the last useful ASCII
space (removing trailing spaces) and adds one ` [TRUNC]` suffix. Without a useful
boundary it keeps the hard cut; non-overflowed text is unchanged.

UTC selects `/flash/minicw/YYYYMMDD.txt`; each line is `HHMM <transcript>`.
There are no GPS/grid, QSO or secondary-format records. Finalized minutes queue
in MiniShell Memory and append only after a safe idle interval, with no TX,
repeat, Tune, Audio or physical key activity. Shutdown cancels output, closes
Tone, and drains the final partial minute before Filesystem teardown. Unavailable
UTC does not fabricate a timestamp. Allocation failure drops only that minute;
filesystem failures are nonfatal and records are attempted once to avoid
repeating an ambiguous append.

On the normal screen, either `'` or `"` toggles a temporary note mode. Entry is
accepted only while idle. It saves KeyOut/Mute, sets KeyOut OFF and Mute OFF, and
adds transcript-only `**`. Type a note such as `20M` through normal local CW;
either quote then adds closing `**`, cancels pending note playback, and restores
the saved values. Quote keys and generated delimiters never enter the TX FIFO.
The transcript contains a separated token, for example `CQ **20M** TU`. Only
note-boundary spaces are collapsed; other user spacing remains unchanged. A
note at the start of a minute adds no extra leading space. Operation editors retain their existing key
handling. KeyOut/Mute changes are ignored during the safety overlay, and settings
snapshots retain the saved logical KeyOut. Exiting the app closes an active note
before taking the final settings snapshot.
