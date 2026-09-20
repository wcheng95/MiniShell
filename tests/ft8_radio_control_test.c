#include "radio_control.h"
#include "config_service.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
static char transcript[128];
static unsigned opens, closes, writes, fail_at;
static bool short_write;
static mini_result_t open_result, write_result;
static mini_result_t open_serial(const char *endpoint, mini_serial_t *out)
{ CHECK(strcmp(endpoint, "test:cat") == 0); ++opens; *out = 17; return open_result; }
static mini_result_t close_serial(mini_serial_t stream)
{ CHECK(stream == 17); ++closes; return MINI_OK; }
static mini_result_t write_serial(mini_serial_t stream, const void *bytes, uint32_t size,
                                   uint32_t *out, uint32_t timeout)
{
    CHECK(stream == 17 && timeout == 200);
    CHECK(strlen(transcript) + size < sizeof(transcript));
    strncat(transcript, bytes, size);
    ++writes;
    *out = short_write && writes == fail_at ? size - 1 : size;
    return writes == fail_at ? write_result : MINI_OK;
}
int main(void)
{
    mini_serial_api_t serial = {.struct_size = sizeof(serial), .capabilities = MINI_SERIAL_CAP_WRITE,
        .open = open_serial, .write = write_serial, .close = close_serial};
    mini_api_t api = {.struct_size = sizeof(api), .serial = &serial};
    RadioControl radio = {0};
    const char *expected[] = {"FA00003573000;", "FA00007074000;", "FA00010136000;",
        "FA00014074000;", "FA00018100000;", "FA00021074000;", "FA00028074000;"};
    CHECK(config_service_band_dial_hz(-1) == 0 && config_service_band_dial_hz(7) == 0);
    for (int band = 0; band < 7; ++band) {
        transcript[0] = 0; writes = 0;
        CHECK(radio_control_open_qmx(&radio, &api, "test:cat", config_service_band_dial_hz(band)) == MINI_OK);
        char sequence[64]; snprintf(sequence, sizeof(sequence), "MD6;FR0;FT0;%s", expected[band]);
        CHECK(strcmp(transcript, sequence) == 0 && writes == 4);
        CHECK(!strstr(transcript, "TX;") && !strstr(transcript, "RX;") &&
              !strstr(transcript, "TA") && !strstr(transcript, "TM"));
        CHECK(radio_control_open_qmx(&radio, &api, "test:cat", 14074000) == MINI_ERR_TOO_MANY_OPEN);
        CHECK(radio_control_close(&radio) == MINI_OK);
        CHECK(radio_control_close(&radio) == MINI_OK);
    }
    CHECK(opens == 7 && closes == 7);
    for (fail_at = 1; fail_at <= 4; ++fail_at) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            writes = 0; transcript[0] = 0;
            short_write = mode == 0;
            write_result = mode == 0 ? MINI_OK : MINI_ERR_TIMEOUT;
            unsigned before = closes;
            CHECK(radio_control_open_qmx(&radio, &api, "test:cat", 14074000) ==
                  (mode == 0 ? MINI_ERR_IO : MINI_ERR_TIMEOUT));
            CHECK(writes == fail_at && closes == before + 1 && !radio.stream);
        }
    }
    fail_at = 0; short_write = false; transcript[0] = 0; writes = 0;
    CHECK(radio_control_sync_frequency(NULL, 18100000) == MINI_ERR_INVALID);
    CHECK(radio_control_sync_frequency(&radio, 18100000) == MINI_ERR_INVALID);
    CHECK(radio_control_open_qmx(&radio, &api, "test:cat", 14074000) == MINI_OK);
    unsigned opened = opens, closed = closes;
    transcript[0] = 0; writes = 0;
    CHECK(radio_control_sync_frequency(&radio, 18100000) == MINI_OK);
    CHECK(strcmp(transcript, "MD6;FR0;FT0;FA00018100000;") == 0 && writes == 4);
    CHECK(opens == opened && closes == closed);
    CHECK(radio_control_sync_frequency(&radio, 0) == MINI_ERR_INVALID);
    radio.tx_active = true;
    CHECK(radio_control_sync_frequency(&radio, 18100000) == MINI_ERR_NOT_READY);
    radio.tx_active = false; radio.rx_required = true;
    CHECK(radio_control_sync_frequency(&radio, 18100000) == MINI_ERR_NOT_READY);
    CHECK(writes == 4);
    radio.rx_required = false;
    for (fail_at = 1; fail_at <= 4; ++fail_at) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            writes = 0; transcript[0] = 0;
            short_write = mode == 0;
            write_result = mode == 0 ? MINI_OK : MINI_ERR_TIMEOUT;
            CHECK(radio_control_sync_frequency(&radio, 18100000) ==
                  (mode == 0 ? MINI_ERR_IO : MINI_ERR_TIMEOUT));
            CHECK(writes == fail_at && radio.stream == 17);
            CHECK(opens == opened && closes == closed);
        }
    }
    CHECK(radio_control_close(&radio) == MINI_OK);
    writes = 0; open_result = MINI_ERR_ACCESS;
    unsigned before = closes;
    CHECK(radio_control_open_qmx(&radio, &api, "test:cat", 14074000) == MINI_ERR_ACCESS);
    CHECK(writes == 0 && closes == before && !radio.stream);
    /* An older/short API table must be rejected without reading its tail. */
    api.struct_size = offsetof(mini_api_t, serial);
    CHECK(radio_control_open_qmx(&radio, &api, "test:cat", 14074000) == MINI_ERR_UNSUPPORTED);
    api.struct_size = sizeof(api); api.serial = NULL;
    CHECK(radio_control_open_qmx(&radio, &api, "test:cat", 14074000) == MINI_ERR_UNSUPPORTED);
    api.serial = &serial; serial.struct_size = offsetof(mini_serial_api_t, close);
    CHECK(radio_control_open_qmx(&radio, &api, "test:cat", 14074000) == MINI_ERR_UNSUPPORTED);
    puts("QMX receive-only startup sequence, all bands, short/error writes and cleanup PASS");
    return 0;
}
