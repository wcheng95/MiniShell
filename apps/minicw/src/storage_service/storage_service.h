#pragma once
#include "keyer_service.h"
typedef struct {
    uint8_t volume;
    uint16_t tone_hz;
    keyer_key_in_mode_t key_in;
    uint8_t key_in_wpm;
    keyer_config_t keyer;
} storage_snapshot_t;
typedef enum { STORAGE_OK, STORAGE_MISSING, STORAGE_INVALID, STORAGE_READ_FAILED } storage_load_t;
void storage_defaults(storage_snapshot_t *out);
storage_load_t storage_load(storage_snapshot_t *out);
bool storage_save(const storage_snapshot_t *snapshot);
bool storage_parse(const char *text, storage_snapshot_t *out);
bool storage_serialize(const storage_snapshot_t *snapshot, char *out, size_t size);
bool storage_equal(const storage_snapshot_t *a, const storage_snapshot_t *b);
