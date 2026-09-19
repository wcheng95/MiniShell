#ifndef FT8_APP_RX_ORDER_H
#define FT8_APP_RX_ORDER_H

#include "rx_result_builder.h"

/* Pure display-index -> factual-index projection. Returns the bounded count.
 * Input is never modified; output storage must not overlap the messages. */
size_t app_rx_order_build(const RxMessage *messages, size_t count,
                          size_t *order, size_t capacity);

#endif
