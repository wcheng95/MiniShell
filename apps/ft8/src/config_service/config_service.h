#ifndef FT8_CONFIG_SERVICE_H
#define FT8_CONFIG_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FT8_CONFIG_CALLSIGN_CAP 14u
#define FT8_CONFIG_GRID_CAP 7u
#define FT8_CONFIG_FREETEXT_CAP 14u
#define FT8_CONFIG_FD_EXCHANGE_CAP 12u

/* Selected list index; named nonzero constants describe the default list only. */
typedef uint8_t Ft8ConfigCqType;
#define FT8_CONFIG_CQ          ((Ft8ConfigCqType)0u)
#define FT8_CONFIG_CQ_SOTA     ((Ft8ConfigCqType)1u)
#define FT8_CONFIG_CQ_POTA     ((Ft8ConfigCqType)2u)
#define FT8_CONFIG_CQ_QRP      ((Ft8ConfigCqType)3u)
#define FT8_CONFIG_CQ_FD       ((Ft8ConfigCqType)4u)
#define FT8_CONFIG_CQ_MODIFIERS_MAX 16u
#define FT8_CONFIG_CQ_MODIFIER_CAP 5u

typedef enum { FT8_OFFSET_RANDOM = 0, FT8_OFFSET_FIXED = 1, FT8_OFFSET_RX = 2 } Ft8OffsetSource;

typedef struct {
    char callsign[FT8_CONFIG_CALLSIGN_CAP];
    char grid[FT8_CONFIG_GRID_CAP];
    char cq_freetext[FT8_CONFIG_FREETEXT_CAP];
    char free_text[FT8_CONFIG_FREETEXT_CAP];
    char fd_exchange[FT8_CONFIG_FD_EXCHANGE_CAP];
    bool skip_tx1;
    bool rxtx_log;
    int max_retry;
    int profile_index;
    int band_index;
    Ft8ConfigCqType cq_type; /* Index: 0 plain CQ, 1..count configured modifiers. */
    uint8_t cq_modifier_count;
    char cq_modifiers[FT8_CONFIG_CQ_MODIFIERS_MAX][FT8_CONFIG_CQ_MODIFIER_CAP];
    Ft8OffsetSource offset_src;
    int16_t fixed_offset_hz;
} ConfigService;

void config_service_defaults(ConfigService *config);
bool config_service_parse(ConfigService *config, const char *text);
bool config_service_serialize(const ConfigService *config, char *out, size_t out_size);

int config_service_profile_count(void);
const char *config_service_profile_name(int profile_index);
int config_service_band_count(int profile_index);
const char *config_service_band_name(int profile_index, int band_index);
/* Canonical FT8 dial frequency; zero for an invalid band index. */
uint32_t config_service_band_dial_hz(int band_index);

void config_service_set_skip_tx1(ConfigService *config, bool enabled);
void config_service_set_max_retry(ConfigService *config, int value);
void config_service_set_profile(ConfigService *config, int index);
void config_service_set_band(ConfigService *config, int index);
const char *config_service_cq_modifier(const ConfigService *config, unsigned index);
bool config_service_set_cq_type(ConfigService *config, int value);
bool config_service_set_cq_freetext(ConfigService *config, const char *text);
bool config_service_set_free_text(ConfigService *config, const char *text);
bool config_service_set_fd_exchange(ConfigService *config, const char *text);

#endif
