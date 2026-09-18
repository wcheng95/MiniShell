#include "radio_qmx.h"
#include <stdio.h>
#include <string.h>

mini_result_t radio_qmx_sync(const mini_serial_api_t *serial, mini_serial_t stream,
                             uint32_t dial_hz)
{
    if (!serial || !serial->write || !stream || !dial_hz) return MINI_ERR_INVALID;
    char frequency[16];
    int length = snprintf(frequency, sizeof(frequency), "FA%011lu;", (unsigned long)dial_hz);
    if (length != 14) return MINI_ERR_INVALID;
    const char *commands[] = {"MD6;", "FR0;", "FT0;", frequency};
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        uint32_t written = 0, size = (uint32_t)strlen(commands[i]);
        mini_result_t result = serial->write(stream, commands[i], size, &written, 200u);
        if (result != MINI_OK) return result;
        if (written != size) return MINI_ERR_IO;
    }
    return MINI_OK;
}
