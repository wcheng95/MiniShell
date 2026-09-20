#!/usr/bin/env python3
"""ADV diagnostic patch for ESP-IDF 5.5.x DWC HCD isochronous skips.

Generate a build-local copy; never mutate ESP-IDF.  The patch records only
integer state in the HCD ISR when an ISO descriptor is NOT_EXECUTED.  Reporting
is deferred to the ADV capture task.
"""

import argparse
from pathlib import Path

STATE_ANCHOR = "static hcd_obj_t *s_hcd_obj = NULL;     // Note: \"s_\" is for the static pointer\n"
SKIP_ANCHOR = """        case USB_DWC_HAL_XFER_DESC_STS_NOT_EXECUTED:
            transfer->isoc_packet_desc[pkt_idx].status = USB_TRANSFER_STATUS_SKIPPED;
            break;
"""

STATE_CODE = r'''
typedef struct {
    uint32_t total;
    uint32_t packet_hist[64];
    uint32_t last_packet;
    uint32_t last_start_idx;
    uint32_t last_desc_idx;
    uint32_t last_stop_idx;
    uint32_t last_interval;
    uint32_t last_num_packets;
} t017_hcd_isoc_diag_t;

static volatile t017_hcd_isoc_diag_t s_t017_hcd_isoc_diag;

void hcd_dwc_t017_isoc_diag_snapshot(uint32_t *total,
                                     uint32_t *packet_hist,
                                     uint32_t hist_len,
                                     uint32_t *last_packet,
                                     uint32_t *last_start_idx,
                                     uint32_t *last_desc_idx,
                                     uint32_t *last_stop_idx,
                                     uint32_t *last_interval,
                                     uint32_t *last_num_packets)
{
    HCD_ENTER_CRITICAL();
    if (total) {
        *total = s_t017_hcd_isoc_diag.total;
    }
    if (packet_hist) {
        uint32_t count = hist_len < 64u ? hist_len : 64u;
        for (uint32_t i = 0; i < count; ++i) {
            packet_hist[i] = s_t017_hcd_isoc_diag.packet_hist[i];
        }
    }
    if (last_packet) {
        *last_packet = s_t017_hcd_isoc_diag.last_packet;
    }
    if (last_start_idx) {
        *last_start_idx = s_t017_hcd_isoc_diag.last_start_idx;
    }
    if (last_desc_idx) {
        *last_desc_idx = s_t017_hcd_isoc_diag.last_desc_idx;
    }
    if (last_stop_idx) {
        *last_stop_idx = s_t017_hcd_isoc_diag.last_stop_idx;
    }
    if (last_interval) {
        *last_interval = s_t017_hcd_isoc_diag.last_interval;
    }
    if (last_num_packets) {
        *last_num_packets = s_t017_hcd_isoc_diag.last_num_packets;
    }
    HCD_EXIT_CRITICAL();
}
'''

SKIP_CODE = """        case USB_DWC_HAL_XFER_DESC_STS_NOT_EXECUTED:
            transfer->isoc_packet_desc[pkt_idx].status = USB_TRANSFER_STATUS_SKIPPED;
            ++s_t017_hcd_isoc_diag.total;
            if ((unsigned)pkt_idx < 64u) {
                ++s_t017_hcd_isoc_diag.packet_hist[pkt_idx];
            }
            s_t017_hcd_isoc_diag.last_packet = (uint32_t)pkt_idx;
            s_t017_hcd_isoc_diag.last_start_idx = buffer->flags.isoc.start_idx;
            s_t017_hcd_isoc_diag.last_desc_idx = (uint32_t)desc_idx;
            s_t017_hcd_isoc_diag.last_stop_idx = buffer->status_flags.stop_idx;
            s_t017_hcd_isoc_diag.last_interval = buffer->flags.isoc.interval;
            s_t017_hcd_isoc_diag.last_num_packets = transfer->num_isoc_packets;
            break;
"""


def patch_source(source: str) -> str:
    if source.count(STATE_ANCHOR) != 1:
        raise ValueError("ESP-IDF HCD state anchor changed; review diagnostic patch")
    if source.count(SKIP_ANCHOR) != 1:
        raise ValueError("ESP-IDF HCD NOT_EXECUTED anchor changed; review diagnostic patch")
    if "hcd_dwc_t017_isoc_diag_snapshot" in source:
        raise ValueError("HCD diagnostic patch already applied")
    source = source.replace(STATE_ANCHOR, STATE_ANCHOR + STATE_CODE + "\n")
    source = source.replace(SKIP_ANCHOR, SKIP_CODE)
    return source


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("output must not overwrite ESP-IDF source")
    try:
        patched = patch_source(args.source.read_text())
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != patched:
        args.output.write_text(patched)


if __name__ == "__main__":
    main()
