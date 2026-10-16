# T085 — Compact and order-independent `date` setter

Status: TESTING

## Intent

Make the portable MiniShell `date` utility easier and less error-prone while
keeping one small command grammar.

Bare `date` continues to print the current UTC date and time. Setting accepts
one or two compact operands:

```text
date
date YYYYMMDD
date HHMMSS
date YYYYMMDD HHMMSS
date HHMMSS YYYYMMDD
```

The two setter operand types are unambiguous by exact length:

```text
YYYYMMDD   exactly 8 decimal digits
HHMMSS     exactly 6 decimal digits
```

This is a MiniShell utility task, unrelated to MiniFT8 production code. Do not
change the MiniFT8 About version.

## Current behavior

`apps/date/main/date.c` currently:

- bare `date` reads and prints UTC;
- setter accepts `YYYY-MM-DDTHH:MM:SS` or
  `YYYY-MM-DD HH:MM:SS`;
- setting requires `MINI_TIMELOC_CAP_SET_UTC` and `utc_set`;
- date validation includes leap-year/month/day checks;
- successful set reads back and prints the resulting UTC value.

ADV statically packages the same portable source through
`platform/adv/main/date_static.c`.

## Required behavior

### Bare command

```text
date
```

Read UTC once and print the current date and time using the existing display
format:

```text
YYYY-MM-DD HH:MM:SS UTC
```

Do not change the output format in this task.

### One operand

```text
date YYYYMMDD
```

- parse and validate the supplied date;
- read current UTC once;
- preserve the current hour/minute/second from that snapshot;
- set the combined UTC;
- read back and print the result using the normal output format.

```text
date HHMMSS
```

- parse and validate the supplied time;
- read current UTC once;
- preserve the current year/month/day from that snapshot;
- set the combined UTC;
- read back and print the result.

The preserved half must come from one coherent `utc_get()` snapshot. Do not
independently re-read date and time pieces.

### Two operands

Accept exactly one date operand and one time operand in either order:

```text
date 20261015 143012
date 143012 20261015
```

Both produce the same requested UTC.

Reject two date operands or two time operands.

### Validation

Date:

```text
YYYYMMDD
YYYY 1970..9999
MM   01..12
DD   valid for year/month, including leap years
```

Time:

```text
HHMMSS
HH 00..23
MM 00..59
SS 00..59
```

Every character must be decimal. Do not silently accept prefixes, suffixes,
spaces inside an operand, punctuation, or partial values.

Examples that must fail:

```text
date 2026-10-15
date 14:30:12
date 1430
date 20260229
date 240000
date 126060
date 20261015 20261016
date 143000 154000
date 20261015 143000 extra
```

Architect clarification: classify exactly eight digits as a date and exactly
six digits as a time. `date 202610` is valid and sets 20:26:10.

Leap-day example that must succeed:

```text
date 20280229
```

### Setter capability

Preserve the current MiniShell Time/Location ownership:

- application uses only the public MiniShell API;
- do not call platform/OS/RTC functions directly;
- setting still requires `MINI_TIMELOC_CAP_SET_UTC` and `utc_set`;
- lack of setter capability remains `date: setting time unsupported`;
- `utc_get` failure remains a normal command failure.

When one operand is supplied, `utc_get` is required before `utc_set` because
the unspecified half must be preserved.

## Grammar simplification

Remove the old setter forms:

```text
YYYY-MM-DDTHH:MM:SS
YYYY-MM-DD HH:MM:SS
```

Do not retain a compatibility parser in parallel. MiniShell ABI/command syntax is
not frozen, and one compact grammar is simpler to explain and test.

Update usage text to describe the compact forms concisely, for example:

```text
usage: date [YYYYMMDD] [HHMMSS]
```

The implementation may word the usage line equivalently, but it must not imply
that date must precede time.

## Implementation constraints

Keep this local to the portable `date` app and its focused tests/build wiring.

Do not:

- add a resident shell command;
- change the Time/Location public API;
- add platform-specific branches;
- add heap allocation;
- alter RTC/backend behavior;
- change MiniFT8 code or version;
- create a generic date/time parsing framework.

Prefer small helpers that classify one operand as DATE, TIME, or INVALID and
reuse the existing civil-date conversion code.

## Tests

Add a focused host unit test around the real portable `date` source using a
fake MiniShell Console + Time/Location API. The test must not require permission
to change the host OS clock.

Cover at minimum:

1. bare `date` calls `utc_get` and prints date + time;
2. date-only set preserves current time;
3. time-only set preserves current date;
4. date-then-time and time-then-date produce the same UTC;
5. leap day `20280229` succeeds;
6. invalid non-leap `20260229` fails;
7. invalid `240000` and `126060` fail;
8. duplicate DATE/DATE fails;
9. duplicate TIME/TIME fails;
10. old punctuated setter syntax fails;
11. more than two operands fails;
12. no SET_UTC capability reports unsupported without calling `utc_set`;
13. successful setter reads back/prints the resulting value.

The test may include `date.c` into a focused test translation unit with the app
entry renamed, or use another equally small technique. Do not expose parser
functions as a new public API solely for testing.

Run:

```text
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
git diff --check
```

Also build ADV because the same portable source is statically packaged there:

```text
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
```

## Manual acceptance

Linux/pc-1:

```text
M$> date
```

must print current date and time.

Setter behavior can be hardware-accepted on ADV, where MiniShell owns the RTC
setter:

```text
M$> date 20261015
M$> date 143012
M$> date 20261015 143012
M$> date 143012 20261015
```

Confirm one-field forms preserve the unspecified half and two-field forms work
in either order.

## Files expected

Likely:

```text
apps/date/main/date.c
tests/date_test.c
CMakeLists.txt
docs/project/codex/T085-date-compact-args.md
```

Avoid unrelated changes.

## Codex handoff requirements

Use branch:

```text
codex/T085-date-compact-args
```

Keep one reviewable implementation commit. In this task packet record:

```text
Implementation summary
Files changed
Behavior/invariants preserved
Tests run and results
Hardware/manual validation still required
Known limitations or risks
Commit reference
```

## Codex handoff

### Implementation summary

Compact eight-digit dates and six-digit times are validated and accepted in
either order. Single operands preserve the unspecified half from exactly one
UTC snapshot. Successful setters read back and print UTC. The architect
confirmed `202610` is valid (20:26:10); its erroneous invalid example was
corrected. Old punctuated forms are removed.

### Files changed

- `apps/date/main/date.c`: compact parsing and snapshot-based partial setters.
- `tests/date_test.c`: real portable app with fake Console and Time/Location;
  grammar, date/time bounds, leap days, snapshot preservation, capability and
  API failures, and readback output.
- `CMakeLists.txt`: focused host test target and CTest registration.
- `tests/linux_resources.py`: use compact setter syntax; retain existing output
  and non-persistence assertions. This additional test file encoded the removed
  syntax and required a narrow update.
- This task packet: clarified example and implementation/test evidence.

### Behavior/invariants preserved

Public MiniShell APIs only; no heap, backend, RTC, public API, output-format,
or MiniFT8/version changes. Setter capability and function are still required.
Invalid input does not call the clock APIs. No implementation deviations beyond
the architect-approved correction of the contradictory example.

### Tests run and results

- `cmake -S . -B build-linux`: passed.
- `cmake --build build-linux -j8`: passed.
- `./build-linux/date_unit`: passed, including `202610` as 20:26:10.
- Final `ctest --test-dir build-linux --output-on-failure`: 129/129 passed
  (60.39 seconds), including the serial test.
- `git diff --check`: passed.
- `source /home/wei/projects/esp-idf/export.sh` then
  `idf.py -C platform/adv build`: passed with the final production source;
  ESP-IDF emitted its stdio.h `include_next` extension warning.

Initial full-suite run before clarification: 126/129 passed. `date_unit` failed
on the contradictory example, and `linux_resources` used removed syntax; both
have been corrected. `linux_serial_unit` failed its write-timeout assertion;
`ctest --test-dir build-linux --output-on-failure -R '^(linux_resources|linux_serial_unit)$'`
then passed 2/2 without serial code changes.

### Hardware/manual validation still required

Architect acceptance on Linux/pc-1 for bare `date`, and on ADV for the four
setter examples above, including preservation and operand order. Fake-clock
unit tests do not alter the host OS clock.

### Known limitations or risks

Hardware acceptance remains pending. One initial serial timeout assertion failed
and passed on rerun; no serial code was modified.

### Commit reference

The single implementation commit containing this handoff on
`codex/T085-date-compact-args` (SHA supplied in the chat handoff).


## Supervisor review

Reviewed implementation `c5455b396a2d4108db5ef256ff52edd687fea655`
against current `main`.

Result: **PASS for software review; advanced to TESTING.**

Accepted behavior:

- bare `date` still prints `YYYY-MM-DD HH:MM:SS UTC`;
- exactly eight digits are classified as `YYYYMMDD`;
- exactly six digits are classified as `HHMMSS`;
- one compact operand preserves the unspecified half from exactly one coherent
  pre-set UTC snapshot;
- one date plus one time is accepted in either order;
- duplicate date/date and time/time operands are rejected;
- old punctuated setter forms are rejected;
- invalid syntax does not call Time/Location getter/setter APIs;
- setter capability checks and successful readback behavior are preserved;
- no public MiniShell API, backend, RTC implementation, MiniFT8 code, or
  MiniFT8 About version changed.

The task packet originally listed `202610` among invalid examples, which
contradicted the architect's length-based grammar. The implementation correctly
treats it as the valid time `20:26:10` and records that clarification.

Accepted software evidence:

```text
Linux CTest: 129/129 PASS
focused date_unit: PASS
ADV build: PASS
git diff --check: PASS
```

An initial unrelated `linux_serial_unit` timeout assertion was flaky and passed
on rerun without serial-code changes; the final full suite passed.

Remaining gate: manual operator acceptance of bare display and compact setter
behavior, especially one-field preservation and two-field arbitrary order.
