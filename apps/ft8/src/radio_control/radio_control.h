#ifndef FT8_RADIO_CONTROL_H
#define FT8_RADIO_CONTROL_H
#include "minishell/api.h"
#include <stdbool.h>

/* Zero-initialized, control-only state. Audio ownership stays with its adapter. */
typedef struct {
    const mini_serial_api_t *serial;
    mini_serial_t stream;
    bool tx_active;
    /* A failed/short TX write may still have keyed the device. */
    bool rx_required;
} RadioControl;

mini_result_t radio_control_open_qmx(RadioControl *radio, const mini_api_t *api,
                                     const char *endpoint, uint32_t dial_hz);
mini_result_t radio_control_sync_frequency(RadioControl *radio, uint32_t dial_hz);
mini_result_t radio_control_close(RadioControl *radio);
mini_result_t radio_control_begin_tx(RadioControl *radio);
mini_result_t radio_control_set_tone_hz(RadioControl *radio, float tone_hz);
mini_result_t radio_control_end_tx(RadioControl *radio);
#endif
