#ifndef FT8_RADIO_QMX_H
#define FT8_RADIO_QMX_H
#include "minishell/api.h"
#include <stdbool.h>

mini_result_t radio_qmx_sync(const mini_serial_api_t *serial, mini_serial_t stream,
                             uint32_t dial_hz);
mini_result_t radio_qmx_begin_tx(const mini_serial_api_t *serial, mini_serial_t stream,
                                 bool *out_tx_attempted);
mini_result_t radio_qmx_set_tone_hz(const mini_serial_api_t *serial, mini_serial_t stream,
                                    float tone_hz);
mini_result_t radio_qmx_end_tx(const mini_serial_api_t *serial, mini_serial_t stream);
#endif
