#include "radio_control.h"
#include "radio_qmx.h"
#include <stddef.h>

#define FIELD_END(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))

mini_result_t radio_control_close(RadioControl *radio)
{
    if (!radio) return MINI_ERR_INVALID;
    mini_result_t result = MINI_OK;
    if (radio->stream) {
        if (radio->rx_required) result = radio_control_end_tx(radio);
        mini_result_t closed = radio->serial->close(radio->stream);
        if (result == MINI_OK) result = closed;
    }
    radio->stream = MINI_SERIAL_INVALID;
    radio->serial = NULL;
    radio->tx_active = radio->rx_required = false;
    return result;
}

mini_result_t radio_control_open_qmx(RadioControl *radio, const mini_api_t *api,
                                     const char *endpoint, uint32_t dial_hz)
{
    if (!radio || !endpoint || !*endpoint || !dial_hz) return MINI_ERR_INVALID;
    if (radio->stream) return MINI_ERR_TOO_MANY_OPEN;
    if (!api || api->struct_size < FIELD_END(mini_api_t, serial) || !api->serial)
        return MINI_ERR_UNSUPPORTED;
    const mini_serial_api_t *serial = api->serial;
    if (serial->struct_size < FIELD_END(mini_serial_api_t, close) ||
        !(serial->capabilities & MINI_SERIAL_CAP_WRITE) ||
        !serial->open || !serial->write || !serial->close) return MINI_ERR_UNSUPPORTED;
    mini_serial_t stream = MINI_SERIAL_INVALID;
    mini_result_t result = serial->open(endpoint, &stream);
    if (result != MINI_OK) return result;
    if (!stream) return MINI_ERR_IO;
    radio->serial = serial;
    radio->stream = stream;
    radio->tx_active = radio->rx_required = false;
    result = radio_qmx_sync(serial, stream, dial_hz);
    if (result != MINI_OK) (void)radio_control_close(radio);
    return result;
}

mini_result_t radio_control_begin_tx(RadioControl *radio)
{
    if (!radio || !radio->stream) return MINI_ERR_BAD_HANDLE;
    if (radio->tx_active || radio->rx_required) return MINI_ERR_NOT_READY;
    mini_result_t result = radio_qmx_begin_tx(radio->serial, radio->stream, &radio->rx_required);
    radio->tx_active = result == MINI_OK;
    return result;
}

mini_result_t radio_control_set_tone_hz(RadioControl *radio, float tone_hz)
{
    if (!radio || !radio->stream) return MINI_ERR_BAD_HANDLE;
    if (!radio->tx_active) return MINI_ERR_NOT_READY;
    return radio_qmx_set_tone_hz(radio->serial, radio->stream, tone_hz);
}

mini_result_t radio_control_end_tx(RadioControl *radio)
{
    if (!radio || !radio->stream) return MINI_ERR_BAD_HANDLE;
    if (!radio->rx_required) return MINI_OK;
    mini_result_t result = radio_qmx_end_tx(radio->serial, radio->stream);
    if (result == MINI_OK) radio->tx_active = radio->rx_required = false;
    return result;
}
