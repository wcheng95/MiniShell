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
SemaphoreHandle_t capture_done, host_done, cdc_done;
std::atomic<bool> reserved{false}, started{false}, connected{false};
std::atomic<bool> quit{false}, host_quit{false}, unplugged{false}, cdc_unplugged{false};
std::atomic<bool> host_installed{false};
bool uac_installed, cdc_installed, capture_running, host_running, cdc_running;
// Worker-owned until joined; retain failed closes for foreground cleanup retry.
uac_host_device_handle_t capture_device;
cdc_acm_dev_hdl_t cdc_device;
std::atomic<unsigned> read_errors{0}, transfer_errors{0};

// Allocation precedes all console/USB work. Keep the block across stop/start
// and failed cleanup; only a completed close/failed-open unwind can free it.
bool allocate_ring()
{
    constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI(tag, "ring allocation request bytes=%u heap-free=%u largest-block=%u",
             (unsigned)sizeof(*ring), (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps));
    ring = static_cast<adv_uac_buffer_t *>(heap_caps_malloc(sizeof(*ring), caps));
    if (ring) memset(ring, 0, sizeof(*ring));
    ESP_LOGI(tag, "ring allocation %s bytes=%u heap-free=%u largest-block=%u",
             ring ? "success" : "failure", (unsigned)sizeof(*ring),
             (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps));
    return ring != nullptr;
}
void free_ring()
{
    heap_caps_free(ring);
    ring = nullptr;
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
    adv_uac_loss(ring);
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
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    close_cdc();
    xSemaphoreGive(cdc_done);
    vTaskDelete(nullptr);
}
void capture_task(void *)
{
    uac_host_device_handle_t &device = capture_device;
    bool streaming = false;
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
        if (!device || !started) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
        portENTER_CRITICAL(&lock);
        bool reset = ring->reset_required;
        uint32_t epoch = ring->epoch;
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
            if (ring->epoch == epoch) {
                ring->reset_required = false;
                ring->used = ring->phase = 0;
            }
            portEXIT_CRITICAL(&lock);
            ESP_LOGI(tag, "selected 48000/24/2 -> 12000/S16/2");
        }
        portENTER_CRITICAL(&lock);
        adv_uac_ticket_t ticket = adv_uac_begin(ring);
        portEXIT_CRITICAL(&lock);
        uint32_t bytes = 0;
        esp_err_t error = uac_host_device_read(device, native_data, sizeof(native_data),
                                               &bytes, pdMS_TO_TICKS(20));
        if (error == ESP_OK && bytes) {
            portENTER_CRITICAL(&lock);
            adv_uac_feed(ring, ticket, native_data, bytes);
            portEXIT_CRITICAL(&lock);
        } else if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
            ++read_errors;
            loss();
            vTaskDelay(1);
        }
    }
    connected = false;
    if (device) {
        if (streaming) uac_host_device_stop(device);
        close_capture();
    }
    xSemaphoreGive(capture_done);
    vTaskDelete(nullptr);
}

bool release()
{
    started = false;
    quit = true;
    if (capture_running) { xSemaphoreTake(capture_done, portMAX_DELAY); capture_running = false; }
    if (cdc_running) { xSemaphoreTake(cdc_done, portMAX_DELAY); cdc_running = false; }
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
    if (host_running) { xSemaphoreTake(host_done, portMAX_DELAY); host_running = false; }
    if (host_installed) return false;
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
    if (!connections || !capture_done || !host_done || !cdc_done) return false;
    if (adv_console_begin_usb_host() != 0) return false;
    usb_host_config_t host = {};
    host.intr_flags = ESP_INTR_FLAG_LEVEL1;
    host.fifo_settings_custom.rx_fifo_lines = 91;
    host.fifo_settings_custom.nptx_fifo_lines = 18;
    host.fifo_settings_custom.ptx_fifo_lines = 91;
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
    capture_running = xTaskCreate(capture_task, "uac_capture", 4096, nullptr, 4, nullptr) == pdPASS;
    return capture_running;
}
mini_result_t rx_open(void *ctx, const char *endpoint, uint32_t rate, uint32_t format,
                      uint32_t channels, minishell_backend_audio_t *out)
{
    if (!endpoint || strcmp(endpoint, "uac:qmx") != 0)
        return base.audio_rx_open ? base.audio_rx_open(ctx, endpoint, rate, format, channels, out) : MINI_ERR_NOT_FOUND;
    if (!out) return MINI_ERR_INVALID;
    *out = MINISHELL_BACKEND_AUDIO_INVALID;
    if (rate != 12000 || format != MINI_AUDIO_SAMPLE_S16 || channels != 2) return MINI_ERR_UNSUPPORTED;
    if (reserved.exchange(true)) {
        // A previous close may have retained infrastructure after a teardown
        // error. Retry cleanup, but never steal an active stream.
        if (started) return MINI_ERR_TOO_MANY_OPEN;
        if (!release()) return MINI_ERR_IO;
        free_ring();
    }
    if (!allocate_ring()) {
        reserved = false;
        return MINI_ERR_NO_MEMORY;
    }
    read_errors = transfer_errors = 0;
    if (!prepare()) {
        if (release()) { free_ring(); reserved = false; }
        return MINI_ERR_IO;
    }
    *out = handle;
    return MINI_OK;
}
mini_result_t rx_start(void *ctx, minishell_backend_audio_t audio)
{
    if (audio != handle) return base.audio_rx_start ? base.audio_rx_start(ctx, audio) : MINI_ERR_BAD_HANDLE;
    if (!reserved) return MINI_ERR_BAD_HANDLE;
    if (started) return MINI_OK;
    if (!capture_running) {
        if (!release() || !prepare()) { release(); return MINI_ERR_IO; }
    }
    started = true;
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
        portENTER_CRITICAL(&lock);
        bool discontinuity = adv_uac_ack(ring);
        uint32_t count = discontinuity ? 0 : adv_uac_take(ring, (int16_t *)frames, std::min<uint32_t>(capacity, 256u));
        portEXIT_CRITICAL(&lock);
        if (discontinuity) return MINI_ERR_DISCONTINUITY;
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
    bool was_started = started;
    bool ok = release();
    portENTER_CRITICAL(&lock);
    if (was_started) adv_uac_loss(ring);
    uint32_t high_water = ring->high_water, overflows = ring->overflows, losses = ring->losses;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(tag, "high-water=%lu/16384 overflow=%lu discontinuity=%lu read-errors=%u transfer-errors=%u",
             (unsigned long)high_water, (unsigned long)overflows,
             (unsigned long)losses, read_errors.load(), transfer_errors.load());
    return ok ? MINI_OK : MINI_ERR_IO;
}
mini_result_t rx_close(void *ctx, minishell_backend_audio_t audio)
{
    if (audio != handle) return base.audio_rx_close ? base.audio_rx_close(ctx, audio) : MINI_ERR_BAD_HANDLE;
    mini_result_t result = rx_stop(ctx, audio);
    if (result == MINI_OK) { free_ring(); reserved = false; }
    return result;
}
} // namespace
extern "C" void adv_audio_uac_configure(minishell_services_port_t *port)
{
    base = *port;
    port->audio_capabilities |= MINI_AUDIO_CAP_RX;
    port->audio_rx_open = rx_open;
    port->audio_rx_start = rx_start;
    port->audio_rx_read = rx_read;
    port->audio_rx_stop = rx_stop;
    port->audio_rx_close = rx_close;
}
