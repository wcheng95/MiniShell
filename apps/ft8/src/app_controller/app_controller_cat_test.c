#include "app_controller.h"
#include "config_service.h"
#include "storage_service.h"
#include "radio_control.h"
#include <math.h>
#include <stddef.h>

mini_result_t app_controller_cat_test(const mini_api_t *api, const char *station_path,
                                      const char *endpoint, float tone_hz, uint32_t duration_ms)
{
    if (!isfinite(tone_hz) || tone_hz < 300.0f || tone_hz > 2700.0f ||
        duration_ms < 100u || duration_ms > 2000u || !station_path || !endpoint || !*endpoint)
        return MINI_ERR_INVALID;
    if (!api || api->struct_size < offsetof(mini_api_t, time_location) + sizeof(api->time_location) ||
        !api->time_location || api->time_location->struct_size <
            offsetof(mini_time_location_api_t, sleep_ms) + sizeof(api->time_location->sleep_ms) ||
        !api->time_location->sleep_ms) return MINI_ERR_UNSUPPORTED;
    StorageService storage;
    ConfigService config;
    char text[2048];
    if (!storage_service_init(&storage, api->fs)) return MINI_ERR_UNSUPPORTED;
    config_service_defaults(&config);
    StorageReadResult loaded = storage_service_read_text(&storage, station_path, text, sizeof(text));
    if (loaded == STORAGE_READ_ERROR) return MINI_ERR_IO;
    if (loaded == STORAGE_READ_FOUND && !config_service_parse(&config, text)) return MINI_ERR_INVALID;

    RadioControl radio = {0};
    mini_result_t result = radio_control_open_qmx(&radio, api, endpoint,
                                                  config_service_band_dial_hz(config.band_index));
    if (result == MINI_OK) result = radio_control_begin_tx(&radio);
    if (result == MINI_OK) result = radio_control_set_tone_hz(&radio, tone_hz);
    if (result == MINI_OK) result = api->time_location->sleep_ms(duration_ms);
    if (result == MINI_OK) result = radio_control_end_tx(&radio);
    mini_result_t closed = radio_control_close(&radio);
    return result == MINI_OK ? closed : result;
}
