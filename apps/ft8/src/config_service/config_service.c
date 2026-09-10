#include "config_service.h"

#include <ctype.h>
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

static int clamp_cq_type(int value)
{
    if (value < (int)FT8_CONFIG_CQ) return (int)FT8_CONFIG_CQ;
    if (value > (int)FT8_CONFIG_CQ_FREETEXT) return (int)FT8_CONFIG_CQ_FREETEXT;
    return value;
}

static void strip_cr(char *text)
{
    size_t len;
    if (text == NULL) return;
    len = strlen(text);
    if (len > 0u && text[len - 1u] == '\r') text[len - 1u] = '\0';
}

static bool copy_checked(char *out, size_t out_size, const char *text)
{
    size_t len;
    if (out == NULL || out_size == 0u || text == NULL) return false;
    len = strlen(text);
    if (len >= out_size) return false;
    memcpy(out, text, len + 1u);
    return true;
}

static bool copy_upper_checked(char *out, size_t out_size, const char *text)
{
    size_t i;
    size_t len;

    if (out == NULL || out_size == 0u || text == NULL) return false;
    len = strlen(text);
    if (len >= out_size) return false;

    for (i = 0u; i < len; ++i) {
        out[i] = (char)toupper((unsigned char)text[i]);
    }
    out[len] = '\0';
    return true;
}

void config_service_defaults(ConfigService *config)
{
    memset(config, 0, sizeof(*config));
    config->skip_tx1 = false;
    config->max_retry = 3;
    config->profile_index = 0;
    config->band_index = 3; /* 20m */
    config->cq_type = FT8_CONFIG_CQ;
}

bool config_service_parse(ConfigService *config, const char *text)
{
    ConfigService parsed;
    char buffer[2048];
    size_t len;

    if (config == NULL || text == NULL) return false;

    config_service_defaults(&parsed);
    len = strlen(text);
    if (len >= sizeof(buffer)) return false;
    memcpy(buffer, text, len + 1u);

    for (char *line = strtok(buffer, "\n"); line != NULL; line = strtok(NULL, "\n")) {
        char *eq;
        const char *key;
        char *value;

        strip_cr(line);
        eq = strchr(line, '=');
        if (eq == NULL) continue;
        *eq = '\0';
        key = line;
        value = eq + 1;

        if (strcmp(key, "callsign") == 0) {
            if (!copy_upper_checked(parsed.callsign, sizeof(parsed.callsign), value)) return false;
        } else if (strcmp(key, "grid") == 0) {
            if (!copy_upper_checked(parsed.grid, sizeof(parsed.grid), value)) return false;
        } else if (strcmp(key, "profile") == 0) {
            parsed.profile_index = clamp_profile(atoi(value));
        } else if (strcmp(key, "band") == 0) {
            parsed.band_index = clamp_band(atoi(value));
        } else if (strcmp(key, "skip_tx1") == 0) {
            parsed.skip_tx1 = atoi(value) != 0;
        } else if (strcmp(key, "max_retry") == 0) {
            int parsed_value = atoi(value);
            parsed.max_retry = parsed_value < 0 ? 0 : parsed_value;
        } else if (strcmp(key, "cq_type") == 0) {
            parsed.cq_type = (Ft8ConfigCqType)clamp_cq_type(atoi(value));
        } else if (strcmp(key, "cq_ft") == 0) {
            if (!copy_checked(parsed.cq_freetext, sizeof(parsed.cq_freetext), value)) return false;
        } else if (strcmp(key, "free_text") == 0) {
            if (!copy_checked(parsed.free_text, sizeof(parsed.free_text), value)) return false;
        } else if (strcmp(key, "fd_exchange") == 0) {
            if (!copy_upper_checked(parsed.fd_exchange, sizeof(parsed.fd_exchange), value)) return false;
        }
    }

    *config = parsed;
    return true;
}

bool config_service_serialize(const ConfigService *config, char *out, size_t out_size)
{
    int used;
    if (config == NULL || out == NULL || out_size == 0u) return false;

    used = snprintf(out, out_size,
                    "# MiniFT8-V3 station.txt\n"
                    "callsign=%s\n"
                    "grid=%s\n"
                    "profile=%d\n"
                    "band=%d\n"
                    "skip_tx1=%d\n"
                    "max_retry=%d\n"
                    "cq_type=%u\n"
                    "cq_ft=%s\n"
                    "free_text=%s\n"
                    "fd_exchange=%s\n",
                    config->callsign,
                    config->grid,
                    config->profile_index,
                    config->band_index,
                    config->skip_tx1 ? 1 : 0,
                    config->max_retry,
                    (unsigned)config->cq_type,
                    config->cq_freetext,
                    config->free_text,
                    config->fd_exchange);
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

bool config_service_set_cq_type(ConfigService *config, int value)
{
    if (config == NULL || value < (int)FT8_CONFIG_CQ ||
        value > (int)FT8_CONFIG_CQ_FREETEXT) {
        return false;
    }
    config->cq_type = (Ft8ConfigCqType)value;
    return true;
}

bool config_service_set_cq_freetext(ConfigService *config, const char *text)
{
    return config != NULL &&
           copy_checked(config->cq_freetext, sizeof(config->cq_freetext), text);
}

bool config_service_set_free_text(ConfigService *config, const char *text)
{
    return config != NULL &&
           copy_checked(config->free_text, sizeof(config->free_text), text);
}

bool config_service_set_fd_exchange(ConfigService *config, const char *text)
{
    return config != NULL &&
           copy_upper_checked(config->fd_exchange, sizeof(config->fd_exchange), text);
}
