# JS8 / JS8Chat for MiniShell

Status: **JS8Chat v0.1 architecture frozen; implementation not started.**

JS8Chat is a **compile-in MiniShell application**, following the same overall integration model as MiniFT8. It is not a separate product or repository boundary.

The application goal is the smallest useful JS8Call-compatible keyboard-chat endpoint for MiniShell/ADV:

- JS8 Normal-mode TX/RX;
- multi-signal decode;
- heartbeat and automatic heartbeat ACK;
- heard/reachable station activity;
- `CQ` and `CQ FIELD`;
- standard and compound callsigns;
- multiple local peer conversations;
- directed free-text chat;
- Huffman and JSC TX/RX;
- compact `ACK` and `73`;
- simple QSO logging.

The frozen interoperability reference is **JS8Call-improved v3.0.3**. Current upstream master is only a secondary compatibility check and does not automatically change v0.1.

## MiniShell integration

JS8Chat is built into MiniShell and consumes MiniShell services:

```text
MiniShell
|
+-- built-in apps
|   +-- FT8
|   +-- JS8Chat
|   +-- ...
|
+-- platform/services
    +-- audio / USB-UAC
    +-- display / keyboard
    +-- GPS / RTC / UTC time
    +-- filesystem / logging
    +-- radio/platform services
```

JS8Chat does not own hardware drivers. Its DSP/protocol/application core should remain modular enough for Linux/host tests, but the deployed application is part of MiniShell.

## Canonical documents

- `architecture.md` — frozen v0.1 architecture and MiniShell boundary.
- `js8-phy.md` — JS8 physical-layer/DSP reference notes.
- `application-protocol.md` — frozen application protocol subset.
- `jsc-dictionary.md` — JSC TX/RX dictionary and storage design.

Implementation/validation items such as golden vectors, JSC TX packing, DSP benchmarking, receive-context collision tests, and on-air interoperability are intentionally not architecture blockers.

Implementation-support assets already live under `apps/js8chat/`, including the pinned v3.0.3 JSC generator and checked-in RX dictionary resource.
