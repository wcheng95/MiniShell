#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

#include "adv_filesystem_handoff.h"
#include "adv_internal.h"

typedef enum {
    ADV_USBMSC_ALL = 0,
    ADV_USBMSC_FLASH,
    ADV_USBMSC_SD,
} adv_usbmsc_target_t;

typedef struct {
    bool msc_driver_ready;
    bool tinyusb_ready;
    tinyusb_msc_storage_handle_t flash_storage;
    tinyusb_msc_storage_handle_t sd_storage;
} adv_usbmsc_usb_t;

static void write_line(uint32_t row, const char *text)
{
    if (text == NULL) return;
    (void)adv_display_text_clear_at(NULL, row, 0u, 1u, 20u);
    size_t length = strlen(text);
    if (length > 20u) length = 20u;
    (void)adv_display_text_write_at(NULL, row, 0u, text, (uint32_t)length);
}

static void show_screen(const char *target, const char *status)
{
    (void)adv_display_text_clear(NULL);
    write_line(0u, "USB MSC");
    write_line(1u, target);
    write_line(2u, status);
    write_line(3u, "Copy files on PC");
    write_line(4u, "Eject drive first");
    write_line(5u, "then press Q");
    write_line(6u, "Q = return");
    (void)adv_display_present(NULL);
}

static void show_error(const char *text)
{
    (void)adv_display_text_clear(NULL);
    write_line(0u, "USB MSC ERROR");
    write_line(2u, text);
    write_line(5u, "Returning to shell");
    (void)adv_display_present(NULL);
    vTaskDelay(pdMS_TO_TICKS(750));
}

static const char *target_label(bool use_flash, bool use_sd)
{
    if (use_flash && use_sd) return "Export: flash + sd";
    if (use_flash) return "Export: flash";
    return "Export: sd";
}

static bool parse_target(int argc, char **argv, adv_usbmsc_target_t *out_target)
{
    if (out_target == NULL || argc < 1 || argv == NULL) return false;
    if (argc == 1) {
        *out_target = ADV_USBMSC_ALL;
        return true;
    }
    if (argc != 2 || argv[1] == NULL) return false;
    if (strcmp(argv[1], "all") == 0) *out_target = ADV_USBMSC_ALL;
    else if (strcmp(argv[1], "flash") == 0) *out_target = ADV_USBMSC_FLASH;
    else if (strcmp(argv[1], "sd") == 0) *out_target = ADV_USBMSC_SD;
    else return false;
    return true;
}

static void msc_event(tinyusb_msc_storage_handle_t handle,
                      tinyusb_msc_event_t *event,
                      void *arg)
{
    (void)handle;
    (void)event;
    (void)arg;
}

static esp_err_t add_flash_storage(adv_usbmsc_usb_t *usb, wl_handle_t wl)
{
    tinyusb_msc_storage_config_t config = {
        .medium.wl_handle = wl,
        .fat_fs = {
            .base_path = "/flash",
            .config = {
                .format_if_mount_failed = false,
                .max_files = 8,
                .allocation_unit_size = 4096,
            },
            .do_not_format = true,
            .format_flags = 0,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };
    return tinyusb_msc_new_storage_spiflash(&config, &usb->flash_storage);
}

static esp_err_t add_sd_storage(adv_usbmsc_usb_t *usb, sdmmc_card_t *card)
{
    tinyusb_msc_storage_config_t config = {
        .medium.card = card,
        .fat_fs = {
            .base_path = "/sd",
            .config = {
                .format_if_mount_failed = false,
                .max_files = 8,
                .allocation_unit_size = 16 * 1024,
            },
            .do_not_format = true,
            .format_flags = 0,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };
    return tinyusb_msc_new_storage_sdmmc(&config, &usb->sd_storage);
}

static esp_err_t start_usb(adv_usbmsc_usb_t *usb,
                           const adv_filesystem_handoff_t *media)
{
    tinyusb_msc_driver_config_t msc_config = {0};
    msc_config.user_flags.auto_mount_off = 1u;
    msc_config.callback = msc_event;

    esp_err_t err = tinyusb_msc_install_driver(&msc_config);
    if (err != ESP_OK) return err;
    usb->msc_driver_ready = true;

    if (media->has_flash) {
        err = add_flash_storage(usb, media->flash_wl);
        if (err != ESP_OK) return err;
    }
    if (media->has_sd) {
        err = add_sd_storage(usb, media->sd_card);
        if (err != ESP_OK) return err;
    }

    tinyusb_config_t usb_config = TINYUSB_DEFAULT_CONFIG();
    err = tinyusb_driver_install(&usb_config);
    if (err == ESP_OK) usb->tinyusb_ready = true;
    return err;
}

static void stop_usb(adv_usbmsc_usb_t *usb)
{
    if (usb->tinyusb_ready) {
        tud_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
        (void)tinyusb_driver_uninstall();
        usb->tinyusb_ready = false;
    }

    if (usb->sd_storage != NULL) {
        for (int retry = 0; retry < 50; ++retry) {
            if (tinyusb_msc_delete_storage(usb->sd_storage) == ESP_OK) {
                usb->sd_storage = NULL;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (usb->flash_storage != NULL) {
        for (int retry = 0; retry < 50; ++retry) {
            if (tinyusb_msc_delete_storage(usb->flash_storage) == ESP_OK) {
                usb->flash_storage = NULL;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    if (usb->msc_driver_ready && usb->sd_storage == NULL && usb->flash_storage == NULL) {
        (void)tinyusb_msc_uninstall_driver();
        usb->msc_driver_ready = false;
    }
}

static void wait_for_exit(void)
{
    adv_keyboard_flush();
    for (;;) {
        mini_key_event_t event = {.struct_size = sizeof(event)};
        if (adv_keyboard_read_event(&event) == MINI_OK) {
            if (event.type == MINI_KEY_EVENT_CHAR &&
                (event.codepoint == 'q' || event.codepoint == 'Q')) {
                return;
            }
            if (event.type == MINI_KEY_EVENT_SPECIAL && event.key == MINI_KEY_ESCAPE) {
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

int minishell_app_usbmsc_main(int argc, char **argv)
{
    adv_usbmsc_target_t target;
    adv_filesystem_handoff_t media = {
        .flash_wl = WL_INVALID_HANDLE,
    };
    adv_usbmsc_usb_t usb = {0};
    bool handoff_active = false;
    bool console_suspended = false;
    bool use_flash = false;
    bool use_sd = false;
    int result = 1;

    if (!parse_target(argc, argv, &target)) {
        adv_console_debug_write("usage: usbmsc [all|flash|sd]\n");
        return 1;
    }

    bool flash_available = adv_filesystem_flash_ready();
    bool sd_available = adv_filesystem_sd_ready();

    switch (target) {
        case ADV_USBMSC_ALL:
            use_flash = flash_available;
            use_sd = sd_available;
            break;
        case ADV_USBMSC_FLASH:
            if (!flash_available) {
                adv_console_debug_write("usbmsc: /flash is unavailable\n");
                return 2;
            }
            use_flash = true;
            break;
        case ADV_USBMSC_SD:
            if (!sd_available) {
                adv_console_debug_write("usbmsc: /sd is unavailable\n");
                return 2;
            }
            use_sd = true;
            break;
    }

    if (!use_flash && !use_sd) {
        adv_console_debug_write("usbmsc: no storage is available\n");
        return 2;
    }

    show_screen(target_label(use_flash, use_sd), "Preparing...");

    if (adv_filesystem_handoff_begin(use_flash, use_sd, &media) != 0) {
        show_error("storage handoff fail");
        return 3;
    }
    handoff_active = true;

    if (adv_console_suspend_for_usb() != 0) {
        show_error("USB console busy");
        goto cleanup;
    }
    console_suspended = true;

    if (start_usb(&usb, &media) != ESP_OK) {
        show_error("TinyUSB start failed");
        goto cleanup;
    }

    show_screen(target_label(use_flash, use_sd), "USB connected");
    wait_for_exit();
    result = 0;

cleanup:
    stop_usb(&usb);

    if (console_suspended) {
        (void)adv_console_resume_after_usb();
        console_suspended = false;
    }

    if (handoff_active && adv_filesystem_handoff_end() != 0) {
        adv_console_debug_write("usbmsc: failed to restore MiniShell storage\n");
        return 4;
    }

    if (result == 0) {
        adv_console_debug_write("usbmsc: storage returned to MiniShell\n");
    } else {
        adv_console_debug_write("usbmsc: failed\n");
    }
    return result;
}
