#!/usr/bin/env python3
"""Exercise production UAC open/start/stop/close with heap and ownership faults.

Compile the actual provider lifecycle functions, replacing only ESP heap/logging
and USB prepare/release operations. No ESP-IDF installation or USB device needed.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
provider = (ROOT / "platform/adv/adv_audio_uac.cpp").read_text()


def function(signature):
    start = provider.index(signature)
    end = provider.index("\n}\n", start) + 3
    return provider[start:end]


assert "adv_uac_buffer_t *ring;" in provider
assert "adv_uac_buffer_t ring;" not in provider
assert provider.count("heap_caps_malloc(") == 1
assert provider.count("heap_caps_free(") == 1
prepare = function("bool prepare()")
release = function("bool release()")
assert "allocate_ring" not in prepare and "free_ring" not in release
assert prepare.index("adv_console_begin_usb_host()") < prepare.index("xTaskCreatePinnedToCore(host_task")

HARNESS = r'''
#include <atomic>
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "minishell_services.h"
#include "adv_audio_uac_buffer.h"

constexpr minishell_backend_audio_t handle = 0x554143u;
const char *tag = "adv_uac";
minishell_services_port_t base = {};
adv_uac_buffer_t *ring;
std::atomic<bool> reserved{false}, started{false};
bool connected = true;
static int64_t esp_timer_get_time() { return 0; }
static void vTaskDelay(int) {}
std::atomic<unsigned> read_errors{0}, transfer_errors{0};
bool capture_running, serial_reserved, session_ready, session_dirty;
uint32_t rx_generation;
#define portENTER_CRITICAL(unused) ((void)0)
#define portEXIT_CRITICAL(unused) ((void)0)
constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2;
static bool fail_alloc, prepare_ok = true, cleanup_ok = true, usb_owned, expect_zero;
static unsigned allocations, frees, prepares, releases, heap_queries, wav_opens;
static void *live_allocation;
static std::vector<std::string> logs;
static void log_message(const char *, const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    logs.emplace_back(line);
}
#define ESP_LOGI(...) log_message(__VA_ARGS__)
#define ESP_LOGW(...) log_message(__VA_ARGS__)
static size_t heap_caps_get_free_size(uint32_t caps)
{
    assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ++heap_queries;
    return live_allocation ? 65536 : 131072;
}
static size_t heap_caps_get_largest_free_block(uint32_t caps)
{ return heap_caps_get_free_size(caps); }
static void *heap_caps_malloc(size_t bytes, uint32_t caps)
{
    assert(!live_allocation);
    assert(bytes == sizeof(adv_uac_buffer_t));
    assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ++allocations;
    if (fail_alloc) return nullptr;
    live_allocation = malloc(bytes);
    assert(live_allocation);
    memset(live_allocation, 0xa5, bytes);
    return live_allocation;
}
static void heap_caps_free(void *pointer)
{
    if (!pointer) return;
    assert(pointer == live_allocation);
    free(pointer);
    live_allocation = nullptr;
    ++frees;
}
static bool prepare()
{
    assert(!usb_owned);
    assert(!ring || ring == live_allocation);
    if (expect_zero && ring) {
        auto bytes = reinterpret_cast<const unsigned char *>(ring);
        for (size_t i = 0; i < sizeof(*ring); ++i) assert(bytes[i] == 0);
        expect_zero = false;
    }
    ++prepares;
    usb_owned = true;
    capture_running = prepare_ok;
    return prepare_ok;
}
static bool release()
{
    ++releases;
    started = false;
    capture_running = false;
    if (cleanup_ok) usb_owned = false;
    return cleanup_ok;
}
'''

CASES = r'''
static mini_result_t wav_open(void *, const char *, uint32_t, uint32_t, uint32_t,
                              minishell_backend_audio_t *out)
{ ++wav_opens; *out = 1; return MINI_OK; }
static mini_result_t wav_close(void *, minishell_backend_audio_t) { return MINI_OK; }
static mini_result_t open_uac(minishell_backend_audio_t *out)
{
    expect_zero = true;
    return rx_open(nullptr, "uac:qmx", 12000, MINI_AUDIO_SAMPLE_S16, 2, out);
}
int main()
{
    static_assert(ADV_UAC_RING_FRAMES == 2048u, "retain approved ring capacity");
    assert(!ring && !reserved && allocations == 0);
    base.audio_rx_open = wav_open;
    base.audio_rx_close = wav_close;
    minishell_backend_audio_t audio = 0;
    const char *endpoints[] = {nullptr, "/flash/kfs.wav", "another:endpoint"};
    for (const char *endpoint : endpoints) {
        assert(rx_open(nullptr, endpoint, 12000, MINI_AUDIO_SAMPLE_S16, 2, &audio) == MINI_OK);
        assert(rx_close(nullptr, audio) == MINI_OK);
    }
    assert(wav_opens == 3 && allocations == 0 && !usb_owned && prepares == 0);
    assert(rx_open(nullptr, "uac:qmx", 48000, MINI_AUDIO_SAMPLE_S16, 2, &audio) == MINI_ERR_UNSUPPORTED);
    assert(!ring && allocations == 0);

    fail_alloc = true;
    assert(open_uac(&audio) == MINI_ERR_NO_MEMORY);
    assert(audio == MINISHELL_BACKEND_AUDIO_INVALID && !reserved && !ring);
    assert(prepares == 0 && releases == 0 && !usb_owned && frees == 0);
    assert(heap_queries == 4 && logs.size() == 2);
    assert(logs[0].find("request bytes=" + std::to_string(sizeof(*ring))) != std::string::npos);
    assert(logs[1].find("failure") != std::string::npos);
    assert(logs[0].find("heap-free=") != std::string::npos);
    assert(logs[1].find("largest-block=") != std::string::npos);
    fail_alloc = false;

    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        assert(open_uac(&audio) == MINI_OK);
        assert(logs.back().find("success") != std::string::npos);
        auto saved = ring;
        unsigned before = allocations;
        assert(rx_start(nullptr, audio) == MINI_OK);
        // Establish delivery, then race an in-flight packet with pause/resume.
        uint32_t count = 99;
        int16_t frames[2];
        assert(rx_read(nullptr, audio, frames, 1, &count, MINI_WAIT_NONE) == MINI_ERR_DISCONTINUITY);
        ring->reset_required = false; // Worker acknowledged/reset native queue.
        auto ticket = adv_uac_begin(ring);
        const uint8_t native[] = {0, 1, 0, 0, 2, 0};
        assert(adv_uac_feed(ring, ticket, native, sizeof(native)));
        assert(ring->head - ring->tail == 1);
        ring->high_water = 42;
        assert(rx_stop(nullptr, audio) == MINI_OK);
        assert(logs.back().find("high-water=42/" + std::to_string(ADV_UAC_RING_FRAMES) + " ") != std::string::npos);
        assert(ring == saved && usb_owned && reserved && ring->pending);
        assert(rx_read(nullptr, audio, frames, 1, &count, MINI_WAIT_NONE) == MINI_ERR_NOT_READY);
        assert(count == 0 && ring->head == ring->tail);
        assert(rx_start(nullptr, audio) == MINI_OK);
        assert(ring == saved && ring->high_water == 42 && ring->pending);
        assert(allocations == before);
        assert(rx_read(nullptr, audio, frames, 1, &count, MINI_WAIT_NONE) == MINI_ERR_DISCONTINUITY);
        ring->reset_required = false;
        assert(adv_uac_feed(ring, ticket, native, sizeof(native))); // Old epoch discarded.
        assert(ring->head == ring->tail);
        assert(adv_uac_feed(ring, adv_uac_begin(ring), native, sizeof(native)));
        assert(rx_read(nullptr, audio, frames, 1, &count, MINI_WAIT_NONE) == MINI_OK);
        assert(count == 1 && frames[0] == 1 && frames[1] == 2);
        assert(rx_close(nullptr, audio) == MINI_OK);
        assert(!ring && !live_allocation && !reserved && !usb_owned);
        assert(rx_close(nullptr, audio) == MINI_ERR_BAD_HANDLE);
    }

    // Serial-first uses no RX allocation; Audio joins the same installation.
    unsigned before_prepare = prepares;
    expect_zero = false;
    assert(acquire_session());
    serial_reserved = true;
    assert(!ring && usb_owned && prepares == before_prepare + 1);
    fail_alloc = true;
    assert(open_uac(&audio) == MINI_ERR_NO_MEMORY);
    assert(!ring && serial_reserved && usb_owned);
    fail_alloc = false;
    assert(open_uac(&audio) == MINI_OK);
    assert(prepares == before_prepare + 1);
    minishell_backend_audio_t duplicate;
    assert(open_uac(&duplicate) == MINI_ERR_TOO_MANY_OPEN);
    assert(rx_start(nullptr, audio) == MINI_OK);
    unsigned before_release = releases;
    assert(rx_stop(nullptr, audio) == MINI_OK);
    assert(serial_reserved && usb_owned && releases == before_release);
    assert(rx_start(nullptr, audio) == MINI_OK);
    serial_reserved = false;
    assert(release_unused() && usb_owned && releases == before_release);
    assert(rx_close(nullptr, audio) == MINI_OK);
    assert(!usb_owned && releases == before_release + 1);

    // Audio closes first; retain the session but release its ring safely.
    assert(open_uac(&audio) == MINI_OK);
    serial_reserved = true;
    assert(rx_start(nullptr, audio) == MINI_OK);
    assert(rx_close(nullptr, audio) == MINI_OK);
    assert(!ring && usb_owned && !reserved);
    assert(open_uac(&audio) == MINI_OK);
    assert(rx_close(nullptr, audio) == MINI_OK);
    serial_reserved = false;
    assert(release_unused());
    before_release = releases;
    assert(release_unused() && releases == before_release);

    prepare_ok = false;
    assert(open_uac(&audio) == MINI_ERR_IO);
    assert(!ring && !reserved && !usb_owned && !live_allocation);
    cleanup_ok = false;
    assert(open_uac(&audio) == MINI_ERR_IO);
    assert(!ring && !reserved && usb_owned && session_dirty);
    before_prepare = prepares;
    assert(open_uac(&audio) == MINI_ERR_IO);
    assert(!ring && prepares == before_prepare);
    cleanup_ok = true;
    prepare_ok = true;
    assert(open_uac(&audio) == MINI_OK);
    assert(rx_start(nullptr, audio) == MINI_OK);
    cleanup_ok = false;
    assert(rx_close(nullptr, audio) == MINI_ERR_IO);
    assert(!ring && !reserved && usb_owned && session_dirty);
    assert(rx_close(nullptr, audio) == MINI_ERR_BAD_HANDLE);
    cleanup_ok = true;
    assert(release_unused());
    assert(!ring && !reserved && !usb_owned && !live_allocation);
    assert(allocations == frees + 2); // The two injected allocation failures.
    puts("UAC lazy allocation/lifecycle: PASS");
}
'''

functions = "\n".join(function(signature) for signature in (
    "bool allocate_ring()", "void free_ring()", "void loss()",
    "bool release_unused()", "bool acquire_session()", "mini_result_t rx_open(",
    "mini_result_t rx_start(", "mini_result_t rx_read(", "mini_result_t rx_stop(", "mini_result_t rx_close("))
with tempfile.TemporaryDirectory(prefix="t017-ring-allocation-") as directory:
    source = Path(directory) / "lifecycle.cpp"
    binary = Path(directory) / "lifecycle"
    source.write_text(HARNESS + functions + CASES)
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else "c++", "-std=c++17",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                    "-I", str(ROOT / "include"),
                    "-I", str(ROOT / "core/minishell_services"),
                    "-I", str(ROOT / "platform/adv"), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
