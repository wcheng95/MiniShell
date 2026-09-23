#include "js8_reassembly.h"
#include <string.h>

void js8_rx_reassembly_init(Js8RxReassembly *state)
{
    if (state) memset(state, 0, sizeof(*state));
}

static int closest(const Js8RxReassembly *state, int32_t frequency)
{
    int found = -1;
    int64_t best = 10001;
    for (int i = 0; i < JS8_RX_CONTEXTS; ++i) {
        const Js8RxContext *c = &state->contexts[i];
        int64_t delta = (int64_t)frequency - c->message.frequency_millihz;
        if (delta < 0) delta = -delta;
        if (c->active && delta < best) { found = i; best = delta; }
    }
    return found;
}

Js8RxStatus js8_rx_reassembly_feed(Js8RxReassembly *state, const Js8RxFragment *e,
                                  Js8RxMessage *message, Js8RxDrops *drops)
{
    if (!state || !e || !message || !drops || e->tx_flags > 7 ||
        (e->kind != JS8_RX_FRAGMENT_DIRECTED && e->kind != JS8_RX_FRAGMENT_DATA))
        return JS8_RX_INVALID;
    if (e->kind == JS8_RX_FRAGMENT_DIRECTED) {
        if (e->command_code > 31 || !e->from[0] || !e->to[0] ||
            !memchr(e->from, 0, sizeof(e->from)) || !memchr(e->to, 0, sizeof(e->to)))
            return JS8_RX_INVALID;
    } else if (e->text_len && !e->text) return JS8_RX_INVALID;

    memset(drops, 0, sizeof(*drops));
    for (int i = 0; i < JS8_RX_CONTEXTS; ++i) {
        Js8RxContext *c = &state->contexts[i];
        if (c->active && e->slot_index > c->message.last_slot &&
            e->slot_index - c->message.last_slot > 6) {
            drops->expired |= (uint8_t)(1u << i);
            memset(c, 0, sizeof(*c));
        }
    }
    if (e->kind == JS8_RX_FRAGMENT_DIRECTED &&
        (e->command_code != 31 || !(e->tx_flags & JS8_TX_FLAG_FIRST) ||
         !strcmp(e->from, "<....>") || !strcmp(e->to, "<....>")))
        return JS8_RX_IGNORED;

    int index = closest(state, e->frequency_millihz);
    if (e->kind == JS8_RX_FRAGMENT_DIRECTED) {
        if (index >= 0) drops->replaced = (uint8_t)(1u << index);
        else {
            for (int i = 0; i < JS8_RX_CONTEXTS; ++i)
                if (!state->contexts[i].active) { index = i; break; }
            if (index < 0) {
                index = 0;
                for (int i = 1; i < JS8_RX_CONTEXTS; ++i)
                    if (state->contexts[i].message.last_slot < state->contexts[index].message.last_slot)
                        index = i;
                drops->evicted = (uint8_t)(1u << index);
            }
        }
        Js8RxContext *c = &state->contexts[index];
        memset(c, 0, sizeof(*c));
        c->active = 1;
        c->command_code = e->command_code;
        memcpy(c->message.from, e->from, sizeof(e->from));
        memcpy(c->message.to, e->to, sizeof(e->to));
        c->message.first_slot = c->message.last_slot = e->slot_index;
        c->message.frequency_millihz = e->frequency_millihz;
    } else {
        if (index < 0) return JS8_RX_ORPHAN;
        Js8RxContext *c = &state->contexts[index];
        if (e->slot_index <= c->message.last_slot) return JS8_RX_DUPLICATE;
        if (e->slot_index - c->message.last_slot > 1) c->gap = 1;
        if (e->text_len > JS8_RX_MESSAGE_SIZE - 1u - c->message.text_len) {
            memset(c, 0, sizeof(*c));
            return JS8_RX_OVERFLOW;
        }
        if (e->text_len) memcpy(c->message.text + c->message.text_len, e->text, e->text_len);
        c->message.text_len += e->text_len;
        c->message.text[c->message.text_len] = 0;
        c->message.last_slot = e->slot_index;
        c->message.frequency_millihz = e->frequency_millihz;
    }
    Js8RxContext *c = &state->contexts[index];
    Js8RxStatus result = c->gap ? JS8_RX_GAP : JS8_RX_ACCEPTED;
    if (e->tx_flags & JS8_TX_FLAG_LAST) {
        if (!c->gap) { *message = c->message; result = JS8_RX_COMPLETE; }
        memset(c, 0, sizeof(*c));
    }
    return result;
}
