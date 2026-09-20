#include <atomic>
#include <algorithm>
#include "adv_internal.h"
#include "adv_audio_uac_buffer.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"
#include "usb/uac_host.h"
#include "usb/cdc_acm_host.h"

extern "C" uint32_t uac_host_t017_skipped_isoc_total(void);

namespace {
constexpr minishell_backend_audio_t handle = (minishell_backend_audio_t)0x554143u;
constexpr uint16_t vid = 0x0483, pid = 0xA34C;
const char *tag = "adv_uac";
minishell_services_port_t base;
adv_uac_buffer_t *ring;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
DMA_ATTR uint8_t native_data[2304];
struct Connection { uint8_t address, interface; };
QueueHandle_t connections;
SemaphoreHandle_t capture_done, host_done, cdc_done, cdc_mutex;
alignas(portBYTE_ALIGNMENT) StackType_t capture_stack[4096 / sizeof(StackType_t)];
static_assert(sizeof(capture_stack) == 4096, "capture stack must remain 4096 bytes");
StaticTask_t capture_tcb;
TaskHandle_t capture_handle;
std::atomic<bool> reserved{false}, started{false}, connected{false};
std::atomic<bool> quit{false}, host_quit{false}, unplugged{false}, cdc_unplugged{false};
std::atomic<bool> host_installed{false};
constexpr minishell_backend_serial_t serial_handle = 0x434443u;
// Public owners are foreground-only; worker-visible RX state uses lock/atomics.
bool serial_reserved, session_ready, session_dirty;
uint32_t rx_generation;
bool uac_installed, cdc_installed, capture_running, host_running, cdc_running;
// Worker-owned until joined; retain failed closes for foreground cleanup retry.
uac_host_device_handle_t capture_device;
cdc_acm_dev_hdl_t cdc_device;
std::atomic<unsigned> read_errors{0}, transfer_errors{0};

// Audio alone allocates the ring. Serial-first startup needs no RX ring.
bool allocate_ring()
{
    constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI(tag, "ring allocation request bytes=%u heap-free=%u largest-block=%u",
             (unsigned)sizeof(*ring), (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps));
    auto *allocated = static_cast<adv_uac_buffer_t *>(heap_caps_malloc(sizeof(*ring), caps));
    if (allocated) memset(allocated, 0, sizeof(*allocated));
    portENTER_CRITICAL(&lock);
    ++rx_generation;
    ring = allocated;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(tag, "ring allocation %s bytes=%u heap-free=%u largest-block=%u",
             ring ? "success" : "failure", (unsigned)sizeof(*ring),
             (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps));
    return ring != nullptr;
}
void free_ring()
{
    portENTER_CRITICAL(&lock);
    auto *released = ring;
    ring = nullptr;
    ++rx_generation;
    portEXIT_CRITICAL(&lock);
    heap_caps_free(released);
}

bool close_capture()
{
    if (!capture_device) return true;
    if (uac_host_device_close(capture_device) != ESP_OK) {
        ESP_LOGE(tag, "UAC close incomplete; handle retained");
        return false;
    }
    capture_device = nullptr;
    return true;
}
bool close_cdc()
{
    if (!cdc_device) return true;
    if (cdc_acm_host_close(cdc_device) != ESP_OK) {
        ESP_LOGE(tag, "CDC close incomplete; handle retained");
        return false;
    }
    cdc_device = nullptr;
    return true;
}

void loss()
{
    portENTER_CRITICAL(&lock);
    if (ring) adv_uac_loss(ring);
    portEXIT_CRITICAL(&lock);
}
void device_event(uac_host_device_handle_t, uac_host_device_event_t event, void *)
{
    if (event == UAC_HOST_DRIVER_EVENT_DISCONNECTED) {
        connected = false;
        unplugged = true;
        loss();
    } else if (event == UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) {
        ++transfer_errors;
        loss();
    }
}
void driver_event(uint8_t address, uint8_t interface, uac_host_driver_event_t event, void *)
{
    if (event != UAC_HOST_DRIVER_EVENT_RX_CONNECTED) return;
    Connection item{address, interface};
    if (xQueueSend(connections, &item, 0) != pdTRUE) {
        loss();
        ESP_LOGE(tag, "enumeration queue full; reconnect device");
    }
}
void host_task(void *)
{
    while (!host_quit) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(20), &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
    // Class clients have detached. Drain asynchronous device/PHY cleanup.
    for (unsigned i = 0; i < 100; ++i) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(20), &flags);
        usb_host_device_free_all();
        if (usb_host_uninstall() == ESP_OK) {
            host_installed = false;
            break;
        }
    }
    if (host_installed) ESP_LOGE(tag, "USB host teardown incomplete; PHY retained");
    xSemaphoreGive(host_done);
    vTaskDelete(nullptr);
}
void cdc_event(const cdc_acm_host_dev_event_data_t *event, void *)
{
    if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) cdc_unplugged = true;
    if (event->type == CDC_ACM_HOST_ERROR) ESP_LOGW(tag, "CDC error %d", event->data.error);
}
void cdc_task(void *)
{
    cdc_acm_dev_hdl_t &device = cdc_device;
    while (!quit) {
        xSemaphoreTake(cdc_mutex, portMAX_DELAY);
        if (cdc_unplugged.exchange(false) && device) {
            if (close_cdc()) ESP_LOGI(tag, "CDC disconnected");
            else cdc_unplugged = true;
        }
        if (!device) {
            cdc_acm_host_device_config_t config = {};
            config.connection_timeout_ms = 100;
            config.out_buffer_size = 64;
            config.event_cb = cdc_event;
            // QMX interface 0 only; no line-coding, DTR, or CAT policy writes.
            if (cdc_acm_host_open(vid, pid, 0, &config, &device) == ESP_OK)
                ESP_LOGI(tag, "CDC ready 0483:a34c interface 0 (no CAT commands)");
        }
        xSemaphoreGive(cdc_mutex);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    xSemaphoreTake(cdc_mutex, portMAX_DELAY);
    close_cdc();
    xSemaphoreGive(cdc_mutex);
    xSemaphoreGive(cdc_done);
    vTaskDelete(nullptr);
}
void capture_task(void *)
{
    uac_host_device_handle_t &device = capture_device;
    bool streaming = false;
    uint32_t last_skipped = uac_host_t017_skipped_isoc_total();
    int64_t next_skip_report_us = esp_timer_get_time() + 15000000;
    while (!quit) {
        if (unplugged.exchange(false) && device) {
            if (!close_capture()) {
                unplugged = true;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            streaming = false;
        }
        Connection item;
        while (xQueueReceive(connections, &item, 0) == pdTRUE) {
            if (device) continue;
            uac_host_device_config_t config = {};
            config.addr = item.address;
            config.iface_num = item.interface;
            config.buffer_size = 9216;
            config.buffer_threshold = 2304;
            config.callback = device_event;
            if (uac_host_device_open(&config, &device) != ESP_OK) continue;
            uac_host_dev_info_t info = {};
            if (uac_host_get_device_info(device, &info) != ESP_OK || info.VID != vid || info.PID != pid) {
                if (!close_capture()) unplugged = true;
                continue;
            }
            ESP_LOGI(tag, "QMX 0483:a34c UAC RX interface %u opened", item.interface);
        }
        if (unplugged) continue;
        // A millisecond delay can truncate to zero and starve foreground startup.
        if (!device || (!streaming && !started)) { vTaskDelay(1); continue; }
        portENTER_CRITICAL(&lock);
        bool reset = ring && started && ring->reset_required;
        uint32_t epoch = ring ? ring->epoch : 0;
        uint32_t generation = rx_generation;
        portEXIT_CRITICAL(&lock);
        if (reset && streaming) {
            connected = false;
            if (uac_host_device_stop(device) != ESP_OK) {
                ++read_errors;
                loss();
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            streaming = false;
        }
        if (!streaming) {
            uac_host_stream_config_t format = {};
            format.channels = 2;
            format.bit_resolution = 24;
            format.sample_freq = 48000;
            if (uac_host_device_start(device, &format) != ESP_OK) {
                ++read_errors;
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            streaming = true;
            connected = true;
            portENTER_CRITICAL(&lock);
            if (ring && rx_generation == generation && ring->epoch == epoch) {
                ring->reset_required = false;
                ring->used = ring->phase = 0;
            }
            portEXIT_CRITICAL(&lock);
            ESP_LOGI(tag, "selected 48000/24/2 -> 12000/S16/2");
        }
        portENTER_CRITICAL(&lock);
        adv_uac_ticket_t ticket = ring && started ? adv_uac_begin(ring) : adv_uac_ticket_t{0, true};
        generation = rx_generation;
        portEXIT_CRITICAL(&lock);
        uint32_t bytes = 0;
        esp_err_t error = uac_host_device_read(device, native_data, sizeof(native_data),
                                               &bytes, pdMS_TO_TICKS(20));
        if (error == ESP_OK && bytes) {
            portENTER_CRITICAL(&lock);
            if (ring && started && rx_generation == generation)
                adv_uac_feed(ring, ticket, native_data, bytes);
            portEXIT_CRITICAL(&lock);
        } else if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
            ++read_errors;
            loss();
            vTaskDelay(1);
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_skip_report_us) {
            uint32_t total = uac_host_t017_skipped_isoc_total();
            uint32_t delta = total - last_skipped;
            if (delta != 0u) {
                ESP_LOGW(tag, "T017 RX pad summary: skipped=%u (+%u/15s)",
                         (unsigned)total, (unsigned)delta);
            }
            last_skipped = total;
            next_skip_report_us = now_us + 15000000;
        }
    }
    connected = false;
    if (device) {
        if (streaming) uac_host_device_stop(device);
        close_capture();
    }
    xSemaphoreGive(capture_done);
    // The owner deletes this task only once suspended on neither CPU. Self
    // deletion defers TCB cleanup to idle and could race static-buffer reuse.
    for (;;) vTaskSuspend(nullptr);
}

bool release()
{
    started = false;
    quit = true;
    if (capture_running) {
        if (xSemaphoreTake(capture_done, pdMS_TO_TICKS(5000)) != pdTRUE) return false;
        while (eTaskGetState(capture_handle) != eSuspended) vTaskDelay(1);
        vTaskDelete(capture_handle);
        capture_handle = nullptr;
        capture_running = false;
    }
    if (cdc_running) {
        if (xSemaphoreTake(cdc_done, pdMS_TO_TICKS(5000)) != pdTRUE) return false;
        cdc_running = false;
    }
    if (!close_capture() || !close_cdc()) return false;
    if (cdc_installed) {
        if (cdc_acm_host_uninstall() != ESP_OK) {
            ESP_LOGE(tag, "CDC uninstall incomplete; USB console remains suspended");
            return false;
        }
        cdc_installed = false;
    }
    if (uac_installed) {
        if (uac_host_uninstall() != ESP_OK) {
            ESP_LOGE(tag, "UAC uninstall incomplete; USB console remains suspended");
            return false;
        }
        uac_installed = false;
    }
    host_quit = true;
    if (host_installed && !host_running) {
        host_running = xTaskCreate(host_task, "uac_host_exit", 4096, nullptr, 5, nullptr) == pdPASS;
        if (!host_running) return false;
    }
    if (host_running) {
        if (xSemaphoreTake(host_done, pdMS_TO_TICKS(5000)) != pdTRUE) return false;
        host_running = false;
    }
    if (host_installed) return false;
    if (cdc_mutex) { vSemaphoreDelete(cdc_mutex); cdc_mutex = nullptr; }
    if (connections) { vQueueDelete(connections); connections = nullptr; }
    if (capture_done) { vSemaphoreDelete(capture_done); capture_done = nullptr; }
    if (cdc_done) { vSemaphoreDelete(cdc_done); cdc_done = nullptr; }
    if (host_done) { vSemaphoreDelete(host_done); host_done = nullptr; }
    // No restoration until both class clients and the host PHY are gone.
    return adv_console_end_usb_host(host_installed || uac_installed || cdc_installed) == 0;
}
bool prepare()
{
    quit = host_quit = unplugged = cdc_unplugged = false;
    connections = xQueueCreate(16, sizeof(Connection));
    capture_done = xSemaphoreCreateBinary();
    host_done = xSemaphoreCreateBinary();
    cdc_done = xSemaphoreCreateBinary();
    cdc_mutex = xSemaphoreCreateMutex();
    if (!connections || !capture_done || !host_done || !cdc_done || !cdc_mutex) return false;
    if (adv_console_begin_usb_host() != 0) return false;
    usb_host_config_t host = {};
    host.intr_flags = ESP_INTR_FLAG_LEVEL1;
    host.fifo_settings_custom.rx_fifo_lines = 91;
    host.fifo_settings_custom.nptx_fifo_lines = 18;
    host.fifo_settings_custom.ptx_fifo_lines = 91;
    adv_console_dump_interrupts();
    if (usb_host_install(&host) != ESP_OK) return false;
    host_installed = true;
    host_running = xTaskCreate(host_task, "uac_host", 4096, nullptr, 5, nullptr) == pdPASS;
    if (!host_running) return false;
    ESP_LOGI(tag, "USB Host installed FIFO 91/18/91; heap %u largest %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    cdc_acm_host_driver_config_t cdc = {};
    cdc.driver_task_stack_size = 3072;
    cdc.driver_task_priority = 4;
    cdc.xCoreID = 0;
    cdc_installed = cdc_acm_host_install(&cdc) == ESP_OK;
    if (cdc_installed) {
        cdc_running = xTaskCreate(cdc_task, "uac_cdc", 3072, nullptr, 3, nullptr) == pdPASS;
        if (!cdc_running && cdc_acm_host_uninstall() == ESP_OK) cdc_installed = false;
    }
    if (!cdc_installed) ESP_LOGW(tag, "CDC unavailable; UAC remains independent");
    uac_host_driver_config_t config = {};
    config.create_background_task = true;
    config.task_priority = 5;
    config.stack_size = 4096;
    config.core_id = 0;
    config.callback = driver_event;
    if (uac_host_install(&config) != ESP_OK) return false;
    uac_installed = true;
    ESP_LOGI(tag, "capture task create begin: static stack=%u heap=%u largest=%u",
             (unsigned)sizeof(capture_stack),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    capture_handle = xTaskCreateStatic(capture_task, "uac_capture", sizeof(capture_stack),
                                      nullptr, 4, capture_stack, &capture_tcb);
    capture_running = capture_handle != nullptr;
    ESP_LOGI(tag, "capture task create %s", capture_running ? "success" : "failure");
    return capture_running;
}
// Retain incomplete teardown for retry without retaining a consumed public handle.
bool release_unused()
{
    if (reserved || serial_reserved) return true;
    session_ready = false;
    if (!session_dirty) return true;
    if (!release()) return false;
    session_dirty = false;
    return true;
}
bool acquire_session()
{
    if (session_ready) return true;
    if (!release_unused()) return false;
    session_dirty = true;
    session_ready = prepare();
    if (!session_ready) release_unused();
    return session_ready;
}
mini_result_t serial_open(void *, const char *endpoint, minishell_backend_serial_t *out)
{
    if (!out) return MINI_ERR_INVALID;
    *out = MINISHELL_BACKEND_SERIAL_INVALID;
    if (!endpoint || strcmp(endpoint, "serial:qmx") != 0) return MINI_ERR_INVALID;
    if (serial_reserved) return MINI_ERR_TOO_MANY_OPEN;
    if (!acquire_session()) return MINI_ERR_IO;
    // Enumeration is asynchronous. Match first-attach/reconnect without an
    // unbounded foreground open when the radio is absent.
    int64_t deadline = esp_timer_get_time() + 3000000;
    while (cdc_running && esp_timer_get_time() < deadline) {
        if (xSemaphoreTake(cdc_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            bool ready = cdc_device && !cdc_unplugged;
            xSemaphoreGive(cdc_mutex);
            if (ready) {
                serial_reserved = true;
                *out = serial_handle;
                return MINI_OK;
            }
        }
        vTaskDelay(1);
    }
    return release_unused() ? MINI_ERR_NOT_READY : MINI_ERR_IO;
}
mini_result_t serial_write(void *, minishell_backend_serial_t serial, const void *data,
                           uint32_t size, uint32_t *out, uint32_t timeout)
{
    if (!out) return MINI_ERR_INVALID;
    *out = 0;
    if (serial != serial_handle || !serial_reserved) return MINI_ERR_BAD_HANDLE;
    if (!size) return MINI_OK;
    if (!data) return MINI_ERR_INVALID;
    int64_t begin = esp_timer_get_time();
    // The pinned driver converts milliseconds with 32-bit multiplication.
    // Its longest safe wait is a transport watchdog even for WAIT_FOREVER;
    // never retry an ambiguous transfer (bytes may already have reached USB).
    constexpr uint32_t max_wait = UINT32_MAX / configTICK_RATE_HZ;
    uint32_t budget = std::min(timeout, max_wait);
    TickType_t ticks = timeout == MINI_WAIT_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(budget);
    if (xSemaphoreTake(cdc_mutex, ticks) != pdTRUE) return MINI_ERR_TIMEOUT;
    uint32_t elapsed = (uint32_t)((esp_timer_get_time() - begin) / 1000);
    uint32_t remaining = timeout == MINI_WAIT_FOREVER ? max_wait :
                         elapsed < budget ? budget - elapsed : 0;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (cdc_device && !cdc_unplugged)
        result = cdc_acm_host_data_tx_blocking(cdc_device, (const uint8_t *)data, size, remaining);
    xSemaphoreGive(cdc_mutex);
    if (result == ESP_OK) { *out = size; return MINI_OK; }
    if (result == ESP_ERR_TIMEOUT && timeout != MINI_WAIT_FOREVER) return MINI_ERR_TIMEOUT;
    return MINI_ERR_IO;
}
mini_result_t serial_close(void *, minishell_backend_serial_t serial)
{
    if (serial != serial_handle || !serial_reserved) return MINI_ERR_BAD_HANDLE;
    serial_reserved = false;
    return release_unused() ? MINI_OK : MINI_ERR_IO;
}
mini_result_t rx_open(void *ctx, const char *endpoint, uint32_t rate, uint32_t format,
                      uint32_t channels, minishell_backend_audio_t *out)
{
    if (!endpoint || strcmp(endpoint, "uac:qmx") != 0)
        return base.audio_rx_open ? base.audio_rx_open(ctx, endpoint, rate, format, channels, out) : MINI_ERR_NOT_FOUND;
    if (!out) return MINI_ERR_INVALID;
    *out = MINISHELL_BACKEND_AUDIO_INVALID;
    if (rate != 12000 || format != MINI_AUDIO_SAMPLE_S16 || channels != 2) return MINI_ERR_UNSUPPORTED;
    if (reserved) return MINI_ERR_TOO_MANY_OPEN;
    if (!allocate_ring()) return MINI_ERR_NO_MEMORY;
    read_errors = transfer_errors = 0;
    if (!acquire_session()) {
        free_ring();
        return MINI_ERR_IO;
    }
    loss();
    reserved = true;
    *out = handle;
    return MINI_OK;
}
mini_result_t rx_start(void *ctx, minishell_backend_audio_t audio)
{
    if (audio != handle) return base.audio_rx_start ? base.audio_rx_start(ctx, audio) : MINI_ERR_BAD_HANDLE;
    if (!reserved) return MINI_ERR_BAD_HANDLE;
    if (started) return MINI_OK;
    portENTER_CRITICAL(&lock);
    // Invalidate an in-flight read from the paused interval before delivery.
    ++rx_generation;
    started = true;
    portEXIT_CRITICAL(&lock);
    return MINI_OK;
}
mini_result_t rx_read(void *ctx, minishell_backend_audio_t audio, void *frames,
                      uint32_t capacity, uint32_t *out, uint32_t timeout)
{
    if (audio != handle) return base.audio_rx_read ? base.audio_rx_read(ctx, audio, frames, capacity, out, timeout) : MINI_ERR_BAD_HANDLE;
    if (!out) return MINI_ERR_INVALID;
    *out = 0;
    if (!reserved) return MINI_ERR_BAD_HANDLE;
    if (!started) return MINI_ERR_NOT_READY;
    if (!capacity) return MINI_OK;
    if (!frames) return MINI_ERR_INVALID;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout * 1000;
    for (;;) {
        uint32_t high_water = 0, overflows = 0, losses = 0;
        portENTER_CRITICAL(&lock);
        bool discontinuity = adv_uac_ack(ring);
        if (discontinuity) {
            high_water = ring->high_water;
            overflows = ring->overflows;
            losses = ring->losses;
        }
        uint32_t count = discontinuity ? 0 : adv_uac_take(
            ring, (int16_t *)frames, std::min<uint32_t>(capacity, 256u));
        portEXIT_CRITICAL(&lock);
        if (discontinuity) {
            ESP_LOGW(tag,
                     "RX discontinuity high-water=%lu/%lu overflow=%lu losses=%lu "
                     "read-errors=%u transfer-errors=%u",
                     (unsigned long)high_water,
                     (unsigned long)ADV_UAC_RING_FRAMES,
                     (unsigned long)overflows,
                     (unsigned long)losses,
                     read_errors.load(),
                     transfer_errors.load());
            return MINI_ERR_DISCONTINUITY;
        }
        if (count) { *out = count; return MINI_OK; }
        if (!connected) return MINI_ERR_NOT_READY;
        if (timeout == MINI_WAIT_NONE || (timeout != MINI_WAIT_FOREVER && esp_timer_get_time() >= deadline)) return MINI_ERR_TIMEOUT;
        vTaskDelay(1);
    }
}
mini_result_t rx_stop(void *ctx, minishell_backend_audio_t audio)
{
    if (audio != handle) return base.audio_rx_stop ? base.audio_rx_stop(ctx, audio) : MINI_ERR_BAD_HANDLE;
    if (!reserved) return MINI_ERR_BAD_HANDLE;
    portENTER_CRITICAL(&lock);
    bool was_started = started.exchange(false);
    if (was_started) adv_uac_loss(ring);
    uint32_t high_water = ring->high_water, overflows = ring->overflows, losses = ring->losses;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(tag, "high-water=%lu/%lu overflow=%lu discontinuity=%lu read-errors=%u transfer-errors=%u",
             (unsigned long)high_water, (unsigned long)ADV_UAC_RING_FRAMES, (unsigned long)overflows,
             (unsigned long)losses, read_errors.load(), transfer_errors.load());
    return MINI_OK;
}
mini_result_t rx_close(void *ctx, minishell_backend_audio_t audio)
{
    if (audio != handle) return base.audio_rx_close ? base.audio_rx_close(ctx, audio) : MINI_ERR_BAD_HANDLE;
    mini_result_t result = rx_stop(ctx, audio);
    if (result != MINI_OK) return result;
    free_ring();
    reserved = false;
    return release_unused() ? MINI_OK : MINI_ERR_IO;
}
} // namespace
extern "C" mini_result_t adv_qmx_prepare_serial(void)
{
    if (!acquire_session() || !cdc_running) return MINI_ERR_IO;
    if (xSemaphoreTake(cdc_mutex, 0) != pdTRUE) return MINI_ERR_NOT_READY;
    bool ready = cdc_device && !cdc_unplugged;
    xSemaphoreGive(cdc_mutex);
    return ready ? MINI_OK : MINI_ERR_NOT_READY;
}
extern "C" mini_result_t adv_qmx_release_unused(void)
{
    return release_unused() ? MINI_OK : MINI_ERR_IO;
}
extern "C" void adv_audio_uac_configure(minishell_services_port_t *port)
{
    base = *port;
    port->serial_capabilities = MINI_SERIAL_CAP_WRITE;
    port->serial_open = serial_open;
    port->serial_write = serial_write;
    port->serial_close = serial_close;
    port->audio_capabilities |= MINI_AUDIO_CAP_RX;
    port->audio_rx_open = rx_open;
    port->audio_rx_start = rx_start;
    port->audio_rx_read = rx_read;
    port->audio_rx_stop = rx_stop;
    port->audio_rx_close = rx_close;
}
