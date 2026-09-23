#include "js8_activity.h"
#include <string.h>

#define TERMINATED(field) (memchr(f->field, 0, sizeof(f->field)) != NULL)
int js8_activity_build(const Js8ActivityFields *f, const char *text,
                       size_t text_len, Js8Activity *out)
{
    if (!f || !out || (text_len && !text) || f->tx_flags > 7 || f->hard_errors < 0)
        return -1;
    size_t capacity = 1;
    switch (f->kind) {
    case JS8_ACTIVITY_HEARTBEAT:
    case JS8_ACTIVITY_CQ:
        if (!TERMINATED(call) || !TERMINATED(grid) || !TERMINATED(beacon) || f->subtype > 7)
            return -1;
        break;
    case JS8_ACTIVITY_COMPOUND:
        if (!TERMINATED(call) || !TERMINATED(grid) || f->bits3 > 7 || f->compound_directed > 1)
            return -1;
        break;
    case JS8_ACTIVITY_DIRECTED:
        if (!TERMINATED(from) || !TERMINATED(to) || !TERMINATED(command) ||
            f->command_code > 31 || f->has_number > 1 || f->free_text > 1 ||
            f->ack > 1 || f->end73 > 1) return -1;
        break;
    case JS8_ACTIVITY_DATA:
        if (f->codec == JS8_ACTIVITY_CODEC_HUFFMAN) capacity = JS8_HUFF_TEXT_MAX;
        else if (f->codec == JS8_ACTIVITY_CODEC_JSC || f->codec == JS8_ACTIVITY_CODEC_NONE)
            capacity = JS8_JSC_TEXT_SIZE;
        else return -1;
        break;
    case JS8_ACTIVITY_MESSAGE:
        if (!TERMINATED(from) || !TERMINATED(to) || f->first_slot > f->last_slot ||
            f->last_slot != f->slot_index) return -1;
        capacity = JS8_RX_MESSAGE_SIZE;
        break;
    default: return -1;
    }
    if (text_len >= capacity) return -1;
    memset(out, 0, sizeof(*out));
    out->fields = *f;
    out->elapsed_seconds = (uint64_t)f->slot_index * 15;
    out->text_len = (uint16_t)text_len;
    if (text_len) memcpy(out->text, text, text_len);
    return 0;
}
