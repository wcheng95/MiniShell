#include <stdlib.h>
#include <string.h>

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_partition.h"

#include "adv_filesystem_handoff.h"
#include "adv_internal.h"

#define ADV_HANDOFF_FLASH_LABEL "flash"
#define ADV_HANDOFF_SD_SPI_HOST SPI2_HOST
#define ADV_HANDOFF_SD_SCK_GPIO 40
#define ADV_HANDOFF_SD_MISO_GPIO 39
#define ADV_HANDOFF_SD_MOSI_GPIO 14
#define ADV_HANDOFF_SD_CS_GPIO 12
#define ADV_HANDOFF_SD_FREQ_KHZ 20000u

typedef struct {
    bool active;
    bool flash_ready;
    wl_handle_t flash_wl;
    bool sd_bus_ready;
    bool sd_device_ready;
    sdspi_dev_handle_t sd_device;
    sdmmc_card_t *sd_card;
} adv_handoff_state_t;

static adv_handoff_state_t s_handoff = {
    .flash_wl = WL_INVALID_HANDLE,
    .sd_device = -1,
};

static void release_flash(void)
{
    if (!s_handoff.flash_ready) return;
    (void)wl_unmount(s_handoff.flash_wl);
    s_handoff.flash_wl = WL_INVALID_HANDLE;
    s_handoff.flash_ready = false;
}

static int prepare_flash(void)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_FAT,
        ADV_HANDOFF_FLASH_LABEL);
    if (partition == NULL) return -1;

    s_handoff.flash_wl = WL_INVALID_HANDLE;
    if (wl_mount(partition, &s_handoff.flash_wl) != ESP_OK) return -1;
    s_handoff.flash_ready = true;
    return 0;
}

static void release_sd(void)
{
    if (s_handoff.sd_device_ready) {
        (void)sdspi_host_remove_device(s_handoff.sd_device);
        s_handoff.sd_device = -1;
        s_handoff.sd_device_ready = false;
    }

    free(s_handoff.sd_card);
    s_handoff.sd_card = NULL;

    if (s_handoff.sd_bus_ready) {
        (void)spi_bus_free(ADV_HANDOFF_SD_SPI_HOST);
        s_handoff.sd_bus_ready = false;
    }
}

static int prepare_sd(void)
{
    spi_bus_config_t bus_config = {
        .mosi_io_num = ADV_HANDOFF_SD_MOSI_GPIO,
        .miso_io_num = ADV_HANDOFF_SD_MISO_GPIO,
        .sclk_io_num = ADV_HANDOFF_SD_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024,
    };

    if (spi_bus_initialize(ADV_HANDOFF_SD_SPI_HOST,
                           &bus_config,
                           SPI_DMA_CH_AUTO) != ESP_OK) {
        return -1;
    }
    s_handoff.sd_bus_ready = true;

    if (sdspi_host_init() != ESP_OK) return -1;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = ADV_HANDOFF_SD_SPI_HOST;
    slot_config.gpio_cs = ADV_HANDOFF_SD_CS_GPIO;

    if (sdspi_host_init_device(&slot_config, &s_handoff.sd_device) != ESP_OK) {
        return -1;
    }
    s_handoff.sd_device_ready = true;

    s_handoff.sd_card = (sdmmc_card_t *)calloc(1u, sizeof(*s_handoff.sd_card));
    if (s_handoff.sd_card == NULL) return -1;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = s_handoff.sd_device;
    host.max_freq_khz = ADV_HANDOFF_SD_FREQ_KHZ;

    return sdmmc_card_init(&host, s_handoff.sd_card) == ESP_OK ? 0 : -1;
}

static void release_raw_media(void)
{
    release_sd();
    release_flash();
}

int adv_filesystem_handoff_begin(bool use_flash,
                                 bool use_sd,
                                 adv_filesystem_handoff_t *out_handoff)
{
    if (out_handoff == NULL || (!use_flash && !use_sd) || s_handoff.active) {
        return -1;
    }
    if ((use_flash && !adv_filesystem_flash_ready()) ||
        (use_sd && !adv_filesystem_sd_ready())) {
        return -1;
    }

    memset(out_handoff, 0, sizeof(*out_handoff));
    out_handoff->flash_wl = WL_INVALID_HANDLE;

    /* Quiesce both local FAT volumes. Only the requested medium/media are
     * initialized raw and returned to the borrower. */
    adv_filesystem_shutdown();

    if (use_flash && prepare_flash() != 0) goto fail;
    if (use_sd && prepare_sd() != 0) goto fail;

    s_handoff.active = true;
    out_handoff->has_flash = use_flash;
    out_handoff->flash_wl = s_handoff.flash_wl;
    out_handoff->has_sd = use_sd;
    out_handoff->sd_card = s_handoff.sd_card;
    return 0;

fail:
    release_raw_media();
    (void)adv_filesystem_prepare();
    return -1;
}

int adv_filesystem_handoff_end(void)
{
    if (!s_handoff.active) return -1;

    release_raw_media();
    s_handoff.active = false;
    return adv_filesystem_prepare();
}
