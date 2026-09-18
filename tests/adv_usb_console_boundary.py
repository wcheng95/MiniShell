#!/usr/bin/env python3
"""Guard the connection between the tested console lease and ADV ownership."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
provider = (root / "platform/adv/adv_audio_uac.cpp").read_text()
console = (root / "platform/adv/adv_console.c").read_text()


def ordered(source, *anchors):
    cursor = 0
    for anchor in anchors:
        cursor = source.index(anchor, cursor) + len(anchor)


prepare = provider.split("bool prepare()", 1)[1].split("mini_result_t rx_open", 1)[0]
release = provider.split("bool release()", 1)[1].split("bool prepare()", 1)[0]
ordered(prepare, "if (adv_console_begin_usb_host() != 0) return false;", "usb_host_install(&host)")
ordered(release, "xSemaphoreTake(capture_done", "xSemaphoreTake(cdc_done",
        "if (!close_capture() || !close_cdc()) return false;",
        "cdc_acm_host_uninstall()", "uac_host_uninstall()",
        "xSemaphoreTake(host_done", "if (host_installed) return false;",
        "adv_console_end_usb_host(host_installed || uac_installed || cdc_installed)")
ordered(provider, "if (usb_host_uninstall() == ESP_OK)", "host_installed = false;")
assert provider.count("adv_console_end_usb_host(") == 1
assert provider.count("adv_console_begin_usb_host(") == 1
assert "adv_console_resume_after_usb" not in provider
ordered(console.split("int adv_console_suspend_for_usb(void)", 1)[1],
        "if (s_host_console.suspended) return -1;", "usb_serial_jtag_driver_uninstall()")
assert "uart_set_pin(UART_NUM_0, GPIO_NUM_4, GPIO_NUM_5," in console
assert ".baud_rate = 115200" in console
assert "s_previous_log = esp_log_set_vprintf(uart_log)" in console
assert "esp_log_set_vprintf(s_previous_log)" in console
assert "uart_read_bytes" not in console  # No competing FT8 input policy.
print("ADV USB console ownership / diagnostic integration: PASS")
