#include "radio_qmx.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static mini_result_t send_command(const mini_serial_api_t *serial, mini_serial_t stream,
                                   const char *command, uint32_t timeout_ms)
{
    if (!serial || !serial->write || !stream) return MINI_ERR_INVALID;
    uint32_t written = 0, size = (uint32_t)strlen(command);
    mini_result_t result = serial->write(stream, command, size, &written, timeout_ms);
    if (result != MINI_OK) return result;
    return written == size ? MINI_OK : MINI_ERR_IO;
}

mini_result_t radio_qmx_sync(const mini_serial_api_t *serial, mini_serial_t stream,
                             uint32_t dial_hz)
{
    if (!serial || !serial->write || !stream || !dial_hz) return MINI_ERR_INVALID;
    char frequency[16];
    int length = snprintf(frequency, sizeof(frequency), "FA%011lu;", (unsigned long)dial_hz);
    if (length != 14) return MINI_ERR_INVALID;
    const char *commands[] = {"MD6;", "FR0;", "FT0;", frequency};
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        mini_result_t result = send_command(serial, stream, commands[i], 200u);
        if (result != MINI_OK) return result;
    }
    return MINI_OK;
}

mini_result_t radio_qmx_begin_tx(const mini_serial_api_t *serial, mini_serial_t stream,
                                 bool *out_tx_attempted)
{
    if (!out_tx_attempted) return MINI_ERR_INVALID;
    *out_tx_attempted = false;
    mini_result_t result = send_command(serial, stream, "MD6;", 200u);
    if (result != MINI_OK) return result;
    *out_tx_attempted = true;
    return send_command(serial, stream, "TX;", 200u);
}

mini_result_t radio_qmx_set_tone_hz(const mini_serial_api_t *serial, mini_serial_t stream,
                                    float tone_hz)
{
    if (!isfinite(tone_hz)) return MINI_ERR_INVALID;
    /* Preserve V2's floor/round/clamp order without out-of-range float-to-int casts. */
    int integer = (int)fminf(9999.0f, fmaxf(0.0f, floorf(tone_hz)));
    float hundredths = (tone_hz - (float)integer) * 100.0f;
    int fraction = (int)lrintf(fminf(100.0f, fmaxf(0.0f, hundredths)));
    if (fraction > 99) fraction = 99;
    char command[32];
    int length = snprintf(command, sizeof(command), "TA%04d.%02d;", integer, fraction);
    if (length != 10) return MINI_ERR_INVALID;
    return send_command(serial, stream, command, 10u);
}

mini_result_t radio_qmx_end_tx(const mini_serial_api_t *serial, mini_serial_t stream)
{
    return send_command(serial, stream, "RX;", 200u);
}
