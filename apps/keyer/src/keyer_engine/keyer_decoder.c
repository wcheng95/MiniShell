#include "keyer_decoder.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *pattern;
    char ch;
} decoder_entry_t;

static const decoder_entry_t DECODER_TABLE[] = {
    {".-", 'A'},     {"-...", 'B'},   {"-.-.", 'C'},   {"-..", 'D'},
    {".", 'E'},      {"..-.", 'F'},   {"--.", 'G'},    {"....", 'H'},
    {"..", 'I'},     {".---", 'J'},   {"-.-", 'K'},    {".-..", 'L'},
    {"--", 'M'},     {"-.", 'N'},     {"---", 'O'},    {".--.", 'P'},
    {"--.-", 'Q'},   {".-.", 'R'},    {"...", 'S'},    {"-", 'T'},
    {"..-", 'U'},    {"...-", 'V'},   {".--", 'W'},    {"-..-", 'X'},
    {"-.--", 'Y'},   {"--..", 'Z'},   {"-----", '0'},  {".----", '1'},
    {"..---", '2'},  {"...--", '3'},  {"....-", '4'},  {".....", '5'},
    {"-....", '6'},  {"--...", '7'},  {"---..", '8'},  {"----.", '9'},
    {".-.-.-", '.'}, {"--..--", ','}, {"..--..", '?'}, {"-..-.", '/'},
    {"-...-", '='},
};

static bool all_dits(const keyer_engine_decoder_t *decoder)
{
    if (decoder == NULL) return false;
    for (uint8_t i = 0u; i < decoder->len; ++i) {
        if (decoder->pattern[i] != '.') return false;
    }
    return true;
}

void keyer_decoder_reset(keyer_engine_decoder_t *decoder)
{
    if (decoder == NULL) return;
    decoder->pattern[0] = '\0';
    decoder->len = 0u;
    decoder->overflow = false;
}

bool keyer_decoder_has_pending(const keyer_engine_decoder_t *decoder)
{
    return decoder != NULL && (decoder->len > 0u || decoder->overflow);
}

void keyer_decoder_append(keyer_engine_decoder_t *decoder, bool dah)
{
    if (decoder == NULL) return;
    if (decoder->len >= KEYER_ENGINE_DECODER_MAX_ELEMENTS) {
        decoder->overflow = true;
        return;
    }

    decoder->pattern[decoder->len++] = dah ? '-' : '.';
    decoder->pattern[decoder->len] = '\0';
}

keyer_decoder_result_t keyer_decoder_finalize(keyer_engine_decoder_t *decoder)
{
    keyer_decoder_result_t result = {KEYER_DECODER_RESULT_NONE, '\0'};

    if (decoder == NULL || !keyer_decoder_has_pending(decoder)) return result;

    if (decoder->overflow) {
        result.type = KEYER_DECODER_RESULT_INVALID;
        keyer_decoder_reset(decoder);
        return result;
    }

    if (decoder->len >= 6u && decoder->len <= 12u && all_dits(decoder)) {
        result.type = KEYER_DECODER_RESULT_BACKSPACE;
        result.ch = '\b';
        keyer_decoder_reset(decoder);
        return result;
    }

    if (strcmp(decoder->pattern, ".-..-.") == 0) {
        result.type = KEYER_DECODER_RESULT_ENTER;
        result.ch = '\n';
        keyer_decoder_reset(decoder);
        return result;
    }

    if (strcmp(decoder->pattern, "----") == 0) {
        result.type = KEYER_DECODER_RESULT_SPACE;
        result.ch = ' ';
        keyer_decoder_reset(decoder);
        return result;
    }

    for (size_t i = 0u; i < sizeof(DECODER_TABLE) / sizeof(DECODER_TABLE[0]); ++i) {
        if (strcmp(decoder->pattern, DECODER_TABLE[i].pattern) == 0) {
            result.type = KEYER_DECODER_RESULT_CHAR;
            result.ch = DECODER_TABLE[i].ch;
            keyer_decoder_reset(decoder);
            return result;
        }
    }

    result.type = KEYER_DECODER_RESULT_INVALID;
    keyer_decoder_reset(decoder);
    return result;
}
