#ifndef FT8_RADIO_QMX_H
#define FT8_RADIO_QMX_H
#include "minishell/api.h"

mini_result_t radio_qmx_sync(const mini_serial_api_t *serial, mini_serial_t stream,
                             uint32_t dial_hz);
#endif
