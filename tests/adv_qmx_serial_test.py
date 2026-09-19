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
    ++writes; driver_timeout = timeout; return tx_result;
}
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
    'mini_result_t serial_open(', 'mini_result_t serial_write(', 'mini_result_t serial_close('))
with tempfile.TemporaryDirectory(prefix='t030-serial-') as temp:
    path = Path(temp) / 'serial.cpp'
    binary = Path(temp) / 'serial'
    path.write_text(harness + functions + cases)
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else 'c++', '-std=c++17',
        '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-I', str(root / 'include'),
        '-I', str(root / 'core/minishell_services'), str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('ADV QMX Serial callbacks / ownership: PASS')
