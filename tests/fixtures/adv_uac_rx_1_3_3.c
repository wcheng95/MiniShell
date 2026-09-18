/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unmodified RX callback fixture from espressif/usb_host_uac 1.3.3 uac_host.c.
 * Upstream: espressif/esp-usb, 26f9973b2461f3452f2de03fd6b8c5738014b68d.
 * Used only to execute the T017 notification patch in host tests.
 */
static void stream_rx_xfer_done(usb_transfer_t *in_xfer)
{
    assert(in_xfer);

    uac_iface_t *iface = in_xfer->context;
    assert(iface);

    if (iface->state != UAC_INTERFACE_STATE_ACTIVE) {
        in_xfer->status = USB_TRANSFER_STATUS_CANCELED;
    }

    switch (in_xfer->status) {
    case USB_TRANSFER_STATUS_COMPLETED: {

        // if ringbuffer will overflow, notify user to read data
        size_t data_len = _ring_buffer_get_len(iface->ringbuf);
        if (data_len + in_xfer->actual_num_bytes >= iface->ringbuf_size) {
            uac_host_user_interface_callback(iface, UAC_HOST_DEVICE_EVENT_RX_DONE);
        }

        // if ringbuffer overflow (happens if user not read in above callback), the data will be dropped
        data_len = _ring_buffer_get_len(iface->ringbuf);
        if (data_len + in_xfer->actual_num_bytes > iface->ringbuf_size) {
            ESP_LOGD(TAG, "RX Ringbuffer overflow");
        } else {
            // else push data to ringbuffer
            for (int i = 0; i < in_xfer->num_isoc_packets; i++) {
                if (in_xfer->isoc_packet_desc[i].status != USB_TRANSFER_STATUS_COMPLETED) {
                    // copy data to ringbuffer
                    ESP_LOGD(TAG, "Bad RX Isoc packet %d status %d", i, in_xfer->isoc_packet_desc[i].status);
                    continue;
                }
                int requested_num_bytes = in_xfer->isoc_packet_desc[i].num_bytes;
                int actual_num_bytes = in_xfer->isoc_packet_desc[i].actual_num_bytes;
                // in UAC, the actual_num_bytes may less than requested_num_bytes
                // eg. the packet_size is 64, but the endpoint size is 100
                assert(requested_num_bytes >= actual_num_bytes);
                // copy data to ringbuffer
                _ring_buffer_push(iface->ringbuf, in_xfer->data_buffer + i * requested_num_bytes, actual_num_bytes, 0);
            }
        }
        // Relaunch transfer
        usb_host_transfer_submit(in_xfer);

        // if ringbuffer is reach the threshold, notify user to read out
        data_len = _ring_buffer_get_len(iface->ringbuf);
        if (data_len >= iface->ringbuf_threshold) {
            uac_host_user_interface_callback(iface, UAC_HOST_DEVICE_EVENT_RX_DONE);
        }

        return;
    }
    case USB_TRANSFER_STATUS_NO_DEVICE:
    case USB_TRANSFER_STATUS_CANCELED:
        // User is notified about device disconnection from usb_event_cb
        // No need to do anything
        return;
    default:
        // Any other error
        break;
    }

    ESP_LOGE(TAG, "Transfer failed, status %d", in_xfer->status);
    // Notify user about transfer or any other error
    uac_host_user_interface_callback(iface, UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR);
}
