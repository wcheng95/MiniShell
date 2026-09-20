#include "adv_ft8_decode.h"

#include <stdalign.h>
#include <stdatomic.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define ADV_FT8_DECODE_STACK_BYTES 8192u
#define ADV_FT8_DECODE_PRIORITY (tskIDLE_PRIORITY + 1u)
#define ADV_FT8_DECODE_CORE 1

static const char *s_tag = "adv_ft8";

static alignas(portBYTE_ALIGNMENT)
    StackType_t s_decode_stack[ADV_FT8_DECODE_STACK_BYTES / sizeof(StackType_t)];
_Static_assert(sizeof(s_decode_stack) == ADV_FT8_DECODE_STACK_BYTES,
               "decode stack must remain 8192 bytes");
static StaticTask_t s_decode_tcb;
static StaticSemaphore_t s_decode_done_storage;
static SemaphoreHandle_t s_decode_done;
static TaskHandle_t s_decode_task;
static atomic_bool s_decode_stop;

static void decode_task(void *arg)
{
    AppController *app = (AppController *)arg;
    UBaseType_t lowest_free = ADV_FT8_DECODE_STACK_BYTES;

    ESP_LOGI(s_tag, "decode worker core=%d priority=%u stack=%u bytes",
             xPortGetCoreID(),
             (unsigned)uxTaskPriorityGet(NULL),
             (unsigned)ADV_FT8_DECODE_STACK_BYTES);

    while (!atomic_load_explicit(&s_decode_stop, memory_order_acquire)) {
        bool did_work = false;

        if (!app_controller_decode_worker_step(app, &did_work)) {
            ESP_LOGE(s_tag, "decode worker step failed; min-free=%u/%u bytes",
                     (unsigned)lowest_free,
                     (unsigned)ADV_FT8_DECODE_STACK_BYTES);
            break;
        }

        if (did_work) {
            UBaseType_t free_bytes = uxTaskGetStackHighWaterMark(NULL);
            if (free_bytes < lowest_free) {
                lowest_free = free_bytes;
                ESP_LOGI(s_tag, "decode stack minimum-free=%u/%u bytes",
                         (unsigned)lowest_free,
                         (unsigned)ADV_FT8_DECODE_STACK_BYTES);
            }
            taskYIELD();
        } else {
            vTaskDelay(1);
        }
    }

    ESP_LOGW(s_tag, "decode worker exiting; min-free=%u/%u bytes",
             (unsigned)lowest_free,
             (unsigned)ADV_FT8_DECODE_STACK_BYTES);
    xSemaphoreGive(s_decode_done);

    /* Static task storage is reclaimed by the owner after suspension. */
    for (;;)
        vTaskSuspend(NULL);
}

bool adv_ft8_decode_worker_start(AppController *app)
{
    if (app == NULL || s_decode_task != NULL)
        return false;
    if (!app_controller_enable_decode_worker(app, true))
        return false;

    s_decode_done = xSemaphoreCreateBinaryStatic(&s_decode_done_storage);
    if (s_decode_done == NULL) {
        (void)app_controller_enable_decode_worker(app, false);
        return false;
    }

    atomic_store_explicit(&s_decode_stop, false, memory_order_release);
    s_decode_task = xTaskCreateStaticPinnedToCore(
        decode_task,
        "ft8_decode",
        sizeof(s_decode_stack),
        app,
        ADV_FT8_DECODE_PRIORITY,
        s_decode_stack,
        &s_decode_tcb,
        ADV_FT8_DECODE_CORE);

    if (s_decode_task == NULL) {
        s_decode_done = NULL;
        (void)app_controller_enable_decode_worker(app, false);
        return false;
    }
    return true;
}

void adv_ft8_decode_worker_stop(AppController *app)
{
    if (s_decode_task == NULL)
        return;

    atomic_store_explicit(&s_decode_stop, true, memory_order_release);
    if (s_decode_done != NULL)
        (void)xSemaphoreTake(s_decode_done, pdMS_TO_TICKS(5000));

    while (eTaskGetState(s_decode_task) != eSuspended)
        vTaskDelay(1);

    vTaskDelete(s_decode_task);
    s_decode_task = NULL;
    s_decode_done = NULL;

    app_controller_decode_worker_abort(app);
    (void)app_controller_enable_decode_worker(app, false);
}
