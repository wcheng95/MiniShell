#include <string.h>

#include "adv_internal.h"
#include "adv_rtc.h"
#include "minishell_services.h"
#include "platform_backend.h"

static minishell_services_port_t s_services_port;
static bool s_display_ready;
static bool s_keyboard_ready;
static bool s_filesystem_ready;

static void system_write(void *ctx, const char *text)
{
    (void)ctx;
    /* System.write is a diagnostic sink, not user-facing application output. */
    adv_console_debug_write(text);
}

static void console_write(void *ctx, const char *text)
{
    (void)ctx;
    /* Console.write joins the resident shell's line-oriented output stream.
     * On ADV that means Cardputer display plus the USB mirror. */
    minishell_platform_console_write(text);
}

static void configure_services_port(void)
{
    memset(&s_services_port, 0, sizeof(s_services_port));

    s_services_port.system_write = system_write;
    s_services_port.console_write = console_write;

    s_services_port.memory_alloc = adv_memory_alloc;
    s_services_port.memory_realloc = adv_memory_realloc;
    s_services_port.memory_free = adv_memory_free;
    s_services_port.memory_get_info = adv_memory_get_info;

    s_services_port.monotonic_us = adv_monotonic_us;
    s_services_port.sleep_ms = adv_sleep_ms;
    s_services_port.time_location_capabilities = 0u;

    if (s_filesystem_ready) adv_filesystem_configure(&s_services_port);
    adv_time_location_configure(&s_services_port);

    if (s_display_ready) {
        s_services_port.display_capabilities = MINI_DISPLAY_CAP_TEXT;
        s_services_port.display_text_get_info = adv_display_text_get_info;
        s_services_port.display_text_clear = adv_display_text_clear;
        s_services_port.display_text_clear_at = adv_display_text_clear_at;
        s_services_port.display_text_write_at = adv_display_text_write_at;
        s_services_port.display_text_write_at_attr = adv_display_text_write_at_attr;
        s_services_port.display_present = adv_display_present;
    }

    if (s_keyboard_ready) {
        s_services_port.input_capabilities = MINI_INPUT_CAP_KEY;
        s_services_port.input_wait = adv_input_wait;
        s_services_port.input_flush = adv_input_flush;
    }

    adv_digital_io_configure(&s_services_port);
}

int minishell_platform_init(void)
{
    if (adv_console_prepare() != 0) return -1;

    s_display_ready = adv_display_prepare() == 0;
    if (!s_display_ready) {
        adv_console_debug_write("ADV: display unavailable; USB console remains active\n");
    }

    s_keyboard_ready = adv_keyboard_prepare() == 0;
    if (!s_keyboard_ready) {
        adv_console_debug_write("ADV: keyboard unavailable; USB input remains active\n");
    }

    if (adv_rtc_prepare() != 0) {
        adv_console_debug_write("ADV: RTC unavailable; using session UTC fallback\n");
    }

    s_filesystem_ready = adv_filesystem_prepare() == 0;
    if (!s_filesystem_ready) {
        adv_console_debug_write("ADV: /flash filesystem unavailable; continuing without persistence\n");
    }

    configure_services_port();
    return 0;
}

void minishell_platform_shutdown(void)
{
    adv_filesystem_shutdown();
}

const minishell_services_port_t *minishell_platform_services_port(void)
{
    return &s_services_port;
}

void minishell_platform_services_prepare(minishell_services_port_t *out_port)
{
    if (out_port == NULL) return;
    memset(out_port, 0, sizeof(*out_port));
    *out_port = s_services_port;
    if (s_filesystem_ready) {
        /* WAV is an ADV test Audio RX provider layered on the normal Filesystem
         * backend. It remains independent of the physical speaker TX path. */
        adv_audio_wav_configure(out_port);
    }

    /* The Cardputer speaker is a physical Audio TX endpoint and does not depend
     * on Filesystem availability. It is opened lazily by the foreground app. */
    adv_audio_speaker_configure(out_port);
}
