#pragma once
#include "minishell/api.h"
#include <stdbool.h>

#define ADV_MIRROR_COLUMNS 20u
#define ADV_MIRROR_ROWS 7u
#define ADV_MIRROR_CELLS (ADV_MIRROR_COLUMNS * ADV_MIRROR_ROWS)
#define ADV_MIRROR_PAYLOAD 284u
#define ADV_REMOTE_KEYS 16u
#define ADV_MIRROR_QUERY_CAP 32u

typedef struct {
    uint32_t generation;
    uint8_t cells[ADV_MIRROR_CELLS];
    uint8_t attrs[ADV_MIRROR_CELLS];
} adv_display_snapshot_t;

#ifdef __cplusplus
extern "C" {
#endif
void adv_display_snapshot(adv_display_snapshot_t *out);
bool adv_remote_input_start(void);
void adv_remote_input_stop(void);
mini_result_t adv_remote_input_push(const mini_key_event_t *event);
bool adv_mirror_parse_key(const char *query, mini_key_event_t *event);
void adv_mirror_encode_screen(const adv_display_snapshot_t *screen, uint8_t *out);
#ifdef __cplusplus
}
#endif
