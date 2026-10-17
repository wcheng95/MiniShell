# ADV QMX UAC RX — Current Code Reading Guide

This document describes the current MiniFT8-V3.088 ADV receive path around:

    platform/adv/adv_audio_uac.cpp

It is a code-reading companion for learning both the audio path and the C/C++
syntax used by the ADV backend.

For implementation history, experiments, hardware bring-up evidence, and the
reasons behind many choices, see:

    docs/project/codex/T017-adv-usb-uac-rx.md

T017 is historical. This document describes the current accepted baseline.

## 1. Current receive path

    QMX USB UAC
    48 kHz / signed 24-bit packed / stereo
            |
            v
    platform/adv/adv_audio_uac.cpp
    USB host + UAC class + capture worker
    native -> canonical conversion
            |
            v
    2048-frame canonical ring
    12 kHz / S16 / stereo
            |
            v
    MiniShell Audio API
            |
            v
    rx_audio_adapter
            |
            v
    RxFrontend
    12 kHz stereo -> 6 kHz mono float
            |
            v
    RxSlotFramer
    960 samples/block
            |
            v
    ft8_monitor
    FFT / waterfall
            |
            v
    ft8_decoder
    candidate search / LDPC / CRC / decode

The ownership boundary is simple:

    MiniShell / ADV
        owns USB transport, buffering, and native-format conversion

    MiniFT8
        owns channel interpretation, FT8 timing, FFT, decoding, and protocol behavior

adv_audio_uac.cpp must not know FT8 symbols, waterfall semantics, AutoSeq, or UI.

## 2. Current format facts

QMX native input:

    sample rate     48000 Hz
    sample format   signed 24-bit packed little-endian
    channels        2
    bytes/frame     6

MiniShell Audio presented to MiniFT8:

    sample rate     12000 Hz
    sample format   signed 16-bit
    channels        2
    bytes/frame     4

The ADV provider:

1. unpacks signed 24-bit little-endian samples;
2. reduces S24 to S16;
3. decimates 48 kHz -> 12 kHz by 4;
4. preserves channel order;
5. puts canonical stereo frames into the ring.

It does not downmix. RxFrontend owns stereo -> mono and 12 kHz -> 6 kHz.

## 3. Important state near the top of adv_audio_uac.cpp

The file uses an unnamed C++ namespace:

    namespace {
        ...
    }

Everything inside is private to this translation unit. This is similar to using
file-local static definitions in C.

Important current objects include:

    native_data[2304]      native UAC read buffer
    ring                   lazily allocated canonical ring
    connections            UAC connection-event queue
    capture_device         worker-owned UAC device handle
    capture_stack          static 4096-byte capture-task stack
    reserved / started     public Audio RX lifecycle state
    connected              UAC stream connection state
    rx_generation          invalidates stale/in-flight reads
    read_errors
    transfer_errors

QMX identity is fixed:

    VID  0x0483
    PID  0xA34C

The private Audio handle is declared constexpr, meaning it is a compile-time
constant.

## 4. Current task ownership and priority

Current ADV scheduling:

    USB Host event task
        priority 5
        core 1

    UAC class background task
        priority 5
        core 0

    CDC-ACM driver task
        priority 4
        core 0

    CDC owner task
        priority 3

    UAC capture task
        priority 4
        static 4096-byte stack

    MiniShell foreground app
        priority 1
        core 0

    MiniFT8 decoder worker
        priority 4
        core 1
        static 8192-byte stack

The capture worker keeps USB acquisition independent from foreground application,
UI, logging, and decoder scheduling.

The decoder is now separate from the foreground task, but the ring still
decouples USB acquisition from application consumption and absorbs bounded jitter.

## 5. Canonical ring

Defined in:

    platform/adv/adv_audio_uac_buffer.h

Current size:

    ADV_UAC_RING_FRAMES = 2048
    sample storage      = 8192 bytes
    duration            ~= 170.7 ms at 12 kHz

The ring is lazily allocated from internal heap when uac:qmx Audio RX opens.

The allocation uses internal 8-bit-capable heap because contiguous internal RAM
matters on ESP32-S3. Largest free block can therefore matter as much as total
free heap.

Current V3.088 operator observation with FT8 loaded:

    free heap       ~87 KiB
    largest block   ~31 KiB

These are observations, not fixed resource guarantees.

## 6. allocate_ring()

Read this function early.

High-level sequence:

    report heap
        |
    allocate adv_uac_buffer_t
        |
    zero object
        |
    enter critical section
        |
    increment rx_generation
    publish ring pointer
        |
    leave critical section
        |
    report success/failure

C++ syntax:

    auto *allocated = static_cast<adv_uac_buffer_t *>(...);

auto lets the compiler infer the declared type.

static_cast<T *>(...) is the explicit C++ cast form used instead of a C cast.

## 7. Critical sections and atomics

Two synchronization styles appear.

Short compound state updates use:

    portENTER_CRITICAL(&lock);
    ...
    portEXIT_CRITICAL(&lock);

Simple cross-task/core flags use C++ atomics, for example:

    std::atomic<bool> started{false};
    std::atomic<bool> connected{false};
    std::atomic<unsigned> read_errors{0};

An atomic variable does not make the whole provider state atomic. Compound state
still needs the explicit lock.

## 8. USB Host ownership

host_task() owns the ESP-IDF USB Host Library lifecycle.

Current configuration:

    interrupt level   LEVEL1
    RX FIFO lines     91
    NP TX FIFO lines  18
    P TX FIFO lines   91
    core              1
    priority          5

The task installs USB Host, services usb_host_lib_handle_events(), and owns the
uninstall loop.

The installing task/core stays alive until host resources really release. This is
an ownership rule, not just a scheduling detail.

## 9. USB PHY / console handoff

ESP32-S3 USB Host and USB Serial/JTAG share the internal USB PHY.

Current ownership:

    normal shell
        USB Serial/JTAG

    FT8/QMX USB Host window
        USB Serial/JTAG suspended
        UART0 diagnostics
            TX GPIO3
            RX GPIO6
            115200

    after clean host teardown
        UART0 released
        USB Serial/JTAG restored

Implementation:

    platform/adv/adv_console.c

The Cardputer display and keyboard remain the normal local MiniFT8 UI.

## 10. prepare()

prepare() creates the shared QMX USB session.

Conceptually:

    create queues/semaphores
        |
    obtain console/USB-PHY lease
        |
    start USB Host task
        |
    install CDC infrastructure
        |
    install UAC class
        |
    start static UAC capture task

UAC Audio and CDC Serial share one physical QMX session but remain independent
MiniShell resources.

    one physical QMX USB session
        |
        +-- Audio RX: uac:qmx
        |
        +-- Serial:   serial:qmx

## 11. UAC connection events

driver_event() receives UAC connection notification.

It intentionally does little:

    callback
        -> make small Connection record
        -> queue it
        -> return

Heavy device-open/read work belongs to the capture owner task.

This is a useful embedded design pattern: callbacks report facts; owners do work.

## 12. Capture task

The capture worker owns capture_device.

Conceptually:

    connection event
        |
    open QMX UAC RX
        |
    verify 48000 / 24 / 2
        |
    read native blocks
        |
    convert + decimate
        |
    push canonical frames into ring

The task is priority 4 with a static 4096-byte stack.

When no device is available or logical Audio is not started, it blocks for one
real FreeRTOS tick rather than spinning.

## 13. Native buffer geometry

The source contains:

    DMA_ATTR uint8_t native_data[2304];

2304 bytes means:

    2304 / 6 bytes/native frame = 384 native frames
    384 / 48000                 = 8 ms
    after /4 decimation         = 96 canonical frames

So a normal native block represents about 8 ms of QMX audio.

DMA_ATTR requests memory placement appropriate for DMA use.

## 14. Conversion and decimation phase

Do not assume each USB read begins a fresh decimation cycle.

The conversion state keeps phase across arbitrary native chunk boundaries:

    take every fourth 48-kHz frame
    and remember where "every fourth" continues across reads

This is transport conversion only.

Later RxFrontend does:

    12 kHz stereo S16
        -> channel combine/select
        -> 6 kHz mono float

## 15. Overflow and transport loss

Transport loss is explicit.

Examples:

- UAC transfer error;
- canonical ring overflow;
- disconnect;
- patched UAC driver reporting skipped/failed ISO data.

The provider marks a new loss epoch and eventually returns:

    MINI_ERR_DISCONTINUITY

The ring does not overwrite unread audio and pretend continuity was preserved.

## 16. Current V3.088 response to discontinuity

For live MiniFT8 RX:

    MINI_ERR_DISCONTINUITY
        |
        v
    reset RxFrontend conversion/decimation phase
        |
        v
    keep UTC slot schedule unchanged

Current live timing:

    UTC +0.00 s    start/reset slot capture
    UTC +12.64 s   submit decode
    next UTC       next slot starts

A damaged slot can contain fewer than 79 valid waterfall blocks. Its deadline
still stays fixed, and the following UTC boundary starts a clean next slot.

The older T017 timing-pending/re-anchor behavior predates T088 and is not the
current live V3.088 rule.

## 17. Audio lifecycle

### rx_open()

For uac:qmx:

    validate requested canonical format
    allocate canonical ring
    acquire shared QMX USB session
    return private Audio handle

Accepted format:

    12000 Hz / S16 / 2 channels

Other endpoints delegate to the previous/base ADV Audio provider.

### rx_start()

Starts logical Audio delivery.

Physical USB acquisition remains owned independently below this API.

### rx_read()

Important behavior:

- takes at most 256 canonical frames per call;
- reports one discontinuity when continuity was lost;
- returns NOT_READY when QMX is not connected;
- WAIT_NONE does not deliberately block;
- finite waits use a deadline and one-tick sleeps.

### rx_stop()

Stops logical delivery and reports provider statistics.

Current T083 behavior keeps physical UAC drained across logical FT8 TX pauses
rather than tearing the physical USB stream down.

### rx_close()

Stops logical Audio, frees the ring, and releases the shared QMX session when no
Audio/Serial/discovery owner remains.

## 18. Why CDC/Serial is also in adv_audio_uac.cpp

Current adv_audio_uac.cpp also owns the ADV QMX CDC byte transport.

The filename is now narrower than the physical-session responsibility.

The architecture is still:

    adv_audio_uac.cpp
        shared QMX USB Host session
        raw UAC transport
        raw CDC transport

    MiniShell Audio
        uac:qmx

    MiniShell Serial
        serial:qmx

    MiniFT8 radio_qmx
        CAT meaning:
        MD6 / FR0 / FT0 / FA / TX / RX / TA

The platform layer transports CDC bytes. MiniFT8 decides what those bytes mean.

## 19. Current bare ADV ft8 composition

Current wrapper:

    platform/adv/main/ft8_static.c

Bare:

    M$> ft8

normally composes:

    --rx  uac:qmx
    --cat serial:qmx

unless explicit options override them.

That is ADV composition policy, not a platform dependency inside portable
MiniFT8.

## 20. QMX re-enumeration limitation

Supported:

    start FT8 with QMX absent
    later first attachment
    enumerate and run

Not guaranteed:

    finish a QMX session
    fully tear down USB Host
    immediately enumerate the same QMX again without resetting QMX

A new enumeration can fail until QMX is reset or power-cycled. This appears to
match the practical V2/QMX limitation.

## 21. Current decoder scheduling

Do not use the original T017 synchronous-decoder model for current ADV.

V3.088:

    capture / waterfall
        foreground/core-0 application path

    decoder
        core 1
        priority 4
        static 8192-byte stack

Live slot ownership:

    0.00 .. 12.64 s
        current slot owns capture + FFT/waterfall

    12.64 s
        submit decode

    12.64 .. 15.00 s
        keep draining Audio
        discard before FFT

    next UTC
        next slot starts even if prior decode is still finishing

This V3.088 baseline removed the field-observed zero-decode problem.

## 22. C++ syntax worth learning in this file

### namespace { ... }

Unnamed namespace = translation-unit-private C++ definitions.

### constexpr

Compile-time constant.

### auto

Compiler infers the variable type.

### static_cast<T>()

Explicit C++ conversion.

### references: T &name

Example shape:

    bool close_cdc(cdc_acm_dev_hdl_t &cdc_device)

The argument is an alias to the caller's variable, not a copied handle. The
function can change the caller's handle itself.

### nullptr

C++ null pointer literal.

### std::atomic<T>

Atomic scalar state shared across tasks/cores.

### std::min<T>(a, b)

C++ standard-library minimum; the explicit T is a template type argument.

### extern "C"

Exports a C++ function with C linkage so C code can call the symbol.

### brace initialization

Examples:

    std::atomic<bool> connected{false};
    Connection item{address, interface};

This is standard C++ initialization syntax.

## 23. Recommended reading order inside adv_audio_uac.cpp

Do not read the file top-to-bottom on the first pass.

Suggested order:

    1. private/global state at top
    2. allocate_ring() / free_ring()
    3. loss()
    4. driver_event()
    5. host_task()
    6. prepare()
    7. rx_open()
    8. rx_start()
    9. rx_read()
    10. rx_stop()
    11. rx_close()
    12. capture_task()
    13. CDC/Serial path
    14. release/cleanup path

Then read:

    platform/adv/adv_audio_uac_buffer.h

After that move upward through MiniFT8:

    rx_audio_adapter.c
    rx_frontend.c
    rx_slot_framer.c
    ft8_monitor.c
    ft8_decoder.c

## 24. Four questions for each function

For each function ask:

1. What does this function own?
2. Which task/core calls it?
3. What state can another task change concurrently?
4. What is the smallest data passed across the boundary?

That maps directly to the MiniShell design rules:

    Modular
    Ownership
    Decouple
    KISS

For adv_audio_uac.cpp, ownership is more important than clever C++ syntax. Once
ownership is clear, most of the syntax becomes mechanical.
