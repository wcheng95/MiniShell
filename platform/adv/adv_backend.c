#include <stdio.h>
#include <string.h>

#include "adv_internal.h"
#include "minishell_services.h"
#include "platform_backend.h"

static minishell_services_port_t s_services_port;
static bool s_display_ready;
static bool s_keyboard_ready;
static bool s_filesystem_ready;

typedef struct {
    const char *name;
    uint64_t free_bytes;
    uint64_t largest_free_block;
} adv_ram_checkpoint_t;

static void capture_ram_checkpoint(adv_ram_checkpoint_t *checkpoint, const char *name)
{
    if (checkpoint == NULL) return;

    checkpoint->name = name;
    checkpoint->free_bytes = 0u;
    checkpoint->largest_free_block = 0u;
    (void)adv_memory_get_info(NULL, &checkpoint->free_bytes,
                              &checkpoint->largest_free_block);
}

static void print_ram_ledger(const adv_ram_checkpoint_t *checkpoints, size_t count)
{
    char line[96];

    adv_console_debug_write("ADV RAM boot ledger (bytes):\n");
    for (size_t i = 0; i < count; ++i) {
        const int written = snprintf(line, sizeof(line),
                                     "  %-16s free=%llu largest=%llu\n",
                                     checkpoints[i].name,
                                     (unsigned long long)checkpoints[i].free_bytes,
                                     (unsigned long long)checkpoints[i].largest_free_block);
        if (written > 0) {
            adv_console_debug_write(line);
        }
    }
}

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

    if (s_filesystem_ready) {
        adv_filesystem_configure(&s_services_port);
        adv_time_location_configure(&s_services_port);
    }

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
}

int minishell_platform_init(void)
{
    adv_ram_checkpoint_t ram[6];

    capture_ram_checkpoint(&ram[0], "boot entry");

    if (adv_console_prepare() != 0) return -1;
    capture_ram_checkpoint(&ram[1], "after console");

    s_display_ready = adv_display_prepare() == 0;
    if (!s_display_ready) {
        adv_console_debug_write("ADV: display unavailable; USB console remains active\n");
    }
    capture_ram_checkpoint(&ram[2], "after display");

    s_keyboard_ready = adv_keyboard_prepare() == 0;
    if (!s_keyboard_ready) {
        adv_console_debug_write("ADV: keyboard unavailable; USB input remains active\n");
    }
    capture_ram_checkpoint(&ram[3], "after keyboard");

    s_filesystem_ready = adv_filesystem_prepare() == 0;
    if (!s_filesystem_ready) {
        adv_console_debug_write("ADV: /flash filesystem unavailable; continuing without persistence\n");
    }
    capture_ram_checkpoint(&ram[4], "after filesystem");

    configure_services_port();
    capture_ram_checkpoint(&ram[5], "shell ready");

    /* Temporary diagnostic: capture all values before printing so diagnostic
     * formatting/output cannot perturb the intermediate heap measurements. */
    print_ram_ledger(ram, sizeof(ram) / sizeof(ram[0]));
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
}
