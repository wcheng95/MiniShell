# Keyer application

K6 adds the portable field UI and keyboard/message scheduler to the hardware-proven
K3 physical engine, K4 Digital I/O adapters, and K5 Audio TX sidetone.
Implementation/handoff evidence: [T038](../../docs/project/codex/T038-keyer-k6-field-ui.md).
Hardware acceptance remains pending supervisor review.

The external ADV artifact is `platform/adv/elf_apps/keyer/build/keyer.app.elf`.
Install as `/flash/apps/keyer.elf` or `/sd/apps/keyer.elf` after review. Its only
resident import is `mini_api_get`; compiler memory helpers are linked into the ELF.

The normal 20x7 screen shows UTC, KeyIn/KeyOut, speed and volume on row 0,
physical decoded history on rows 1–5, and the unsent TX tail/status on row 6.
Ordinary letters (including Q/O), digits and supported punctuation enter CW text.

| Key | Action |
| --- | --- |
| Ctrl+C | Quit and release outputs |
| Opt | Enter/leave Operation; cancel an unfinished edit |
| Fn+Up/Down | Wrap the three Operation pages |
| Up/Down or 1–6 | Select Operation item |
| Enter | Edit/commit in Operation; bypass TxDelay in normal screen |
| Left/Right or Up/Down (also ADV Fn+arrows) | Adjust a numeric/choice edit |
| Escape or bare backtick | Cancel edit/back from Operation |
| `[`, `]` | Speed −/+1, clamped 5–60 |
| `{`, `}` | Volume −/+5, clamped 0–99 |
| `\` | Toggle mute and save immediately |
| Alt+1–5 | Queue M1–M5 atomically |
| Tab | Toggle latched Tune |
| Backtick | Cancel automatic TX, Tune and M1 repeat |
| Backspace | Remove an editable unsent character |

Numeric edits also accept replacement digits. Memories accept up to 95 printable
ASCII characters. Unsupported Morse characters reject the entire append with
`Unsupported char`; full FIFO rejects the entire append with `TX full`.
Keyboard and memory selections use TxDelay; M1 repeats after completion plus
Repeat seconds without a second TxDelay. Physical input cancels all automatic
states before manual output arbitration. TuneTimeout zero disables the timeout.

Only `app_controller` coordinates modules. `ui_shell` and `tx_engine` are pure,
bounded state machines; `ui_adapter` translates Display/Input. Physical
`keyer_engine` is unchanged. All file operations use MiniShell Filesystem.
The single foreground loop retains K5's 48-frame, 48 kHz finite Audio TX writes.
Volume/mute scale PCM without changing Digital I/O or reopening Audio TX.

KeyOut is SKS (tip/ring key together), SKM (tip keys; ring remains low while
running), or OFF (no output handles). Both output lines are released on cleanup.
Default wiring remains inputs G13/G15, open-drain outputs G3/G6.

Committed settings immediately rewrite `/flash/keyer/setting.txt` through a
sibling temporary file, sync, close, then rename. All fields, including GPIO IDs,
are serialized. Save failure retains the runtime edit, preserves the prior file,
and shows `Save failed`; it never reports `Saved`. Missing settings use defaults;
invalid/unreadable settings fail startup. Legacy `SK`/`SK-M` load as SKS/SKM;
legacy Paddle/PaddleR KeyOut values are rejected.
