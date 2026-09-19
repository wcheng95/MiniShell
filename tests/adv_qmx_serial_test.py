#!/usr/bin/env python3
"""Run production ADV Serial callbacks with deterministic CDC/clock faults."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
provider = (root / 'platform/adv/adv_audio_uac.cpp').read_text()

def function(signature):
    start = provider.index(signature)
    return provider[start:provider.index('\n}\n', start) + 3]

# Driver operations are excluded from the mock proof; lock ordering must cover
# both worker-side close/open and foreground blocking transfer.
worker = function('void cdc_task(')
assert worker.index('xSemaphoreTake(cdc_mutex') < worker.index('close_cdc()')
assert worker.index('cdc_acm_host_open(') < worker.index('xSemaphoreGive(cdc_mutex)')
assert 'if (ring && started && rx_generation == generation)' in function('void capture_task(')
assert 'if (ring) adv_uac_loss(ring)' in function('void loss()')
assert 'release(' not in function('mini_result_t rx_stop(')
assert 'prepare(' not in function('mini_result_t rx_start(')
for done in ('capture_done', 'cdc_done', 'host_done'):
    assert f'xSemaphoreTake({done}, pdMS_TO_TICKS(5000)) != pdTRUE' in function('bool release()')
configure = provider.split('extern "C" void adv_audio_uac_configure', 1)[1]
assert 'port->serial_capabilities = MINI_SERIAL_CAP_WRITE;' in configure
assert 'port->serial_write = serial_write;' in configure
assert 'port->serial_read =' not in configure

harness = r'''
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>
#include "adv_audio_uac_buffer.h"
extern "C" {
#include "radio_control.h"
int minishell_app_ft8_main(int, char **);
}
#include <cassert>
#include <cstdint>
#include <cstring>
#include "minishell_services.h"
constexpr minishell_backend_serial_t serial_handle = 0x434443u;
constexpr uint32_t configTICK_RATE_HZ = 100, portMAX_DELAY = UINT32_MAX;
using TickType_t = uint32_t;
#define pdMS_TO_TICKS(ms) ((ms) / 10)
constexpr int pdTRUE = 1;
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_ERR_TIMEOUT = 1, ESP_ERR_INVALID_STATE = 2;
int cdc_mutex;
bool serial_reserved, cdc_running = true, cdc_unplugged;
void *cdc_device = (void *)1;
bool session_ready, session_dirty, reserved;
bool prepare_ok = true, release_ok = true, lock_ok = true, locked;
int prepares, releases, writes;
std::string transmitted;
uint32_t driver_timeout, advance_ms;
int64_t now;
esp_err_t tx_result;
static int64_t esp_timer_get_time() { return now; }
static void vTaskDelay(int) { now += 10000; }
static bool prepare() { ++prepares; return prepare_ok; }
static bool release() { ++releases; return release_ok; }
static int xSemaphoreTake(int, TickType_t) {
    now += (int64_t)advance_ms * 1000;
    if (!lock_ok) return 0;
    assert(!locked); locked = true; return pdTRUE;
}
static void xSemaphoreGive(int) { assert(locked); locked = false; }
static esp_err_t cdc_acm_host_data_tx_blocking(void *device, const uint8_t *data,
                                             uint32_t size, uint32_t timeout) {
    assert(locked && device == cdc_device && data && size);
    ++writes; driver_timeout = timeout;
    if (tx_result == ESP_OK) transmitted.append((const char *)data, size);
    return tx_result;
}
'''
harness += r'''
constexpr minishell_backend_audio_t handle = 0x554143u;
minishell_services_port_t base = {};
adv_uac_buffer_t *ring;
std::atomic<bool> started{false};
uint32_t rx_generation;
std::atomic<unsigned> read_errors{0}, transfer_errors{0};
const char *tag = "test";
#define portENTER_CRITICAL(unused) ((void)0)
#define portEXIT_CRITICAL(unused) ((void)0)
static void ESP_LOGI(const char *, const char *, ...) {}
static bool allocate_ring() {
    assert(!ring); ring = (adv_uac_buffer_t *)calloc(1, sizeof(*ring)); return ring != nullptr;
}
static void free_ring() { free(ring); ring = nullptr; }
'''
cases = r'''
int main() {
    minishell_backend_serial_t serial = 99;
    assert(serial_open(nullptr, "serial:qmx", nullptr) == MINI_ERR_INVALID);
    for (const char *endpoint : {"bad", "serial:other", ""}) {
        assert(serial_open(nullptr, endpoint, &serial) == MINI_ERR_INVALID);
        assert(serial == 0 && prepares == 0);
    }
    assert(serial_open(nullptr, nullptr, &serial) == MINI_ERR_INVALID);
    prepare_ok = false;
    assert(serial_open(nullptr, "serial:qmx", &serial) == MINI_ERR_IO);
    assert(serial == 0 && !serial_reserved && !session_dirty);
    prepare_ok = true;
    cdc_device = nullptr;
    assert(serial_open(nullptr, "serial:qmx", &serial) == MINI_ERR_NOT_READY);
    assert(now >= 3000000 && now <= 3020000 && !session_dirty);
    cdc_running = false;
    assert(serial_open(nullptr, "serial:qmx", &serial) == MINI_ERR_NOT_READY);
    cdc_running = true; cdc_device = (void *)1;
    assert(serial_open(nullptr, "serial:qmx", &serial) == MINI_OK);
    assert(serial == serial_handle && serial_reserved);
    minishell_backend_serial_t second = 99;
    assert(serial_open(nullptr, "serial:qmx", &second) == MINI_ERR_TOO_MANY_OPEN);
    assert(second == 0);
    uint32_t count = 99;
    const char bytes[] = "raw bytes";
    assert(serial_write(nullptr, 0, bytes, 9, &count, 200) == MINI_ERR_BAD_HANDLE && count == 0);
    assert(serial_write(nullptr, serial, bytes, 9, nullptr, 200) == MINI_ERR_INVALID);
    assert(serial_write(nullptr, serial, nullptr, 0, &count, 0) == MINI_OK && count == 0);
    assert(serial_write(nullptr, serial, nullptr, 9, &count, 200) == MINI_ERR_INVALID && count == 0);
    assert(writes == 0);
    advance_ms = 30;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 200) == MINI_OK);
    assert(count == 9 && driver_timeout == 170 && !locked);
    advance_ms = 0;
    tx_result = ESP_ERR_TIMEOUT;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 10) == MINI_ERR_TIMEOUT && count == 0);
    assert(serial_reserved);
    assert(serial_write(nullptr, serial, bytes, 9, &count, MINI_WAIT_NONE) == MINI_ERR_TIMEOUT);
    assert(driver_timeout == 0);
    assert(serial_write(nullptr, serial, bytes, 9, &count, MINI_WAIT_FOREVER) == MINI_ERR_IO);
    assert(driver_timeout == UINT32_MAX / configTICK_RATE_HZ);
    tx_result = ESP_ERR_INVALID_STATE;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 200) == MINI_ERR_IO && count == 0);
    int before = writes;
    cdc_unplugged = true;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 200) == MINI_ERR_IO && count == 0);
    assert(writes == before && serial_reserved);
    cdc_unplugged = false; cdc_device = nullptr;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 200) == MINI_ERR_IO);
    cdc_device = (void *)1; tx_result = ESP_OK;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 200) == MINI_OK && count == 9);
    lock_ok = false;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 200) == MINI_ERR_TIMEOUT && count == 0);
    lock_ok = true;
    reserved = true; before = releases;
    assert(serial_close(nullptr, serial) == MINI_OK);
    assert(releases == before && session_ready && !serial_reserved);
    assert(serial_close(nullptr, serial) == MINI_ERR_BAD_HANDLE);
    assert(serial_write(nullptr, serial, bytes, 9, &count, 200) == MINI_ERR_BAD_HANDLE);
    assert(serial_open(nullptr, "serial:qmx", &serial) == MINI_OK);
    reserved = false; release_ok = false;
    assert(serial_close(nullptr, serial) == MINI_ERR_IO);
    assert(!serial_reserved && session_dirty);
    assert(serial_close(nullptr, serial) == MINI_ERR_BAD_HANDLE);
    assert(serial_open(nullptr, "serial:qmx", &serial) == MINI_ERR_IO);
    release_ok = true;
    assert(serial_open(nullptr, "serial:qmx", &serial) == MINI_OK);
    assert(serial_close(nullptr, serial) == MINI_OK && !session_dirty);
    before = releases;
    assert(release_unused() && releases == before);
}
'''
functions = '\n'.join(function(s) for s in ('bool release_unused()', 'bool acquire_session()',
    'mini_result_t serial_open(', 'mini_result_t serial_write(', 'mini_result_t serial_close(',
    'void loss()', 'mini_result_t rx_open(', 'mini_result_t rx_start(',
    'mini_result_t rx_stop(', 'mini_result_t rx_close(',
    'extern "C" mini_result_t adv_qmx_prepare_serial(',
    'extern "C" mini_result_t adv_qmx_release_unused('))
integration = r'''
static mini_api_t api;
static mini_serial_api_t serial_api;
static mini_key_input_api_t keys;
static mini_input_api_t input_api;
static mini_console_api_t console_api;
static int polls, entries, scenario, announcements;
extern "C" const mini_api_t *mini_api_get(void) { return &api; }
static mini_result_t public_open(const char *ep, mini_serial_t *out) {
    minishell_backend_serial_t backend = 0;
    mini_result_t result = serial_open(nullptr, ep, &backend);
    *out = (mini_serial_t)backend; return result;
}
static mini_result_t public_write(mini_serial_t serial, const void *data, uint32_t size,
                                  uint32_t *out, uint32_t timeout) {
    return serial_write(nullptr, serial, data, size, out, timeout);
}
static mini_result_t public_close(mini_serial_t serial) { return serial_close(nullptr, serial); }
static void console_write(const char *text) {
    if (strstr(text, "waiting for QMX")) ++announcements;
}
static mini_result_t key_read(mini_key_event_t *event, uint32_t timeout) {
    assert(timeout == 100 && session_ready && !serial_reserved && !reserved);
    assert(transmitted.empty() && entries == 0);
    now += 100000; ++polls;
    if (polls == 40) {
        if (scenario == 1 || scenario == 2) {
            event->type = scenario == 1 ? MINI_KEY_EVENT_CHAR : MINI_KEY_EVENT_SPECIAL;
            event->codepoint = 'q'; event->key = MINI_KEY_ESCAPE;
            return MINI_OK;
        }
        if (scenario == 3) return MINI_ERR_IO;
        cdc_device = (void *)1; // First attachment, later than the old 3 s deadline.
    }
    return MINI_ERR_TIMEOUT;
}
extern "C" int test_ft8_entry(int argc, char **argv) {
    assert(argc == 5 && strcmp(argv[2], "uac:qmx") == 0 && strcmp(argv[4], "serial:qmx") == 0);
    ++entries;
    if (scenario == 6) return 3; // Portable configuration/UI failure before CAT.
    RadioControl radio = {};
    mini_result_t synced = radio_control_open_qmx(&radio, &api, argv[4], 7074000);
    if (synced != MINI_OK) return 12;
    assert(transmitted == "MD6;FR0;FT0;FA00007074000;");
    assert(!reserved); // Real radio adapter synchronized before RX acquisition.
    minishell_backend_audio_t audio;
    assert(rx_open(nullptr, argv[2], 12000, MINI_AUDIO_SAMPLE_S16, 2, &audio) == MINI_OK);
    assert(rx_start(nullptr, audio) == MINI_OK && started && serial_reserved);
    assert(radio_control_close(&radio) == MINI_OK && session_ready);
    assert(rx_close(nullptr, audio) == MINI_OK);
    return 0;
}
static void test_late_attach() {
    serial_api.struct_size = sizeof(serial_api);
    serial_api.capabilities = MINI_SERIAL_CAP_WRITE;
    serial_api.open = public_open; serial_api.write = public_write; serial_api.close = public_close;
    keys.struct_size = sizeof(keys); keys.read = key_read;
    input_api.struct_size = sizeof(input_api); input_api.key = &keys;
    console_api.struct_size = sizeof(console_api); console_api.write = console_write;
    api.struct_size = sizeof(api); api.serial = &serial_api;
    api.input = &input_api; api.console = &console_api;
    for (scenario = 0; scenario < 8; ++scenario) {
        now = polls = entries = announcements = 0;
        transmitted.clear(); cdc_device = nullptr;
        prepare_ok = scenario != 4; cdc_running = scenario != 5;
        tx_result = scenario == 7 ? ESP_ERR_TIMEOUT : ESP_OK;
        int before_prepare = prepares, before_release = releases;
        char name[] = "ft8"; char *argv[] = {name, nullptr};
        int result = minishell_app_ft8_main(1, argv);
        assert(result == (scenario >= 3 && scenario != 6 ? 12 : scenario == 6 ? 3 : 0));
        assert(!serial_reserved && !reserved && !session_dirty && !ring);
        assert(prepares == before_prepare + 1 && releases == before_release + 1);
        assert(polls == (scenario == 4 || scenario == 5 ? 0 : 40));
        assert(announcements == (polls ? 1 : 0));
        assert(entries == (scenario == 0 || scenario == 6 || scenario == 7 ? 1 : 0));
        if (scenario != 0) assert(transmitted.empty());
    }
}
'''
cases = cases.rsplit('}', 1)[0] + 'test_late_attach();\n}\n'
parser = (root / 'apps/ft8/main/ft8_main.c').read_text().split('int main(', 1)[0]
wrapper_path = Path(sys.argv[2]) if len(sys.argv) > 2 else root / 'platform/adv/main/ft8_static.c'
wrapper = wrapper_path.read_text().split('int minishell_app_ft8_main', 1)[1]
includes = [root / p for p in ('include', 'core/minishell_services', 'platform/adv',
    'apps/ft8/include', 'apps/ft8/main', 'apps/ft8/src/app_controller',
    'apps/ft8/src/presentation_profile', 'apps/ft8/src/ui_shell', 'apps/ft8/src/radio_control')]
include_args = [arg for inc in includes for arg in ('-I', str(inc))]
with tempfile.TemporaryDirectory(prefix='t030-serial-') as temp:
    path = Path(temp) / 'serial.cpp'
    binary = Path(temp) / 'serial'
    path.write_text(harness + functions + integration + cases)
    wrapper_source = Path(temp) / 'wrapper.c'
    wrapper_source.write_text(parser + '\n#include "adv_internal.h"\n'
        + 'int test_ft8_entry(int, char **);\n#define adv_ft8_entry test_ft8_entry\n'
        + 'int minishell_app_ft8_main' + wrapper)
    objects = []
    for i, source in enumerate((wrapper_source,
            root / 'apps/ft8/src/presentation_profile/presentation_profile.c',
            root / 'apps/ft8/src/radio_control/radio_control.c',
            root / 'apps/ft8/src/radio_control/radio_qmx.c')):
        obj = Path(temp) / f'part{i}.o'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        *include_args, '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else 'c++', '-std=c++17',
        '-Wall', '-Wextra', '-Werror', '-Wpedantic', *include_args,
        str(path), *objects, '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('ADV QMX Serial callbacks / ownership / late first attach: PASS')
