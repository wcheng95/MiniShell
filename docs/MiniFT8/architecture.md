# MiniFT8-V3 Architecture

## Purpose

MiniFT8-V3 is an FT8 application built on the MiniShell API. The application core remains platform-independent: it must not depend on ESP-IDF, Linux, NuttX, board GPIO numbering, UART ownership, RTC chips, GPS/NMEA libraries, or host-specific test mechanisms.

MiniShell owns platform resources and presents stable services. MiniFT8 owns FT8 protocol behavior, scheduling, application state, and presentation policy.

## Top-level boundary

```text
hardware / OS / platform providers
            |
            v
        MiniShell API
            |
            v
      MiniFT8-V3 app
```

MiniFT8 talks sideways neither to hardware nor to platform providers. All external state enters through MiniShell services and is coordinated through the application controller.

## Application structure

```text
                 MiniShell API
                      |
                      v
               app_controller
               /     |      \
              /      |       \
         AutoSeq   storage   UI model
            |                   |
            v                   v
      TX lifecycle          ui_shell
                                |
                                v
                         presentation adapter

MiniShell Audio
      |
      v
rx_audio_adapter
      |
      v
RxFrontend
      |
      v
RxSlotFramer
      |
      v
Ft8Engine
      |
      v
RxResultBuilder
      |
      v
app_controller
```

`app_controller` is the application-level coordinator. Modules do not bypass it with ad-hoc side channels.

## Time and slot ownership

MiniShell Time/Location owns platform UTC. MiniFT8 never reads an RTC, GPS receiver, NTP source, or host clock directly.

For live FT8 operation:

```text
MiniShell UTC
    -> initial slot_id + sample_offset
    -> sample count owns progress within the stream
```

This keeps the timing boundary deterministic: UTC establishes alignment, while audio sample count maintains slot progress.

`Ft8Engine` has no clock dependency.

## GPS and location ownership

MiniFT8-V2 owned GPS hardware and NMEA parsing directly. V3 intentionally moves that responsibility beneath MiniShell:

```text
GPS hardware
    |
    v
MiniShell platform GPS provider
    |
    +--> UTC synchronization
    +--> RTC persistence policy
    +--> live latitude/longitude
             |
             v
      Time/Location API
             |
             v
      MiniFT8 app_controller
             |
             v
   4-char Maidenhead working grid
```

On Cardputer ADV the current MiniShell GPS backend reuses V2's PORTA wiring, UART1 RX=G1 / TX=G2, and 115200/9600 auto-baud behavior. Those facts are platform implementation details and must not appear in portable FT8 modules.

The configured grid in `station.txt` is the persistent/manual station grid. A live GPS fix may temporarily replace the working grid used by AutoSeq and outgoing messages, but GPS never overwrites the configured station grid. When the live fix disappears, the controller restores the manual grid.

This preserves V2 user-visible GPS behavior while enforcing V3 ownership rules.

## RX architecture

Production RX is:

```text
MiniShell Audio
12 kHz / S16 / stereo
    -> rx_audio_adapter
    -> RxFrontend
       6 kHz mono float
    -> RxSlotFramer
       sample-count framing
       exact 960-sample engine blocks
    -> Ft8Engine
       monitor
       candidate search
       likelihood / LDPC / CRC
       hash store
       typed message codec
       exact-payload dedupe
    -> Ft8ProtocolSlot
    -> RxResultBuilder
    -> RxBatch
    -> app_controller
```

The normal path streams audio and does not retain a full raw PCM slot.

### RX module responsibilities

`rx_audio_adapter`
- owns interaction with MiniShell Audio for RX;
- validates/open/closes the selected endpoint;
- converts public Audio reads into the frontend input contract;
- does not own UTC or FT8 protocol logic.

`RxFrontend`
- transforms the MiniShell audio format to the engine's 6 kHz mono float stream;
- owns frontend sample-format/channel/rate conversion only.

`RxSlotFramer`
- owns slot framing after initial UTC alignment;
- advances by sample count;
- presents exact engine blocks and handles slot remainder policy.

`Ft8Engine`
- owns FT8 DSP and protocol decode mechanics;
- has no platform knowledge;
- never reads the clock, filesystem, display, keyboard, GPS, RTC, or radio directly.

`RxResultBuilder`
- translates engine protocol output into application RX records/batches.

`app_controller`
- coordinates RX results with application state, AutoSeq, TX policy, live Time/Location state, storage, and the UI model.

## AutoSeq and TX

AutoSeq is policy, not hardware control. It receives station identity and decoded protocol events, updates QSO state, and produces TX intent.

The station identity supplied to AutoSeq includes the current working grid. Therefore a live GPS-derived grid can be applied by `app_controller` without AutoSeq knowing anything about GPS.

TX lifecycle owns application-level timing/state transitions for a queued intent. Actual radio/audio providers remain MiniShell/platform concerns.

## Configuration and storage

`config_service` owns MiniFT8 application configuration such as callsign and manually configured grid.

`storage_service` owns MiniFT8 application persistence through MiniShell Filesystem.

Platform configuration such as GPS UART baud belongs to MiniShell platform state, not FT8 `station.txt`.

This distinction is intentional:

```text
station.txt                  MiniShell platform state
-----------                  ------------------------
callsign                     GPS detected baud
manual FT8 grid              hardware/provider details
FT8 application settings     platform resource choices
```

## UI and presentation

The controller builds one complete `UiModel`. `ui_shell` decides which application state is visible and converts user input into `AppAction`. Presentation adapters render the resulting frame and translate platform-neutral MiniShell input events.

The application currently supports:

```text
DESKTOP   30 x 8
ADV       20 x 7
```

The ADV UI contract is defined separately in `docs/MiniFT8/ui.md`.

## Ownership rules

The working rules are:

1. Hardware has one platform owner.
2. Applications use MiniShell services, not hardware drivers.
3. `app_controller` coordinates application modules; modules do not create hidden side channels.
4. DSP modules receive data and return data; they do not own I/O.
5. Persistent application configuration is distinct from transient provider state.
6. UTC source selection, GPS parsing, RTC updates, and live-location source selection belong to MiniShell Time/Location.
7. FT8 may derive FT8-specific state such as Maidenhead grid from generic MiniShell location data.
8. Host mocks/providers remain beneath the MiniShell boundary and never leak into the application core.

## Testing strategy

The architecture is designed for layered verification:

```text
pure module unit tests
    -> controller/reference tests
    -> Linux MiniShell integration
    -> ADV cross-build
    -> real hardware/provider tests
```

Linux and test providers exercise the same MiniShell-facing application code used on ADV. Hardware tests should therefore validate the platform provider boundary rather than requiring a second FT8 code path.

## Current state

The decode RX path through RX-7 is complete. AutoSeq and TX lifecycle foundations exist. MiniShell Time/Location now provides the ownership boundary needed for RTC and GPS-backed UTC/location, and MiniFT8-V3 consumes live location without owning GPS hardware.

Remaining radio/audio provider integration and UI behavior continue as separate workstreams rather than being folded into the GPS/Time/Location boundary.
