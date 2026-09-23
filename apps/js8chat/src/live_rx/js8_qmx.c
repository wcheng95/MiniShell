#include "js8_live.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
mini_result_t js8_qmx_open(const mini_serial_api_t *serial, const char *endpoint,
                           uint32_t dial, mini_serial_t *out)
{
    if (!out) return MINI_ERR_INVALID;
    *out = MINI_SERIAL_INVALID;
    if (!serial || serial->struct_size < offsetof(mini_serial_api_t, close)+sizeof(serial->close) ||
        !(serial->capabilities & MINI_SERIAL_CAP_WRITE) || !serial->open || !serial->write || !serial->close || !dial) return MINI_ERR_UNSUPPORTED;
    mini_result_t result = serial->open(endpoint, out);
    if (result || !*out) return result ? result : MINI_ERR_IO;
    char frequency[16]; snprintf(frequency, sizeof(frequency), "FA%011u;", dial);
    const char *commands[] = {"MD6;", "FR0;", "FT0;", frequency};
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t written = 0, length = (uint32_t)strlen(commands[i]);
        result = serial->write(*out, commands[i], length, &written, 200);
        if (result || written != length) {
            (void)serial->close(*out); *out = MINI_SERIAL_INVALID;
            return result ? result : MINI_ERR_IO;
        }
    }
    return MINI_OK;
}
