#!/usr/bin/env python3
"""Execute the patched pinned RX callback with deterministic loss injection."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("patch_uac_rx", ROOT / "platform/adv/patch_uac_rx.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
fixture = (ROOT / "tests/fixtures/adv_uac_rx_1_3_3.c").read_text()
callback = fixture[fixture.index(patch.BEGIN):]
patched = patch.patch_callback(callback)
for invalid in (callback + "\n", patched):
    try:
        patch.patch_callback(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("callback drift / repeated application accepted")
try:
    patch.patch_source(b"unreviewed source")
except ValueError:
    pass
else:
    raise AssertionError("source hash guard bypassed")

HARNESS = r'''
#include <assert.h>
#include <stddef.h>
#include "adv_audio_uac_buffer.h"
#define ESP_OK 0
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
enum { USB_TRANSFER_STATUS_COMPLETED, USB_TRANSFER_STATUS_CANCELED,
       USB_TRANSFER_STATUS_SKIPPED, USB_TRANSFER_STATUS_NO_DEVICE,
       USB_TRANSFER_STATUS_ERROR };
enum { UAC_INTERFACE_STATE_ACTIVE = 1, UAC_HOST_DEVICE_EVENT_RX_DONE,
       UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR };
typedef struct { int status, num_bytes, actual_num_bytes; } packet_t;
typedef struct {
    int state;
    void *ringbuf;
    size_t ringbuf_size, ringbuf_threshold;
    size_t packet_size;
} uac_iface_t;
typedef struct {
    void *context;
    int status, actual_num_bytes, num_isoc_packets;
    uint8_t *data_buffer;
    packet_t isoc_packet_desc[2];
} usb_transfer_t;
static unsigned errors, pushes, submits;
static int push_result, submit_result;
static size_t native_length, last_push_bytes;
static adv_uac_buffer_t canonical;
static size_t _ring_buffer_get_len(void *ringbuf)
{ (void)ringbuf; return native_length; }
static int _ring_buffer_push(void *ringbuf, uint8_t *bytes, size_t count, int wait)
{
    (void)ringbuf; (void)bytes; (void)wait;
    ++pushes;
    last_push_bytes = count;
    if (!push_result) native_length += count;
    return push_result;
}
static int usb_host_transfer_submit(usb_transfer_t *transfer)
{ (void)transfer; ++submits; return submit_result; }
static void uac_host_user_interface_callback(uac_iface_t *iface, int event)
{
    (void)iface;
    if (event == UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) {
        ++errors;
        adv_uac_loss(&canonical);
    }
}
'''

CASES = r'''
int main(void)
{
    uint8_t data[12] = {0};
    for (unsigned test = 0; test < 11; ++test) {
        memset(&canonical, 0, sizeof(canonical));
        errors = pushes = submits = 0;
        push_result = submit_result = 0;
        native_length = last_push_bytes = 0;
        uac_iface_t iface = {UAC_INTERFACE_STATE_ACTIVE, NULL, 64, 12, 6};
        usb_transfer_t transfer = {&iface, USB_TRANSFER_STATUS_COMPLETED, 12, 2,
                                   data, {{0, 6, 6}, {0, 6, 6}}};
        switch (test) {
        case 1: native_length = 60; break; /* native ring overflow */
        case 2: transfer.isoc_packet_desc[0].status = USB_TRANSFER_STATUS_ERROR; break;
        case 3: push_result = -1; transfer.num_isoc_packets = 1; break;
        case 4: submit_result = -1; break;
        case 5: transfer.status = USB_TRANSFER_STATUS_ERROR; break;
        case 6: transfer.status = USB_TRANSFER_STATUS_CANCELED; break;
        case 7: transfer.status = USB_TRANSFER_STATUS_NO_DEVICE; break;
        case 8: iface.state = 0; break;
        case 9: transfer.isoc_packet_desc[0].actual_num_bytes = 0; break;
        case 10: transfer.isoc_packet_desc[0].status = USB_TRANSFER_STATUS_SKIPPED; break;
        }
        adv_uac_ticket_t in_flight = adv_uac_begin(&canonical);
        stream_rx_xfer_done(&transfer);
        bool lost = test >= 1 && test <= 5;
        assert(errors == (unsigned)lost);
        assert(canonical.pending == lost);
        assert(submits == (unsigned)(test < 5 || test == 9 || test == 10));
        if (test == 0 || test == 4 || test == 9 || test == 10) assert(pushes == 2);
        if (test == 10) assert(last_push_bytes == iface.packet_size);
        if (test == 1 || (test >= 5 && test <= 8)) assert(pushes == 0);
        if (test == 2 || test == 3) assert(pushes == 1);
        if (lost) {
            /* No canonical frames from a native read spanning a callback loss. */
            assert(adv_uac_feed(&canonical, in_flight, data, sizeof(data)));
            assert(canonical.head == canonical.tail);
            assert(adv_uac_ack(&canonical));
            assert(!adv_uac_ack(&canonical));
        }
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="t017-uac-patch-") as directory:
    source = Path(directory) / "callback.c"
    binary = Path(directory) / "callback"
    source.write_text(HARNESS + patched + CASES)
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else "cc", "-std=c11",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                    "-I", str(ROOT / "platform/adv"), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("UAC 1.3.3 patch: source guards and ten RX callback cases passed")
