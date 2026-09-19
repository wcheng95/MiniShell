#include "app_rx_order.h"

static unsigned group(const RxMessage *message)
{
    return message->is_to_me ? 0u : message->is_cq ? 1u : 2u;
}

static bool precedes(const RxMessage *messages, size_t a, size_t b)
{
    unsigned ga = group(&messages[a]), gb = group(&messages[b]);
    if (ga != gb) return ga < gb;
    if (messages[a].snr_db != messages[b].snr_db)
        return messages[a].snr_db > messages[b].snr_db;
    return a < b;
}

size_t app_rx_order_build(const RxMessage *messages, size_t count,
                          size_t *order, size_t capacity)
{
    if (!messages || !order) return 0;
    if (count > capacity) count = capacity;
    for (size_t i = 0; i < count; ++i) {
        size_t j = i;
        while (j > 0 && precedes(messages, i, order[j - 1])) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = i;
    }
    return count;
}
