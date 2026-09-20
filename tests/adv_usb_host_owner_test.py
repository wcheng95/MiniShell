#!/usr/bin/env python3
"""Execute production host owner/prepare/release with controlled RTOS/USB faults."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'platform/adv/adv_audio_uac.cpp').read_text()
def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]
owner, prepare, release = (function(s) for s in ('void host_task(', 'bool prepare()', 'bool release()'))
assert source.count('usb_host_install(') == owner.count('usb_host_install(') == 1
assert source.count('usb_host_uninstall(') == owner.count('usb_host_uninstall(') == 1
assert 'xTaskCreatePinnedToCore(host_task, "uac_host", 4096, nullptr,\n                                           5, nullptr, 1)' in prepare
assert 'host_task' not in release  # No replacement task/core on teardown failure.
assert 'host.intr_flags = ESP_INTR_FLAG_LEVEL1;' in owner and 'ESP_INTR_FLAG_SHARED' not in source
for field, value in [('rx', 91), ('nptx', 18), ('ptx', 91)]:
    assert f'host.fifo_settings_custom.{field}_fifo_lines = {value};' in owner
assert 'adv_console_dump_interrupts' not in source
console = (root / 'platform/adv/adv_console.c').read_text()
assert 'esp_intr_dump' not in console and 'funopen' not in console
assert prepare.index('xSemaphoreTake(host_ready') < prepare.index('if (host_start_result != ESP_OK)') < prepare.index('cdc_acm_host_install') < prepare.index('uac_host_install')
assert release.index('cdc_acm_host_uninstall') < release.index('uac_host_uninstall') < release.index('host_quit = true;') < release.index('xSemaphoreTake(host_done') < release.index('adv_console_end_usb_host')
assert 'ADV_APP_TASK_CORE 0' in (root / 'platform/adv/adv_apps.c').read_text()
assert 'xTaskCreateStatic(capture_task' in prepare and 'config.core_id = 0;' in prepare

harness = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_INVALID_STATE=1, pdTRUE=1, pdPASS=1, eSuspended=2;
constexpr int ESP_INTR_FLAG_LEVEL1=2, USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS=1;
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
struct Semaphore { std::mutex mutex; std::condition_variable cv; bool ready=false; };
using SemaphoreHandle_t=Semaphore*;
SemaphoreHandle_t capture_done, host_ready, host_done, cdc_done, cdc_mutex;
std::atomic<bool> quit{false}, host_quit{false}, unplugged{false}, cdc_unplugged{false};
std::atomic<bool> host_installed{false};
std::atomic<esp_err_t> host_start_result{ESP_ERR_INVALID_STATE};
bool started, capture_running, cdc_running, host_running, cdc_installed, uac_installed;
void *connections, *capture_handle;
unsigned char capture_stack[4096]; int capture_tcb;
struct Connection { int unused; };
struct usb_host_config_t { int intr_flags; struct {int rx_fifo_lines,nptx_fifo_lines,ptx_fifo_lines;} fifo_settings_custom; };
struct cdc_acm_host_driver_config_t { int driver_task_stack_size,driver_task_priority,xCoreID; };
struct uac_host_driver_config_t { bool create_background_task; int task_priority,stack_size,core_id; void (*callback)(); };
void driver_event() {} void cdc_task(void*) {} void capture_task(void*) {}
std::thread task;
thread_local int core=0;
std::thread::id install_thread;
std::atomic<bool> allow_install{true}, block_uninstall{false};
std::atomic<int> install_calls{0}, uninstall_calls{0}, events{0}, frees{0};
int install_error, tasks, class_calls, remaining_semaphores;
bool task_failure, ready_timeout, join_timeout, uac_failure, console_active, ready_taken;
Semaphore* xSemaphoreCreateBinary() { ++remaining_semaphores; return new Semaphore; }
Semaphore* xSemaphoreCreateMutex() { return xSemaphoreCreateBinary(); }
void vSemaphoreDelete(Semaphore* s) { --remaining_semaphores; delete s; }
void xSemaphoreGive(Semaphore* s) {
    std::lock_guard<std::mutex> guard(s->mutex); s->ready=true; s->cv.notify_one();
}
int xSemaphoreTake(Semaphore* s, int) {
    if (s==host_ready && ready_timeout) { allow_install=true; return 0; }
    if (s==host_done && join_timeout) { join_timeout=false; return 0; }
    std::unique_lock<std::mutex> guard(s->mutex);
    if (!s->cv.wait_for(guard,std::chrono::seconds(3),[&]{return s->ready;})) return 0;
    s->ready=false; guard.unlock();
    if (s==host_ready) ready_taken=true;
    if (s==host_done) task.join();
    return pdTRUE;
}
void *xQueueCreate(int,int) { return new int; }
void vQueueDelete(void *p) { delete static_cast<int*>(p); }
void vTaskDelay(int) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
void vTaskDelete(void*) {}
int eTaskGetState(void*) { return eSuspended; }
int xTaskCreatePinnedToCore(void (*fn)(void*), const char*, int stack, void*, int priority, void*, int target) {
    assert(target==1 && stack==4096 && priority==5 && core==0);
    if (task_failure) return 0;
    ++tasks; task=std::thread([=]{core=target; fn(nullptr);}); return pdPASS;
}
int xTaskCreate(void (*fn)(void*),const char*,int,void*,int,void*) {
    assert(fn==cdc_task); xSemaphoreGive(cdc_done); return pdPASS;
}
template<class... T> void* xTaskCreateStatic(void (*fn)(void*),const char*,T...) {
    assert(fn==capture_task); xSemaphoreGive(capture_done); return (void*)1;
}
int adv_console_begin_usb_host() { console_active=true; return 0; }
int adv_console_end_usb_host(bool busy) {
    assert(!busy && !task.joinable()); console_active=false; return 0;
}
int usb_host_install(const usb_host_config_t *config) {
    assert(core==1 && !host_installed && console_active);
    assert(config->intr_flags==ESP_INTR_FLAG_LEVEL1);
    assert(config->fifo_settings_custom.rx_fifo_lines==91 && config->fifo_settings_custom.nptx_fifo_lines==18 && config->fifo_settings_custom.ptx_fifo_lines==91);
    install_thread=std::this_thread::get_id(); ++install_calls;
    while (!allow_install) vTaskDelay(1);
    return install_error;
}
void usb_host_lib_handle_events(int,uint32_t *flags) {
    assert(core==1 && std::this_thread::get_id()==install_thread);
    ++events; *flags=host_quit ? USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS : 0; vTaskDelay(1);
}
void usb_host_device_free_all() { assert(core==1); ++frees; }
int usb_host_uninstall() {
    assert(core==1 && std::this_thread::get_id()==install_thread);
    assert(host_quit && !cdc_installed && !uac_installed && frees>0);
    int attempts=++uninstall_calls;
    return block_uninstall || attempts<=110 ? ESP_ERR_INVALID_STATE : ESP_OK;
}
int cdc_acm_host_install(const cdc_acm_host_driver_config_t*) {
    assert(core==0 && ready_taken && host_installed && host_start_result==ESP_OK); ++class_calls; return ESP_OK;
}
int uac_host_install(const uac_host_driver_config_t*) {
    assert(core==0 && ready_taken && host_installed && host_start_result==ESP_OK); ++class_calls;
    return uac_failure ? ESP_ERR_INVALID_STATE : ESP_OK;
}
int cdc_acm_host_uninstall() { assert(!capture_running && !cdc_running && !host_quit); return ESP_OK; }
int uac_host_uninstall() { assert(!cdc_installed && !host_quit); return ESP_OK; }
bool close_capture() { assert(!capture_running); return true; }
bool close_cdc() { assert(!cdc_running); return true; }
'''
cases = r'''
int main() {
    // Success, task creation failure, install failure, ready timeout, retained
    // owner after teardown timeout, and UAC failure after host/CDC startup.
    for (int scenario=0;scenario<6;++scenario) {
        install_calls=uninstall_calls=events=frees=0;
        tasks=class_calls=0; ready_taken=false;
        task_failure=scenario==1; install_error=scenario==2 ? ESP_ERR_INVALID_STATE : ESP_OK;
        ready_timeout=scenario==3; allow_install=!ready_timeout;
        join_timeout=scenario==4; block_uninstall=join_timeout; uac_failure=scenario==5;
        bool ok=prepare();
        assert(ok==(scenario==0 || scenario==4));
        if (scenario>=1 && scenario<=3) assert(class_calls==0);
        if (scenario==2) assert(!host_installed);
        if (scenario==4) {
            assert(!release());
            assert(host_running && host_installed && console_active && tasks==1);
            block_uninstall=false;
        }
        assert(release());
        assert(!task.joinable() && !host_running && !host_installed && !console_active);
        assert(!host_ready && !host_done && !remaining_semaphores);
        assert(tasks==(scenario==1 ? 0 : 1));
        assert(install_calls==tasks);
        if (scenario==1 || scenario==2) assert(uninstall_calls==0);
        else assert(uninstall_calls>=111);
    }
}
'''
with tempfile.TemporaryDirectory(prefix='t033-usb-owner-') as tmp:
    path=Path(tmp)/'owner.cpp'; binary=Path(tmp)/'owner'
    path.write_text(harness+owner+release+prepare+cases)
    subprocess.run([sys.argv[1] if len(sys.argv)>1 else 'c++','-std=c++17','-pthread',
                    '-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=20)
print('ADV CPU1 USB owner startup/failure/retained teardown: PASS')
