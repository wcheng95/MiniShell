# T086 — Restore V2 RxTxLog durability policy

Status: READY

## Intent

Restore the pinned MiniFT8-V2 RxTxLog persistence behavior in MiniFT8-V3.

V2 appends RX/TX traffic records and closes the file without forcing a per-record
flash sync. V3 currently performs:

```text
open -> write -> sync -> close
```

for every RT record, including every decoded RX message. On an active band this
can make optional logging compete with RX/waterfall work.

T086 changes only RxTxLog durability:

```text
open -> write -> close
```

ADIF and Cabrillo durability are unchanged.

Pinned V2 reference:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

V2 reference behavior:

```text
log_rxtx_line(...)
    -> storage_append_text_locked_path(..., sync_to_flash=false)
```

## Required behavior

For `log_service_write_rt()` only:

- keep the same filename, timestamp, frequency, RX/TX line format and append mode;
- keep normal open/write/close error handling;
- do not require `mini_fs_api_t.sync` to exist;
- do not call `fs->sync()`;
- close the file exactly once after a successful open, including write failure;
- return false on open/write/close failure as before;
- do not change controller policy or `rxtx_log=` configuration behavior.

Do not weaken:

- ADIF copy-on-write sync-before-rename;
- Cabrillo sync-before-rename;
- any MiniShell filesystem API contract.

RxTxLog is best-effort operational trace data. A process/power loss may lose the
most recently buffered RT records, matching V2 policy.

## Ownership / design rules

- `log_service` continues to own FT8 log serialization and durability policy.
- MiniShell Filesystem continues to own the file transport.
- No platform-specific code enters MiniFT8.
- No background logger, queue, extra task, batching framework, or new setting.
- This task is a policy restoration, not a logging redesign.

## MiniFT8 version

This changes production code linked into MiniFT8.

Update `V -> 6 About` to:

```text
MiniFT8-V3.086
```

Update the matching UI assertion. Do not add generated version machinery.

## Tests

Extend focused log-service coverage so RT behavior is explicitly proven.

At minimum verify:

1. RX RT write appends the exact existing V3 line format.
2. TX RT write appends the exact existing V3 line format.
3. RT open uses WRITE|CREATE|APPEND.
4. RT write handles partial writes through `fs_write_all`.
5. successful RT write closes exactly once.
6. RT path succeeds when `fs.sync == NULL`.
7. RT write never calls sync when a fake sync callback is present.
8. open failure returns false and does not close an invalid handle.
9. write failure returns false and still closes the opened handle.
10. close failure returns false.
11. existing ADIF/Cabrillo tests still prove their sync behavior.
12. MiniFT8 About assertion is exactly `MiniFT8-V3.086`.

Do not weaken existing ADIF/Cabrillo failure-injection tests.

Run:

```text
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
git diff --check

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
```

## Hardware/manual validation

On ADV/QMX with `rxtx_log=1`:

- receive several decoded messages;
- exit FT8 normally;
- confirm the current `RTYYMMDD.txt` contains the expected RX records;
- if a TX occurs, confirm its T record is also present;
- verify ordinary FT8 UI/RX behavior remains intact.

Active-band zero-decode frequency is useful diagnostic evidence for the broader
repair, but T086 alone is not required to prove that symptom fully eliminated.

## Expected files

Likely:

```text
apps/ft8/src/log_service/log_service.c
tests/ft8_log_service_test.c
apps/ft8/src/ui_shell/ui_shell.c
tests/ft8_ui_smoke.c
docs/MiniFT8/development.md
docs/project/codex/T086-ft8-rxtxlog-v2-durability.md
```

Avoid unrelated changes.

## Branch / handoff

Use:

```text
codex/T086-ft8-rxtxlog-v2-durability
```

Record:

```text
Implementation summary
Files changed
Behavior/invariants preserved
Tests run and results
Hardware/manual validation still required
Known limitations or risks
Commit reference
```
