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

typedef enum { STORAGE_OP_OK, STORAGE_OP_MISSING, STORAGE_OP_FAILED } storage_op_result_t;
/* Storage/app owns the session table; Keyer only borrows it until detached. */
storage_op_result_t storage_op_load(keyer_op_entry_t **entries, size_t *count);
void storage_op_free(keyer_op_entry_t *entries);

bool storage_transcript_append(uint32_t date, const char *line);
