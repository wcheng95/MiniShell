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

#define MINICW_OP_ENTRY_CAP 192U
typedef enum { STORAGE_OP_OK, STORAGE_OP_MISSING, STORAGE_OP_FAILED, STORAGE_OP_TRUNCATED } storage_op_result_t;
/* Caller owns the session-long table. No allocation or runtime reload. */
storage_op_result_t storage_op_parse(const char *text, keyer_op_entry_t entries[MINICW_OP_ENTRY_CAP], size_t *count);
storage_op_result_t storage_op_load(keyer_op_entry_t entries[MINICW_OP_ENTRY_CAP], size_t *count);
