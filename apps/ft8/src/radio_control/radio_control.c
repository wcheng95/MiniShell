#include "radio_control.h"
#include "radio_qmx.h"
#include <stddef.h>

#define FIELD_END(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))

mini_result_t radio_control_close(RadioControl *radio)
{
    if (!radio) return MINI_ERR_INVALID;
    mini_result_t result = MINI_OK;
    if (radio->stream) result = radio->serial->close(radio->stream);
    radio->stream = MINI_SERIAL_INVALID;
    radio->serial = NULL;
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
    result = radio_qmx_sync(serial, stream, dial_hz);
    if (result != MINI_OK) (void)radio_control_close(radio);
    return result;
}
