#ifndef FT8_CONFIG_SERVICE_H
#define FT8_CONFIG_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#include "ft8/app_types.h"

typedef struct {
    bool skip_tx1;
    int max_retry;
    int profile_index;
    int band_index;
} ConfigService;

void config_service_defaults(ConfigService *config);
bool config_service_parse(ConfigService *config, const char *text);
bool config_service_serialize(const ConfigService *config, char *out, size_t out_size);

int config_service_profile_count(void);
const char *config_service_profile_name(int profile_index);
int config_service_band_count(int profile_index);
const char *config_service_band_name(int profile_index, int band_index);

void config_service_set_skip_tx1(ConfigService *config, bool enabled);
void config_service_set_max_retry(ConfigService *config, int value);
void config_service_set_profile(ConfigService *config, int index);
void config_service_set_band(ConfigService *config, int index);

#endif
