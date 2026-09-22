# Field baseline milestone — 2026-09-21

Status: **OPERATIONAL / FIELD USE**

This milestone closes the current implementation round. MiniShell, MiniFT8 and
Mini-CW are in operational shape on Cardputer ADV, and the next phase is field
use plus learning rather than active feature development.

## Baseline

The hardware-accepted code baseline before this documentation-only coherence
pass is:

```text
48909320646f3a989c812a88052b0b7420a32dc7
```

Subsequent commits in this pass update documentation only; they do not change the
accepted runtime behavior.

## Accepted field stack

### MiniShell / ADV

- 20x7 resident text console with USB mirror;
- 50 physical rows of resident scrollback;
- Fn+Up / Fn+Down moves five rows per press;
- FATFS `/flash` and optional `/sd`;
- WebFS file management;
- `usbmsc` handoff/remount workflow;
- runtime external ELF loading;
- generic Display colors plus 2-pixel row-0 separator;
- normalized Cardputer keyboard input;
- optional RTC/GPS-backed Time/Location providers.

### MiniFT8

- live QMX USB-UAC RX on ADV;
- V2-compatible 12.64-second decode cadence;
- AutoSeq / logging / QSO state;
- physical QMX CAT TX on ADV and Linux;
- band sync and current-day QSO view;
- RX ordering/lifetime;
- ADV status presentation:
  - TX separator white when idle, red during physical TX;
  - CQ rows green;
  - reply-to-me rows red by factual classification;
  - regular rows white;
- bare `;` / `.` paging on RX/TX.

### Mini-CW

- Mini-CW V1.2-derived Keyer-mode behavior under MiniShell;
- clean paddle and automatic M1 audio;
- settings persistence;
- fixed 20x7 header/UI;
- full callsign/operator lookup;
- white/green/cyan color UI with green separator;
- compact daily transcript logging;
- dual-quote safe note mode.

The superseded `apps/keyer` K0-K6 implementation/build/tests have been removed. Historical T038/T040 task records remain in Git/docs for reference; Mini-CW is the current field CW application.

## Known operational caveat

ADV currently tears down the ESP32-S3 USB Host, UAC and CDC class drivers when
the final QMX user exits. A later MiniFT8 launch therefore performs a fresh USB
enumeration. Real QMX hardware can fail that second enumeration; power-cycling
QMX restores the first-enumeration path.

Linux hosts normally keep QMX enumerated in the kernel, so restarting MiniFT8
there only reopens ALSA/CDC handles and does not exercise the same device-reset
path.

A persistent/reusable ADV QMX host session is a future architecture improvement,
not a blocker for this field milestone.

## Deferred work

No implementation task is active at this milestone. Deferred items include:

- permanent `RR73` locator/terminal disambiguation;
- stable Linux QMX endpoint discovery;
- reusable/persistent ADV QMX USB-host ownership;
- any new features discovered through field use.

## Operating mode after this milestone

Use the ADV in the field and collect real operational feedback. Prefer learning,
measurement and observation over speculative feature additions. New engineering
work should start only from a concrete field need or a clearly bounded learning
experiment.
