# T040 — Keyer unified six-line TX transcript

Status: READY

## Objective

Simplify the normal Keyer screen after abandoning T039 audio/display experiments.

Architect intent:

1. Alt opens the transient M1-M5 chooser.
2. Selecting memory 1-5 immediately closes the chooser.
3. Remove the dedicated visible TX-buffer/tail row.
4. Use rows 1-6 as one plain six-line text transcript.
5. Transcript text may come from:
   - decoded physical paddle/straight-key input;
   - keyboard automatic TX;
   - M1-M5 automatic TX.
6. Keep the remaining speaker-pop issue separate. T040 does not attempt to solve it.

The automatic TX scheduler still needs a private FIFO internally. "Drop the TX
buffer" in T040 means **remove it from the UI**, not remove the bounded scheduler
queue required for keyboard/macros/repeat.

## Baseline

Implement from current `main`, which contains the accepted T038 K6 code and no
merged T039 production experiments.

Preserve the useful post-R3 corrections already on main, including:

- `PdL/PdR/SkT/SkR`;
- SKS/SKM/OFF;
- Operation O1/O2/O3;
- Alt memory overlay;
- direct Alt+1..5 support;
- raised-cosine sidetone;
- persistence and shortcuts.

T039 is BREAK and its experimental Audio/Display changes remain unmerged.

## Normal screen layout

Keep row 0 as the Keyer header/status line.

Rows 1-6 are the unified transcript:

```text
row 0: HH:MM PdL SKS 20 V80
row 1: <text>
row 2: <text>
row 3: <text>
row 4: <text>
row 5: <text>
row 6: <text>
```

No normal-screen row is reserved for:

- TX FIFO tail;
- `Tune:Hold`;
- mute/status text.

The visible transcript is six lines = 120 characters. The existing larger
history backing store may remain; render the latest six 20-character lines.

### Header/status behavior

Transient status and Tune indication move to row 0 so rows 1-6 remain text-only.

Priority:

```text
active transient status
    else Tune:Hold
    else normal HH:MM / KeyIn / KeyOut / WPM / Volume header
```

Operation UI behavior remains unchanged.

## Memory chooser behavior

Alt-alone toggles the M1-M5 overlay.

While the overlay is visible:

```text
row 1: M1:<message>
row 2: M2:<message>
row 3: M3:<message>
row 4: M4:<message>
row 5: M5:<message>
row 6: blank
```

The transcript continues accumulating behind the overlay.

Selecting any memory closes the overlay immediately:

- plain `1..5` while overlay is visible -> queue M1-M5 and close overlay;
- direct `Alt+1..5` remains supported;
- if the overlay is visible when direct Alt+1..5 resolves, close it;
- Tune and Operation continue to close the overlay;
- Alt-alone can still close it without transmitting.

After selection, the six-line transcript is immediately visible again.

## Unified transcript semantics

The transcript should represent text that was actually sent/decoded, not the
private unsent queue.

### Physical paddle / straight key

Keep the existing finalized decode path:

```text
keyer_engine CHAR        -> append char
keyer_engine WORD_SPACE  -> append one space
BACKSPACE / ENTER        -> existing history semantics
```

### Keyboard and M1-M5 automatic TX

Do **not** append automatic text merely when it is queued.

Instead add a small pure `tx_engine` text event so the controller can append a
character when that automatic character has actually completed transmission.

Required behavior:

- each completed automatic character emits exactly one transcript character;
- one or more queued spaces that create a word gap emit one transcript space;
- canceled/unsent queued text never appears;
- backspaced unsent text never appears;
- M1 repeat appears again as each repeated character is transmitted;
- keyboard and M1-M5 share the same automatic-transcript path.

Prefer a bounded one-event/take-event seam such as:

```c
bool tx_engine_take_text_event(tx_engine_t *tx, char *out_ch);
```

Exact naming is implementation choice.

Do not make UI code inspect or parse the TX FIFO.

## Display scheduling

Remove the T038 automatic-TX Display suppression.

Return to normal live rendering cadence:

```text
if now >= next_render:
    render
    next_render = now + 50 ms
```

Input can still force an earlier render.

This is intentional so the unified automatic-TX transcript can advance live.

The known Cardputer ADV speaker pop may therefore remain. It is **not** a T040
acceptance failure unless T040 introduces a new functional/audio regression
beyond the already-known issue.

Do not merge any T039 dirty-row or continuous-tone provider work into T040.

## Private TX FIFO

Keep the bounded `tx_engine` FIFO and all existing semantics needed for:

- keyboard queueing;
- TxDelay;
- Enter/start;
- Backspace of unsent text;
- M1-M5;
- M1 repeat;
- automatic cancel/preemption.

It is no longer a visible UI concept.

Do not rename or redesign it unless a tiny internal change is required for the
transcript event.

## Tests

### UI tests

Prove:

- Alt opens M1-M5 overlay;
- plain 1..5 selection closes overlay immediately;
- direct Alt+1..5 still selects memory;
- selection restores transcript view immediately;
- transcript continues accumulating while overlay is hidden by the chooser;
- normal screen renders six transcript rows;
- no TX-tail content is rendered specially in row 6;
- transient status uses row 0;
- Tune indication uses row 0;
- Operation remains unchanged.

### TX-engine transcript tests

Prove:

- a completed automatic `E` emits one `E` event;
- a completed multi-element character emits only one event;
- queued spaces collapse to one transcript word separator when their word gap is
  scheduled;
- cancel before character completion emits no character;
- cancel of queued future characters emits none of those future characters;
- Backspace of unsent text does not produce transcript text;
- M1 repeat emits repeated transcript characters on the repeated cycle;
- existing Morse/key timing assertions remain unchanged.

### Controller integration

Prove the same six-line transcript accepts text from:

1. paddle decode;
2. keyboard automatic TX;
3. M1/M1-M5 automatic TX.

Automatic text must come from the TX-engine completion event, not direct UI
enqueue.

Restore live Display calls during automatic TX. Update/remove the old R4
starvation assertion accordingly.

### Existing gates

Keep passing:

- full Linux CTest;
- portable units;
- focused Keyer/Audio tests;
- architecture/dependency/platform checks;
- real ADV firmware build;
- clean Keyer ELF build;
- `git diff --check`.

No public MiniShell API change is expected.

## Hardware acceptance

On Cardputer ADV verify:

1. Alt shows M1-M5.
2. Pressing 1 queues M1 and the chooser disappears immediately.
3. M1 text appears in the normal six-line transcript as it transmits.
4. Keyboard TX text appears in the same transcript.
5. Paddle-decoded text appears in the same transcript.
6. Rows 1-6 have no dedicated TX-tail/status row.
7. Status/Tune indication uses the top row.
8. Operation and existing shortcuts still work.
9. Ctrl+C releases resources.

The already-known sound pop is recorded but is not solved by T040.

## Non-goals

Do not:

- solve the remaining speaker pop;
- add resident Audio tasks/capabilities;
- change ADV Display provider implementation;
- remove the private automatic TX FIFO;
- change Morse ratios, KeyOut, K3, or sidetone shape;
- change MiniFT8;
- change public MiniShell APIs.

## Branch workflow

Use:

```text
codex/T040-keyer-unified-transcript
```

Codex handoff:

1. implement only T040;
2. run all required software/build gates;
3. record changed files, tests and resource evidence here;
4. set `Status: REVIEW`;
5. commit and push;
6. return exact SHA;
7. no PR and no hardware testing by Codex.

Supervisor reviews the actual diff before hardware validation.
