#ifndef FT8_RADIO_CONTROL_H
#define FT8_RADIO_CONTROL_H
#include "minishell/api.h"

/* Zero-initialized, control-only state. Audio ownership stays with its adapter. */
typedef struct {
    const mini_serial_api_t *serial;
    mini_serial_t stream;
} RadioControl;

mini_result_t radio_control_open_qmx(RadioControl *radio, const mini_api_t *api,
                                     const char *endpoint, uint32_t dial_hz);
mini_result_t radio_control_close(RadioControl *radio);
#endif
