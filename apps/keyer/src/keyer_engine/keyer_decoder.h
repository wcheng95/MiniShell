#pragma once

#include <stdbool.h>

#include "keyer_engine.h"

typedef enum {
    KEYER_DECODER_RESULT_NONE = 0,
    KEYER_DECODER_RESULT_CHAR,
    KEYER_DECODER_RESULT_BACKSPACE,
    KEYER_DECODER_RESULT_ENTER,
    KEYER_DECODER_RESULT_SPACE,
    KEYER_DECODER_RESULT_INVALID,
} keyer_decoder_result_type_t;

typedef struct {
    keyer_decoder_result_type_t type;
    char ch;
} keyer_decoder_result_t;

void keyer_decoder_reset(keyer_engine_decoder_t *decoder);
bool keyer_decoder_has_pending(const keyer_engine_decoder_t *decoder);
void keyer_decoder_append(keyer_engine_decoder_t *decoder, bool dah);
keyer_decoder_result_t keyer_decoder_finalize(keyer_engine_decoder_t *decoder);
