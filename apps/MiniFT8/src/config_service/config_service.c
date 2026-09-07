#include "config_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *k_profiles[MODE_COUNT][2] = {
    {"Default", "User"},
    {"Default", "User"},
    {"Default", "User"},
    {"QRP", "User"}
};

static const char *k_bands[] = {"80m", "40m", "30m", "20m", "17m", "15m", "10m"};
static const int k_band_count = (int)(sizeof(k_bands) / sizeof(k_bands[0]));

static int clamp_mode(int value) {
    if (value < 0 || value >= MODE_COUNT) return MODE_FT8;
    return value;
}

static int clamp_profile(int value) {
    if (value < 0) return 0;
    if (value > 1) return 1;
    return value;
}

static int clamp_band(int value) {
    if (value < 0) return 0;
    if (value >= k_band_count) return k_band_count - 1;
    return value;
}

void config_service_defaults(ConfigService *config) {
    config->saved_mode = MODE_FT8;
    config->skip_tx1 = false;
    config->max_retry = 3;
    for (int i = 0; i < MODE_COUNT; ++i) {
        config->profile_index[i] = 0;
        config->band_index[i] = 3; /* 20m */
    }
}

bool config_service_parse(ConfigService *config, const char *text) {
    if (!config || !text) return false;

    ConfigService parsed;
    config_service_defaults(&parsed);

    char buffer[2048];
    size_t len = strlen(text);
    if (len >= sizeof(buffer)) return false;
    memcpy(buffer, text, len + 1);

    for (char *line = strtok(buffer, "\n"); line; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;

        if (strcmp(key, "mode") == 0) parsed.saved_mode = (Mode)clamp_mode(atoi(value));
        else if (strcmp(key, "skip_tx1") == 0) parsed.skip_tx1 = atoi(value) != 0;
        else if (strcmp(key, "max_retry") == 0) parsed.max_retry = atoi(value) < 0 ? 0 : atoi(value);
        else {
            for (int m = 0; m < MODE_COUNT; ++m) {
                char profile_key[32];
                char band_key[32];
                snprintf(profile_key, sizeof(profile_key), "mode%d_profile", m);
                snprintf(band_key, sizeof(band_key), "mode%d_band", m);
                if (strcmp(key, profile_key) == 0) parsed.profile_index[m] = clamp_profile(atoi(value));
                if (strcmp(key, band_key) == 0) parsed.band_index[m] = clamp_band(atoi(value));
            }
        }
    }

    *config = parsed;
    return true;
}

bool config_service_serialize(const ConfigService *config, char *out, size_t out_size) {
    if (!config || !out || out_size == 0) return false;

    int used = snprintf(out, out_size,
                        "# MiniFT8-V3 Station.txt\n"
                        "mode=%d\n"
                        "skip_tx1=%d\n"
                        "max_retry=%d\n",
                        (int)config->saved_mode,
                        config->skip_tx1 ? 1 : 0,
                        config->max_retry);
    if (used < 0 || (size_t)used >= out_size) return false;

    size_t pos = (size_t)used;
    for (int m = 0; m < MODE_COUNT; ++m) {
        int n = snprintf(out + pos, out_size - pos,
                         "mode%d_profile=%d\nmode%d_band=%d\n",
                         m, config->profile_index[m],
                         m, config->band_index[m]);
        if (n < 0 || (size_t)n >= out_size - pos) return false;
        pos += (size_t)n;
    }
    return true;
}

int config_service_profile_count(Mode mode) {
    (void)mode;
    return 2;
}

const char *config_service_profile_name(Mode mode, int profile_index) {
    int m = clamp_mode((int)mode);
    return k_profiles[m][clamp_profile(profile_index)];
}

int config_service_band_count(Mode mode, int profile_index) {
    (void)mode;
    (void)profile_index;
    return k_band_count;
}

const char *config_service_band_name(Mode mode, int profile_index, int band_index) {
    (void)mode;
    (void)profile_index;
    return k_bands[clamp_band(band_index)];
}

void config_service_set_saved_mode(ConfigService *config, Mode mode) {
    config->saved_mode = (Mode)clamp_mode((int)mode);
}

void config_service_set_skip_tx1(ConfigService *config, bool enabled) {
    config->skip_tx1 = enabled;
}

void config_service_set_max_retry(ConfigService *config, int value) {
    config->max_retry = value < 0 ? 0 : value;
}

void config_service_set_profile(ConfigService *config, Mode mode, int index) {
    int m = clamp_mode((int)mode);
    config->profile_index[m] = clamp_profile(index);
    config->band_index[m] = clamp_band(config->band_index[m]);
}

void config_service_set_band(ConfigService *config, Mode mode, int index) {
    int m = clamp_mode((int)mode);
    config->band_index[m] = clamp_band(index);
}
