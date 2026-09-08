#include "config_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *k_profiles[] = {"Default", "User"};
static const char *k_bands[] = {"80m", "40m", "30m", "20m", "17m", "15m", "10m"};
static const int k_profile_count = (int)(sizeof(k_profiles) / sizeof(k_profiles[0]));
static const int k_band_count = (int)(sizeof(k_bands) / sizeof(k_bands[0]));

static int clamp_profile(int value)
{
    if (value < 0) return 0;
    if (value >= k_profile_count) return k_profile_count - 1;
    return value;
}

static int clamp_band(int value)
{
    if (value < 0) return 0;
    if (value >= k_band_count) return k_band_count - 1;
    return value;
}

void config_service_defaults(ConfigService *config)
{
    config->skip_tx1 = false;
    config->max_retry = 3;
    config->profile_index = 0;
    config->band_index = 3; /* 20m */
}

bool config_service_parse(ConfigService *config, const char *text)
{
    if (config == NULL || text == NULL) return false;

    ConfigService parsed;
    config_service_defaults(&parsed);

    char buffer[2048];
    size_t len = strlen(text);
    if (len >= sizeof(buffer)) return false;
    memcpy(buffer, text, len + 1u);

    for (char *line = strtok(buffer, "\n"); line != NULL; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '=');
        if (eq == NULL) continue;
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;

        if (strcmp(key, "profile") == 0) parsed.profile_index = clamp_profile(atoi(value));
        else if (strcmp(key, "band") == 0) parsed.band_index = clamp_band(atoi(value));
        else if (strcmp(key, "skip_tx1") == 0) parsed.skip_tx1 = atoi(value) != 0;
        else if (strcmp(key, "max_retry") == 0) {
            int parsed_value = atoi(value);
            parsed.max_retry = parsed_value < 0 ? 0 : parsed_value;
        }
    }

    *config = parsed;
    return true;
}

bool config_service_serialize(const ConfigService *config, char *out, size_t out_size)
{
    if (config == NULL || out == NULL || out_size == 0u) return false;

    int used = snprintf(out, out_size,
                        "# MiniFT8-V3 station.txt\n"
                        "profile=%d\n"
                        "band=%d\n"
                        "skip_tx1=%d\n"
                        "max_retry=%d\n",
                        config->profile_index,
                        config->band_index,
                        config->skip_tx1 ? 1 : 0,
                        config->max_retry);
    return used >= 0 && (size_t)used < out_size;
}

int config_service_profile_count(void)
{
    return k_profile_count;
}

const char *config_service_profile_name(int profile_index)
{
    return k_profiles[clamp_profile(profile_index)];
}

int config_service_band_count(int profile_index)
{
    (void)profile_index;
    return k_band_count;
}

const char *config_service_band_name(int profile_index, int band_index)
{
    (void)profile_index;
    return k_bands[clamp_band(band_index)];
}

void config_service_set_skip_tx1(ConfigService *config, bool enabled)
{
    config->skip_tx1 = enabled;
}

void config_service_set_max_retry(ConfigService *config, int value)
{
    config->max_retry = value < 0 ? 0 : value;
}

void config_service_set_profile(ConfigService *config, int index)
{
    config->profile_index = clamp_profile(index);
    config->band_index = clamp_band(config->band_index);
}

void config_service_set_band(ConfigService *config, int index)
{
    config->band_index = clamp_band(index);
}
