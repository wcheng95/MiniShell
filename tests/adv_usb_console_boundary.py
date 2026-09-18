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
assert 'xTaskCreate(capture_task' not in provider
ordered(prepare, 'capture task create begin',
        'xTaskCreateStatic(capture_task, "uac_capture", sizeof(capture_stack),',
        'nullptr, 4, capture_stack, &capture_tcb)', 'capture task create %s')
assert 'StackType_t capture_stack[4096 / sizeof(StackType_t)]' in provider
assert 'StaticTask_t capture_tcb;' in provider
ordered(release, 'xSemaphoreTake(capture_done',
        'while (eTaskGetState(capture_handle) != eSuspended)',
        'vTaskDelete(capture_handle)', 'capture_handle = nullptr;')
capture = provider.split('void capture_task(void *)', 1)[1].split('bool release()', 1)[0]
assert 'if (!device || !started) { vTaskDelay(1); continue; }' in capture
assert 'pdMS_TO_TICKS(5)' not in capture
defaults = (root / 'platform/adv/sdkconfig.defaults').read_text()
assert 'CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y' in defaults.splitlines()
assert 'vTaskDelete(nullptr)' not in capture
ordered(capture, 'xSemaphoreGive(capture_done)', 'for (;;) vTaskSuspend(nullptr);')
composition = (root / 'platform/adv/main/CMakeLists.txt').read_text()
assert ('set_property(SOURCE "${MINISHELL_ROOT}/apps/ft8/src/app_controller/app_controller.c"\n'
        '        APPEND PROPERTY COMPILE_DEFINITIONS FT8_DEFAULT_FREQ_OSR=1)') in composition
assert "adv_console_resume_after_usb" not in provider
ordered(console.split("int adv_console_suspend_for_usb(void)", 1)[1],
        "if (s_host_console.suspended) return -1;", "usb_serial_jtag_driver_uninstall()")
uart_begin = console.split("static int debug_uart_begin(void)", 1)[1].split(
    "static int debug_uart_end(void)", 1)[0]
uart_end = console.split("static int debug_uart_end(void)", 1)[1].split(
    "int adv_console_prepare(void)", 1)[0]
ordered(uart_begin, "s_uart_pins = true;",
        "uart_set_pin(UART_NUM_0, GPIO_NUM_3, GPIO_NUM_6,",
        "UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return -1;",
        "TX=GPIO3 RX=GPIO6 115200")
for setting in (".baud_rate = 115200", ".data_bits = UART_DATA_8_BITS",
                ".parity = UART_PARITY_DISABLE", ".stop_bits = UART_STOP_BITS_1",
                ".flow_ctrl = UART_HW_FLOWCTRL_DISABLE"):
    assert setting in uart_begin
ordered(uart_end, "uart_driver_delete(UART_NUM_0) != ESP_OK",
        "return -1;", "s_uart_installed = false;", "if (s_uart_pins)",
        "esp_err_t tx = gpio_reset_pin(GPIO_NUM_3);",
        "esp_err_t rx = gpio_reset_pin(GPIO_NUM_6);",
        "if (tx != ESP_OK || rx != ESP_OK)", "return -1;", "s_uart_pins = false;")
assert "GPIO_NUM_4" not in console and "GPIO_NUM_5" not in console
assert "TX=GPIO4" not in console and "RX=GPIO5" not in console
assert "s_previous_log = esp_log_set_vprintf(uart_log)" in console
assert "esp_log_set_vprintf(s_previous_log)" in console
assert "uart_read_bytes" not in console  # No competing FT8 input policy.
print("ADV USB console ownership / diagnostic integration: PASS")
