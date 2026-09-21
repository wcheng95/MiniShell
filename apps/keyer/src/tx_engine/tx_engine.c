#include "tx_engine.h"
#include <stddef.h>

/* One encode table for keyboard and all five memories. Physical decode keeps
 * its proven gesture semantics independently. */
static const char *const patterns[] = {
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
    "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
    "..-", "...-", ".--", "-..-", "-.--", "--..",
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.",
    ".-.-.-", "--..--", "..--..", "-..-.", "-...-", "-.-.--", ".----.", "-....-",
    "-.--.", "-.--.-", ".-..-.", "---...", "-.-.-.", ".-.-.", "..--.-", ".--.-.",
};
static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?/=!'-()\":;+_@";
static char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }
static const char *encode(char c)
{
    for (unsigned i = 0; alphabet[i]; ++i) if (alphabet[i] == c) return patterns[i];
    return NULL;
}
void tx_engine_cancel(tx_engine_t *t)
{
    t->count = 0; t->fifo[0] = 0; t->pattern = NULL; t->element = 0;
    t->phase = TX_IDLE; t->down = false; t->tune = false;
    t->repeat = false; t->repeat_waiting = false; t->immediate = false;
    t->due_us = t->typed_us = t->repeat_due_us = t->tune_start_us = 0;
}
void tx_engine_init(tx_engine_t *t) { tx_engine_cancel(t); }
static tx_result_t append(tx_engine_t *t, const char *text, uint64_t now)
{
    unsigned n = 0;
    while (text[n]) {
        if (n >= KEYER_TX_CAPACITY - t->count) return TX_FULL;
        if (text[n] != ' ' && !encode(upper(text[n]))) return TX_UNSUPPORTED;
        ++n;
    }
    for (unsigned i = 0; i < n; ++i) t->fifo[t->count++] = upper(text[i]);
    t->fifo[t->count] = 0;
    t->typed_us = now;
    if (t->phase == TX_IDLE) t->immediate = false;
    return TX_OK;
}
tx_result_t tx_engine_append(tx_engine_t *t, const char *text, uint64_t now)
{
    t->repeat = t->repeat_waiting = false;
    return append(t, text, now);
}
tx_result_t tx_engine_memory(tx_engine_t *t, const keyer_config_t *c, unsigned i, uint64_t now)
{
    if (i >= 5) return TX_UNSUPPORTED;
    if (i) t->repeat = t->repeat_waiting = false;
    tx_result_t rc = append(t, c->messages[i], now);
    if (rc == TX_OK) {
        t->repeat = i == 0 && c->messages[0][0];
        t->repeat_waiting = false;
    }
    return rc;
}
void tx_engine_backspace(tx_engine_t *t)
{
    if (t->count) t->fifo[--t->count] = 0;
}
void tx_engine_start(tx_engine_t *t) { t->immediate = true; }
void tx_engine_tune(tx_engine_t *t, uint64_t now)
{
    bool active = t->tune;
    tx_engine_cancel(t);
    t->tune = !active;
    t->tune_start_us = now;
}
static char pop(tx_engine_t *t)
{
    char c = t->fifo[0];
    --t->count;
    for (unsigned i = 0; i <= t->count; ++i) t->fifo[i] = t->fifo[i + 1];
    return c;
}
static void element(tx_engine_t *t, uint64_t now, uint32_t unit)
{
    t->down = true; t->phase = TX_ELEMENT;
    t->due_us = now + unit * (t->pattern[t->element] == '-' ? 3u : 1u);
}
bool tx_engine_step(tx_engine_t *t, const keyer_config_t *c, uint64_t now, bool physical)
{
    if (physical) { tx_engine_cancel(t); return false; }
    if (t->tune) {
        if (c->tune_timeout_s && now - t->tune_start_us >= (uint64_t)c->tune_timeout_s * 1000000u)
            tx_engine_cancel(t);
        else t->down = true;
        return t->down;
    }
    uint32_t unit = 1200000u / c->wpm;
    if (t->phase != TX_IDLE && now < t->due_us) return t->down;
    if (t->phase == TX_ELEMENT) {
        t->down = false;
        if (t->pattern[++t->element]) {
            t->phase = TX_ELEMENT_GAP; t->due_us = now + unit;
        } else {
            t->pattern = NULL; t->phase = TX_CHAR_GAP;
            t->due_us = now + 3u * unit;
            if (t->count && t->fifo[0] == ' ') {
                while (t->count && t->fifo[0] == ' ') (void)pop(t);
                t->phase = TX_WORD_GAP; t->due_us = now + 7u * unit;
            }
            if (!t->count && t->repeat) {
                t->repeat_waiting = true;
                t->repeat_due_us = now + (uint64_t)c->repeat_s * 1000000u;
            }
        }
        return false;
    }
    if (t->phase == TX_ELEMENT_GAP) { element(t, now, unit); return true; }
    if (t->phase == TX_CHAR_GAP || t->phase == TX_WORD_GAP) {
        /* Spaces arriving during the character gap extend it to seven units. */
        if (t->phase == TX_CHAR_GAP && t->count && t->fifo[0] == ' ') {
            while (t->count && t->fifo[0] == ' ') (void)pop(t);
            t->phase = TX_WORD_GAP; t->due_us = now + 4u * unit;
            return false;
        }
        t->phase = TX_IDLE;
        t->immediate = true;
    }
    if (!t->count) {
        if (t->repeat && !t->repeat_waiting) {
            t->repeat_waiting = true;
            t->repeat_due_us = now + (uint64_t)c->repeat_s * 1000000u;
        }
        if (t->repeat_waiting && now >= t->repeat_due_us) {
            tx_result_t rc = append(t, c->messages[0], now);
            t->repeat_waiting = false;
            t->immediate = true; /* Repeat interval replaces TxDelay. */
            if (rc != TX_OK || !t->count) t->repeat = false;
        }
        if (!t->count) return false;
    }
    if (!t->immediate && now - t->typed_us < (uint64_t)c->tx_delay_s * 1000000u) return false;
    if (t->fifo[0] == ' ') {
        while (t->count && t->fifo[0] == ' ') (void)pop(t);
        t->phase = TX_WORD_GAP; t->due_us = now + 7u * unit;
        return false;
    }
    t->pattern = encode(pop(t)); t->element = 0;
    element(t, now, unit);
    return true;
}
