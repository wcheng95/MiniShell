#!/usr/bin/env python3
"""T017: report silent RX losses in espressif/usb_host_uac 1.3.3.

Generate a build-local copy; never mutate the managed component. The source
hash is from the registry package's CHECKSUMS.json. Any dependency change needs
explicit review, even if the replacement text still happens to match.
"""

import argparse
import hashlib
from pathlib import Path


SOURCE_SHA256 = "2d7549c7e4657744b92079c2c226b558e1934e587ab5030d80974f392eedf75e"
CALLBACK_SHA256 = "4a86d9067ae3b1c9f1900188fae2b2cb59b6dfd9840e3f938d3c79f600a280d2"
BEGIN = "static void stream_rx_xfer_done(usb_transfer_t *in_xfer)\n{"
END = "\nstatic void stream_tx_xfer_submit"
NOTIFY = "uac_host_user_interface_callback(iface, UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR);"


def patch_callback(source):
    if hashlib.sha256(source.encode()).hexdigest() != CALLBACK_SHA256:
        raise ValueError("UAC RX callback differs from pinned 1.3.3; review T017 patch")
    replacements = [
        ('            ESP_LOGD(TAG, "RX Ringbuffer overflow");',
         '            ESP_LOGW(TAG, "T017 RX loss: native-ring-overflow");\n'
         '            ' + NOTIFY),
        ('                if (in_xfer->isoc_packet_desc[i].status != USB_TRANSFER_STATUS_COMPLETED) {\n'
         '                    // copy data to ringbuffer\n'
         '                    ESP_LOGD(TAG, "Bad RX Isoc packet %d status %d", i, in_xfer->isoc_packet_desc[i].status);\n'
         '                    continue;\n'
         '                }',
         '                if (in_xfer->isoc_packet_desc[i].status != USB_TRANSFER_STATUS_COMPLETED) {\n'
         '                    int requested_num_bytes = in_xfer->isoc_packet_desc[i].num_bytes;\n'
         '                    if (in_xfer->isoc_packet_desc[i].status == USB_TRANSFER_STATUS_SKIPPED) {\n'
         '                        uint8_t *packet = in_xfer->data_buffer + i * requested_num_bytes;\n'
         '                        memset(packet, 0, requested_num_bytes);\n'
         '                        ESP_LOGW(TAG, "T017 RX pad: skipped-isoc packet=%d bytes=%d",\n'
         '                                 i, requested_num_bytes);\n'
         '                        if (_ring_buffer_push(iface->ringbuf, packet, requested_num_bytes, 0) != ESP_OK) {\n'
         '                            ESP_LOGW(TAG, "T017 RX loss: native-ring-push");\n'
         '                            ' + NOTIFY + '\n'
         '                        }\n'
         '                    } else {\n'
         '                        ESP_LOGW(TAG, "T017 RX loss: bad-isoc packet=%d status=%d",\n'
         '                                 i, in_xfer->isoc_packet_desc[i].status);\n'
         '                        ' + NOTIFY + '\n'
         '                    }\n'
         '                    continue;\n'
         '                }'),
        ('                _ring_buffer_push(iface->ringbuf, in_xfer->data_buffer + i * requested_num_bytes, actual_num_bytes, 0);',
         '                if (_ring_buffer_push(iface->ringbuf, in_xfer->data_buffer + i * requested_num_bytes, actual_num_bytes, 0) != ESP_OK) {\n'
         '                    ESP_LOGW(TAG, "T017 RX loss: native-ring-push");\n'
         '                    ' + NOTIFY + '\n'
         '                }'),
        ('        usb_host_transfer_submit(in_xfer);',
         '        if (usb_host_transfer_submit(in_xfer) != ESP_OK) {\n'
         '            ESP_LOGW(TAG, "T017 RX loss: resubmit");\n'
         '            ' + NOTIFY + '\n'
         '        }'),
    ]
    for before, after in replacements:
        if source.count(before) != 1:
            raise ValueError("UAC patch anchor mismatch")
        source = source.replace(before, after)
    return source


def patch_source(source):
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise ValueError("UAC source differs from registry 1.3.3; review T017 patch")
    text = source.decode()
    begin = text.index(BEGIN)
    end = text.index(END, begin)
    return (text[:begin] + patch_callback(text[begin:end]) + text[end:]).encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("output must not overwrite the managed source")
    try:
        patched = patch_source(args.source.read_bytes())
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != patched:
        args.output.write_bytes(patched)


if __name__ == "__main__":
    main()
