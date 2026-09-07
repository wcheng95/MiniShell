#ifndef MINIFT8_CONFIG_SERVICE_H
#define MINIFT8_CONFIG_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include "minift8/app_types.h"

typedef struct {
    Mode saved_mode;
    bool skip_tx1;
    int max_retry;
    int profile_index[MODE_COUNT];
    int band_index[MODE_COUNT];
} ConfigService;

void config_service_defaults(ConfigService *config);
bool config_service_parse(ConfigService *config, const char *text);
bool config_service_serialize(const ConfigService *config, char *out, size_t out_size);

int config_service_profile_count(Mode mode);
const char *config_service_profile_name(Mode mode, int profile_index);
int config_service_band_count(Mode mode, int profile_index);
const char *config_service_band_name(Mode mode, int profile_index, int band_index);

void config_service_set_saved_mode(ConfigService *config, Mode mode);
void config_service_set_skip_tx1(ConfigService *config, bool enabled);
void config_service_set_max_retry(ConfigService *config, int value);
void config_service_set_profile(ConfigService *config, Mode mode, int index);
void config_service_set_band(ConfigService *config, Mode mode, int index);

#endif
