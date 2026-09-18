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
assert prepare.index("adv_console_begin_usb_host()") < prepare.index("usb_host_install(")

HARNESS = r'''
#include <atomic>
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
std::atomic<unsigned> read_errors{0}, transfer_errors{0};
bool capture_running;
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
    assert(!usb_owned && !live_allocation);
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
    assert(!usb_owned);
    assert(pointer && pointer == live_allocation);
    free(pointer);
    live_allocation = nullptr;
    ++frees;
}
static bool prepare()
{
    assert(ring && ring == live_allocation && !usb_owned);
    if (expect_zero) {
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
        ring->high_water = 42;
        assert(rx_stop(nullptr, audio) == MINI_OK);
        assert(logs.back().find("high-water=42/" + std::to_string(ADV_UAC_RING_FRAMES) + " ") != std::string::npos);
        assert(ring == saved && !usb_owned && reserved && ring->pending);
        assert(rx_start(nullptr, audio) == MINI_OK);
        assert(ring == saved && ring->high_water == 42 && ring->pending);
        assert(allocations == before);
        assert(rx_close(nullptr, audio) == MINI_OK);
        assert(!ring && !live_allocation && !reserved && !usb_owned);
        assert(rx_close(nullptr, audio) == MINI_ERR_BAD_HANDLE);
    }

    prepare_ok = false;
    assert(open_uac(&audio) == MINI_ERR_IO);
    assert(!ring && !reserved && !usb_owned && !live_allocation);
    cleanup_ok = false;
    assert(open_uac(&audio) == MINI_ERR_IO);
    auto retained = ring;
    assert(retained && reserved && usb_owned);
    unsigned before_alloc = allocations, before_free = frees, before_prepare = prepares;
    assert(open_uac(&audio) == MINI_ERR_IO);
    assert(ring == retained && allocations == before_alloc && frees == before_free);
    assert(prepares == before_prepare);
    cleanup_ok = true;
    prepare_ok = true;
    assert(open_uac(&audio) == MINI_OK);
    assert(frees == before_free + 1 && allocations == before_alloc + 1);
    assert(rx_start(nullptr, audio) == MINI_OK);
    retained = ring;
    cleanup_ok = false;
    assert(rx_close(nullptr, audio) == MINI_ERR_IO);
    assert(ring == retained && reserved && usb_owned);
    assert(frees == before_free + 1);
    cleanup_ok = true;
    assert(rx_close(nullptr, audio) == MINI_OK);
    assert(!ring && !reserved && !usb_owned && !live_allocation);
    assert(allocations == frees + 1); // Only the injected allocation failure.
    puts("UAC lazy allocation/lifecycle: PASS");
}
'''

functions = "\n".join(function(signature) for signature in (
    "bool allocate_ring()", "void free_ring()", "mini_result_t rx_open(",
    "mini_result_t rx_start(", "mini_result_t rx_stop(", "mini_result_t rx_close("))
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
