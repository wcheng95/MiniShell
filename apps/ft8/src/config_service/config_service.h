#ifndef FT8_CONFIG_SERVICE_H
#define FT8_CONFIG_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#include "ft8/app_types.h"

#define FT8_CONFIG_CALLSIGN_CAP 14u
#define FT8_CONFIG_GRID_CAP 7u
#define FT8_CONFIG_FREETEXT_CAP 14u
#define FT8_CONFIG_FD_EXCHANGE_CAP 12u

typedef uint8_t Ft8ConfigCqType;
#define FT8_CONFIG_CQ          ((Ft8ConfigCqType)0u)
#define FT8_CONFIG_CQ_SOTA     ((Ft8ConfigCqType)1u)
#define FT8_CONFIG_CQ_POTA     ((Ft8ConfigCqType)2u)
#define FT8_CONFIG_CQ_QRP      ((Ft8ConfigCqType)3u)
#define FT8_CONFIG_CQ_FD       ((Ft8ConfigCqType)4u)
#define FT8_CONFIG_CQ_FREETEXT ((Ft8ConfigCqType)5u)

typedef struct {
    char callsign[FT8_CONFIG_CALLSIGN_CAP];
    char grid[FT8_CONFIG_GRID_CAP];
    char cq_freetext[FT8_CONFIG_FREETEXT_CAP];
    char free_text[FT8_CONFIG_FREETEXT_CAP];
    char fd_exchange[FT8_CONFIG_FD_EXCHANGE_CAP];
    bool skip_tx1;
    int max_retry;
    int profile_index;
    int band_index;
    Ft8ConfigCqType cq_type;
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
bool config_service_set_cq_type(ConfigService *config, int value);
bool config_service_set_cq_freetext(ConfigService *config, const char *text);
bool config_service_set_free_text(ConfigService *config, const char *text);
bool config_service_set_fd_exchange(ConfigService *config, const char *text);

#endif
