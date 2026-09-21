#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include "minishell/api.h"
#include "tone_stream.h"
using namespace std::chrono_literals;
using TickType_t = uint32_t;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
constexpr int pdTRUE = 1, pdPASS = 1, ESP_CODEC_DEV_OK = 0;
#define pdMS_TO_TICKS(ms) (ms)
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(m) (m)->lock()
#define portEXIT_CRITICAL(m) (m)->unlock()
struct Sem { std::mutex mutex; std::condition_variable cv; unsigned count; };
using SemaphoreHandle_t = Sem *;
using TaskHandle_t = void *;
static std::thread worker;
static unsigned allocs, frees, semaphore_calls, fail_semaphore;
static bool fail_task, fail_hardware, fail_volume;
static std::atomic<bool> stall_write{false}, short_wait{false};
static std::atomic<unsigned> writes{0}, fail_at{0};
static std::atomic<bool> muted{true}, in_write{false};
static bool s_open, s_started;
static void *s_codec, *s_i2s_tx;
static bool fail_mute;
static int i2s_channel_disable(void *) { muted = true; return 0; }
static SemaphoreHandle_t new_sem(unsigned count)
{
    if (++semaphore_calls == fail_semaphore) return nullptr;
    ++allocs; return new Sem{{}, {}, count};
}
static SemaphoreHandle_t xSemaphoreCreateMutex() { return new_sem(1); }
static SemaphoreHandle_t xSemaphoreCreateBinary() { return new_sem(0); }
static void vSemaphoreDelete(Sem *sem) { ++frees; delete sem; }
static int xSemaphoreTake(Sem *sem, uint32_t ms)
{
    if (ms == 3000 && short_wait) ms = 30;
    std::unique_lock<std::mutex> lock(sem->mutex);
    auto ready = [&] { return sem->count != 0; };
    if (ms == UINT32_MAX) sem->cv.wait(lock, ready);
    else if (!sem->cv.wait_for(lock, std::chrono::milliseconds(ms), ready)) return 0;
    --sem->count; return 1;
}
static void xSemaphoreGive(Sem *sem)
{
    std::lock_guard<std::mutex> lock(sem->mutex); ++sem->count; sem->cv.notify_one();
}
static int xTaskCreate(void (*fn)(void *), const char *, uint32_t stack, void *, uint32_t priority, void **out)
{
    assert(stack == 6144 && priority == 5);
    if (worker.joinable()) worker.join();
    if (fail_task) return 0;
    *out = reinterpret_cast<void *>(1);
    worker = std::thread([fn] { fn(nullptr); }); return 1;
}
static void vTaskDelete(void *) { }
static bool prepare_i2s() { return !fail_hardware; }
static bool prepare_codec() { s_codec = reinterpret_cast<void *>(1); muted = true; writes = 0; return true; }
static void cleanup_hardware() { assert(!in_write); s_open = s_started = false; s_codec = nullptr; }
static int esp_codec_dev_set_in_gain(void *, float gain) { assert(gain == 30.0f); return 0; }
static int esp_codec_dev_set_out_vol(void *, int v) { assert(v >= 0 && v <= 99); return fail_volume ? -1 : 0; }
static int esp_codec_dev_set_out_mute(void *, bool mute) { if (!mute) assert(writes >= 8); if (mute && fail_mute) return -1; muted = mute; return 0; }
static int esp_codec_dev_read(void *, int16_t *, unsigned bytes) { assert(bytes == 960); return 0; }
static int esp_codec_dev_write(void *, void *data, unsigned bytes)
{
    assert(bytes == 480); in_write = true;
    unsigned number = ++writes;
    if (number <= 8) {
        assert(muted);
        const int16_t *pcm = static_cast<int16_t *>(data);
        for (unsigned i = 0; i < 240; ++i) assert(!pcm[i]);
    }
    while (stall_write) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(1ms);
    in_write = false;
    return fail_at && number >= fail_at ? -1 : 0;
}
#include "adv_tone_worker.inc"
static void joined() { if (worker.joinable()) worker.join(); assert(allocs == frees); }
int main()
{
    mini_audio_tone_config_t c = {sizeof(c), 700, 80}; mini_audio_tone_t h;
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
        assert(tone_open(&c, &h) == MINI_OK && h == 1 && s_open && !muted);
        unsigned before = writes;
        assert(tone_enqueue(h, 100) == MINI_OK);
        std::this_thread::sleep_for(30ms); /* foreground/UI does no audio servicing */
        assert(writes > before + 10);
        assert(tone_hold(h, 1) == MINI_OK);
        std::this_thread::sleep_for(2ms);
        assert(tone_stop(h) == MINI_OK);
        assert(tone_close(h) == MINI_OK && muted && !s_open && !s_tone_owned);
        joined();
    }
    for (unsigned slot = 1; slot <= 3; ++slot) {
        fail_semaphore = semaphore_calls + slot;
        assert(tone_open(&c, &h) == MINI_ERR_NO_MEMORY); joined();
    }
    fail_semaphore = 0;
    fail_task = true; assert(tone_open(&c, &h) == MINI_ERR_NO_MEMORY); joined(); fail_task = false;
    fail_hardware = true; assert(tone_open(&c, &h) == MINI_ERR_IO); fail_hardware = false;
    fail_at = 4; assert(tone_open(&c, &h) == MINI_ERR_IO && muted); joined();
    fail_at = 12; assert(tone_open(&c, &h) == MINI_OK);
    std::this_thread::sleep_for(20ms);
    uint32_t busy;
    assert(tone_busy(h, &busy) == MINI_ERR_IO && muted);
    assert(tone_close(h) == MINI_ERR_IO); joined();
    fail_at = 0; assert(tone_open(&c, &h) == MINI_OK);
    c.volume = 81; fail_volume = true; assert(tone_configure(h, &c) == MINI_ERR_IO);
    assert(tone_close(h) == MINI_ERR_IO); joined(); fail_volume = false;
    assert(tone_open(&c, &h) == MINI_OK); assert(tone_close(h) == MINI_OK); joined();
    assert(tone_open(&c, &h) == MINI_OK);
    stall_write = true;
    std::this_thread::sleep_for(3ms);
    short_wait = true;
    assert(tone_close(h) == MINI_ERR_TIMEOUT && s_tone_owned);
    short_wait = false; stall_write = false;
    std::this_thread::sleep_for(30ms);
    tone_reap(); joined(); assert(!s_open && !s_tone_owned && muted);
    assert(tone_open(&c, &h) == MINI_OK); assert(tone_close(h) == MINI_OK); joined();

    assert(tone_open(&c, &h) == MINI_OK); fail_mute = true;
    assert(tone_close(h) == MINI_ERR_IO && muted); joined(); fail_mute = false;

}
