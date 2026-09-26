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

# Only this task and its helpers own the handle/driver. Callbacks publish state.
worker = function('void cdc_task(')
execute = function('void cdc_execute(')
assert 'cdc_acm_dev_hdl_t cdc_device = nullptr;' in worker
assert 'cdc_mutex' not in provider
assert provider.count('cdc_acm_host_open(') == worker.count('cdc_acm_host_open(') == 1
assert provider.count('cdc_acm_host_data_tx_blocking(') == execute.count('cdc_acm_host_data_tx_blocking(') == 1
assert provider.count('cdc_acm_host_close(') == function('bool close_cdc(').count('cdc_acm_host_close(') == 1
assert provider.count('close_cdc(') == worker.count('close_cdc(') + 1
assert provider.count('cdc_execute(') == worker.count('cdc_execute(') + 1
callback = function('void cdc_event(')
assert 'cdc_device' not in callback and 'ESP_LOG' not in callback
for signature in ('mini_result_t serial_open(', 'mini_result_t serial_write(', 'mini_result_t serial_close(', 'bool release()'):
    assert 'cdc_device' not in function(signature) and 'cdc_acm_host_data_tx_blocking' not in function(signature)
assert 'xQueueReceive(cdc_requests, &request, pdMS_TO_TICKS(100))' in worker
for signature in ('mini_result_t rx_stop(', 'mini_result_t rx_start('):
    body = function(signature)
    assert not any(op in body for op in ('cdc_', 'usb_host_', 'uac_host_device_', 'release(', 'prepare('))
assert 'adv_uac_loss(ring)' in function('mini_result_t rx_stop(')
assert '++rx_generation' in function('mini_result_t rx_start(')
assert '(!streaming && !started)' in function('void capture_task(')
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
#include <thread>
#include <mutex>
#include <condition_variable>
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
using cdc_acm_dev_hdl_t = void *;
thread_local bool in_owner;
constexpr esp_err_t ESP_OK = 0, ESP_ERR_TIMEOUT = 1, ESP_ERR_INVALID_STATE = 2;
bool serial_reserved, cdc_running = true, cdc_unplugged, quit;
bool cdc_ready = true;
std::atomic<bool> cdc_inflight{false};
uint32_t cdc_generation;
struct CdcRequest { uint32_t generation, size; int64_t deadline_us; uint8_t data[64]; };
struct CdcCompletion { uint32_t generation; esp_err_t result; };
constexpr int cdc_requests = 1, cdc_completions = 2;
bool session_ready, session_dirty, reserved, discovery_held;
bool prepare_ok = true, release_ok = true;
int prepares, releases, writes;
std::string transmitted;
uint32_t driver_timeout, advance_ms;
int64_t now;
esp_err_t tx_result;
std::mutex queue_lock;
std::condition_variable driver_cv;
std::thread owner;
CdcRequest queued;
CdcCompletion completed;
bool request_queued, completion_queued, driver_entered, stuck, unblock, inject_stale;
static int64_t esp_timer_get_time() { return now; }
static TickType_t xTaskGetTickCount() { return (TickType_t)(now / 10000); }
static void vTaskDelay(int) { now += 10000; }
static bool prepare() { ++prepares; return prepare_ok; }
static bool release() { ++releases; return release_ok; }
void cdc_execute(void *, const CdcRequest &);
static void xQueueOverwrite(int queue, const CdcCompletion *completion) {
    assert(queue == cdc_completions && !cdc_inflight);
    std::lock_guard<std::mutex> guard(queue_lock);
    completed = *completion; completion_queued = true;
}
static int xQueueSend(int queue, const CdcRequest *request, TickType_t wait) {
    assert(queue == cdc_requests && wait == 0 && !request_queued);
    queued = *request; request_queued = true; return pdTRUE;
}
static int xQueueReceive(int queue, CdcCompletion *completion, TickType_t wait) {
    assert(queue == cdc_completions);
    if (request_queued) {
        now += (int64_t)advance_ms * 1000;
        request_queued = false; driver_entered = false; unblock = false;
        CdcRequest request = queued;
        assert(!owner.joinable());
        owner = std::thread([request] { in_owner = true; cdc_execute((void *)1, request); });
        if (stuck) {
            std::unique_lock<std::mutex> guard(queue_lock);
            driver_cv.wait(guard, [] { return driver_entered; });
        } else owner.join();
    }
    std::lock_guard<std::mutex> guard(queue_lock);
    if (inject_stale && wait) {
        inject_stale = false;
        *completion = {cdc_generation - 1, ESP_OK}; return pdTRUE;
    }
    if (completion_queued) {
        *completion = completed; completion_queued = false; return pdTRUE;
    }
    if (wait) now = (now / 10000 + wait) * 10000;
    return 0;
}
static esp_err_t cdc_acm_host_data_tx_blocking(void *device, const uint8_t *data,
                                             uint32_t size, uint32_t timeout) {
    assert(in_owner);
    assert(device && data && size);
    ++writes; driver_timeout = timeout;
    std::unique_lock<std::mutex> guard(queue_lock);
    driver_entered = true; driver_cv.notify_one();
    if (stuck) driver_cv.wait(guard, [] { return unblock; });
    if (tx_result == ESP_OK) transmitted.append((const char *)data, size);
    return tx_result;
}
static void finish_late() {
    { std::lock_guard<std::mutex> guard(queue_lock); unblock = true; driver_cv.notify_one(); }
    owner.join(); stuck = false;
}
'''
harness += execute
harness += r'''
constexpr minishell_backend_audio_t handle = 0x554143u;
minishell_services_port_t base = {};
adv_uac_buffer_t *ring;
std::atomic<bool> started{false};
uint32_t rx_generation;
bool rx_pause_discontinuity;
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
    cdc_ready = false;
    assert(serial_open(nullptr, "serial:qmx", &serial) == MINI_ERR_NOT_READY);
    assert(now >= 3000000 && now <= 3020000 && !session_dirty);
    cdc_running = false;
    assert(serial_open(nullptr, "serial:qmx", &serial) == MINI_ERR_NOT_READY);
    cdc_running = true; cdc_ready = true;
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
    assert(count == 9 && driver_timeout == 170);
    advance_ms = 0;
    tx_result = ESP_ERR_TIMEOUT;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 10) == MINI_ERR_TIMEOUT && count == 0);
    assert(serial_reserved);
    assert(serial_write(nullptr, serial, bytes, 9, &count, MINI_WAIT_NONE) == MINI_ERR_TIMEOUT);
    assert(driver_timeout == 10); // Zero budget never submits a command.
    assert(serial_write(nullptr, serial, bytes, 9, &count, MINI_WAIT_FOREVER) == MINI_ERR_IO);
    assert(driver_timeout == UINT32_MAX / configTICK_RATE_HZ);
    tx_result = ESP_ERR_INVALID_STATE;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 200) == MINI_ERR_IO && count == 0);
    int before = writes;
    cdc_unplugged = true;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 200) == MINI_ERR_NOT_READY && count == 0);
    assert(writes == before && serial_reserved);
    cdc_unplugged = false; cdc_ready = false;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 200) == MINI_ERR_NOT_READY);
    cdc_ready = true; tx_result = ESP_OK;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 200) == MINI_OK && count == 9);
    char too_large[65] = {};
    assert(serial_write(nullptr, serial, too_large, sizeof(too_large), &count, 200) == MINI_ERR_INVALID);
    // Production owner execution blocks inside the fake driver. Foreground's
    // clock/waits remain deterministic and do not depend on wall-clock sleeps.
    transmitted.clear(); stuck = true;
    int64_t begin = now; before = writes;
    {
        char caller[] = "TX;";
        assert(serial_write(nullptr, serial, caller, 3, &count, 10) == MINI_ERR_TIMEOUT);
        memset(caller, 'x', 3);
    }
    assert(now - begin == 10000 && count == 0 && writes == before + 1 && cdc_inflight);
    begin = now;
    assert(serial_write(nullptr, serial, "RX;", 3, &count, 200) == MINI_ERR_NOT_READY);
    assert(now == begin && writes == before + 1);
    finish_late();
    assert(transmitted == "TX;" && !cdc_inflight); // Provider-owned copy survived caller.
    // Old queued completion is drained; an injected stale ID cannot report
    // success for a later request whose actual driver result is failure.
    tx_result = ESP_ERR_INVALID_STATE; inject_stale = true;
    assert(serial_write(nullptr, serial, "RX;", 3, &count, 200) == MINI_ERR_IO && count == 0);
    tx_result = ESP_OK;
    // Sub-millisecond queue/wakeup overhead still leaves one 100 Hz tick for TA.
    now += 123;
    assert(serial_write(nullptr, serial, "TA1500.00;", 10, &count, 10) == MINI_OK);
    assert(driver_timeout == 10 && count == 10);
    advance_ms = 20; before = writes;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 10) == MINI_ERR_TIMEOUT);
    assert(writes == before); // Expired queued command never reaches driver.
    advance_ms = 0;
    // A queued/wedged write also consumes wakeup delay from the same deadline.
    stuck = true; advance_ms = 3; now += 123;
    begin = now;
    assert(serial_write(nullptr, serial, bytes, 9, &count, 10) == MINI_ERR_TIMEOUT);
    assert(now - begin <= 10000 && driver_timeout == 7 && cdc_inflight);
    finish_late(); advance_ms = 0;
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
    cdc_ready = false;
    int before_prepare = prepares;
    assert(adv_qmx_discovery_begin() == MINI_OK && discovery_held && session_ready);
    assert(prepares == before_prepare + 1);
    int64_t held_now = now;
    for (unsigned i = 0; i < 200; ++i) {
        serial = 99;
        assert(serial_open(nullptr, "serial:qmx", &serial) == MINI_ERR_NOT_READY);
        assert(serial == 0 && now == held_now && releases == before);
        assert(prepares == before_prepare + 1 && session_ready);
    }
    cdc_ready = true;
    assert(serial_open(nullptr, "serial:qmx", &serial) == MINI_OK);
    assert(serial_close(nullptr, serial) == MINI_OK && releases == before && session_ready);
    assert(adv_qmx_discovery_end() == MINI_OK && releases == before + 1 && !session_dirty);
    assert(!discovery_held);
}
'''
functions = '\n'.join(function(s) for s in ('bool release_unused()', 'bool acquire_session()',
    'mini_result_t serial_open(', 'mini_result_t serial_write(', 'mini_result_t serial_close(',
    'void loss()', 'mini_result_t rx_open(', 'mini_result_t rx_start(',
    'mini_result_t rx_stop(', 'mini_result_t rx_close(',
    'extern "C" mini_result_t adv_qmx_discovery_begin(',
    'extern "C" mini_result_t adv_qmx_discovery_end('))
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
    assert(transmitted.empty() && entries == 1 && discovery_held);
    now += 100000; ++polls;
    if (scenario == 2 && polls == 41) {
        event->type = MINI_KEY_EVENT_CHAR; event->codepoint = 'q';
        return MINI_OK;
    }
    if (polls == 40) {
        if (scenario == 1 || scenario == 2) {
            event->type = scenario == 1 ? MINI_KEY_EVENT_CHAR : MINI_KEY_EVENT_SPECIAL;
            event->codepoint = 'q'; event->key = MINI_KEY_ESCAPE;
            return MINI_OK;
        }
        if (scenario == 3) return MINI_ERR_IO;
        cdc_ready = true; // First attachment, later than the old 3 s deadline.
    }
    return MINI_ERR_TIMEOUT;
}
extern "C" int test_ft8_entry(int argc, char **argv) {
    assert(argc == 5 && strcmp(argv[2], "uac:qmx") == 0 && strcmp(argv[4], "serial:qmx") == 0);
    ++entries;
    if (scenario == 6) return 3; // Portable configuration/UI failure before CAT.
    assert(now == 0 && discovery_held && session_ready); // No pre-entry readiness wait.
    RadioControl radio = {};
    mini_result_t synced;
    int session_prepares = prepares, session_releases = releases;
    while ((synced = radio_control_open_qmx(&radio, &api, argv[4], 7074000)) == MINI_ERR_NOT_READY) {
        assert(now == (int64_t)polls * 100000);
        assert(!serial_reserved && radio.stream == MINI_SERIAL_INVALID);
        assert(prepares == session_prepares && releases == session_releases);
        mini_key_event_t event = {};
        mini_result_t input = key_read(&event, 100);
        if (input == MINI_OK) {
            if (event.type == MINI_KEY_EVENT_CHAR && event.codepoint == 'q') return 0;
            assert(event.key == MINI_KEY_ESCAPE); // Back, then a later Q quits.
        } else if (input != MINI_ERR_TIMEOUT) return 6;
    }
    if (synced != MINI_OK) return 12;
    assert(transmitted == "MD6;FR0;FT0;FA00007074000;");
    assert(!reserved); // Real radio adapter synchronized before RX acquisition.
    minishell_backend_audio_t audio;
    assert(rx_open(nullptr, argv[2], 12000, MINI_AUDIO_SAMPLE_S16, 2, &audio) == MINI_OK);
    assert(rx_start(nullptr, audio) == MINI_OK && started && serial_reserved);
    assert(radio_control_close(&radio) == MINI_OK && session_ready);
    assert(rx_close(nullptr, audio) == MINI_OK);
    assert(session_ready && discovery_held && releases == session_releases);
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
        transmitted.clear(); cdc_ready = false;
        prepare_ok = scenario != 4; cdc_running = scenario != 5;
        tx_result = scenario == 7 ? ESP_ERR_TIMEOUT : ESP_OK;
        int before_prepare = prepares, before_release = releases;
        char name[] = "ft8"; char *argv[] = {name, nullptr};
        int result = minishell_app_ft8_main(1, argv);
        assert(result == (scenario == 3 ? 6 : scenario == 6 ? 3 : scenario >= 4 ? 12 : 0));
        assert(!serial_reserved && !reserved && !session_dirty && !ring && !discovery_held);
        assert(prepares == before_prepare + 1 && releases == before_release + 1);
        assert(polls == (scenario == 4 || scenario == 5 || scenario == 6 ? 0 : scenario == 2 ? 41 : 40));
        assert(announcements == 0);
        assert(entries == (scenario == 4 ? 0 : 1));
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
        '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-pthread', *include_args,
        str(path), *objects, '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('ADV QMX Serial callbacks / ownership / late first attach: PASS')
