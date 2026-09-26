#include "radio_control.h"
#include "app_controller.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
static char transcript[1024];
static unsigned writes, closes, fail_at, sleeps;
static bool short_write;
static mini_result_t sleep_result, file_result = MINI_ERR_NOT_FOUND;

static mini_result_t open_serial(const char *endpoint, mini_serial_t *out)
{ CHECK(strcmp(endpoint, "test:cat") == 0); *out = 17; return MINI_OK; }
static mini_result_t close_serial(mini_serial_t stream)
{ CHECK(stream == 17); ++closes; strcat(transcript, "<close>"); return MINI_OK; }
static mini_result_t write_serial(mini_serial_t stream, const void *bytes, uint32_t size,
                                   uint32_t *out, uint32_t timeout)
{
    CHECK(stream == 17 && timeout == (memcmp(bytes, "TA", 2) == 0 ? 10u : 200u));
    CHECK(strlen(transcript) + size + 8 < sizeof(transcript));
    strncat(transcript, bytes, size);
    ++writes;
    *out = writes == fail_at && short_write ? size - 1 : size;
    return writes == fail_at && !short_write ? MINI_ERR_TIMEOUT : MINI_OK;
}
static mini_result_t sleep_ms(uint32_t duration)
{
    CHECK(duration == 500 && strstr(transcript, "TX;TA1500.00;") && !strstr(transcript, "RX;"));
    ++sleeps;
    return sleep_result;
}
/* The diagnostic may read station state, but must never persist or start audio/UI. */
static mini_result_t open_file(const char *path, uint32_t flags, mini_file_t *out)
{ CHECK(strcmp(path, "/flash/ft8/station.txt") == 0 && flags == MINI_FS_READ); *out = 0; return file_result; }
static mini_result_t unused_file(mini_file_t file)
{ (void)file; abort(); }
static mini_result_t unused_read(mini_file_t file, void *buf, uint32_t size, uint32_t *out)
{ (void)file; (void)buf; (void)size; (void)out; abort(); }
static mini_result_t unused_write(mini_file_t file, const void *buf, uint32_t size, uint32_t *out)
{ (void)file; (void)buf; (void)size; (void)out; abort(); }
static mini_result_t unused_stat(const char *path, mini_fs_stat_t *out)
{ (void)path; (void)out; abort(); }
static mini_result_t unused_rename(const char *from, const char *to)
{ (void)from; (void)to; abort(); }
static mini_result_t unused_path(const char *path)
{ (void)path; abort(); }
static const mini_fs_api_t fs = {.struct_size = sizeof(fs), .open = open_file,
    .close = unused_file, .read = unused_read, .write = unused_write, .sync = unused_file,
    .stat = unused_stat, .rename = unused_rename, .remove_file = unused_path, .mkdir = unused_path};
static const mini_serial_api_t serial = {.struct_size = sizeof(serial), .capabilities = MINI_SERIAL_CAP_WRITE,
    .open = open_serial, .write = write_serial, .close = close_serial};
static const mini_time_location_api_t clock_api = {.struct_size = sizeof(clock_api), .sleep_ms = sleep_ms};
static const mini_api_t api = {.struct_size = sizeof(api), .serial = &serial, .fs = &fs,
    .time_location = &clock_api};

static void reset(void)
{ transcript[0] = 0; writes = closes = fail_at = sleeps = 0; short_write = false; sleep_result = MINI_OK; }
static void open_radio(RadioControl *radio)
{
    reset();
    CHECK(radio_control_open_qmx(radio, &api, "test:cat", 14074000) == MINI_OK);
    CHECK(strcmp(transcript, "MD6;FR0;FT0;FA00014074000;") == 0);
    reset();
}
static void check_tone(RadioControl *radio, float tone, const char *expected)
{
    transcript[0] = 0;
    CHECK(radio_control_set_tone_hz(radio, tone) == MINI_OK);
    CHECK(strcmp(transcript, expected) == 0 && strlen(transcript) == 10);
    CHECK(transcript[6] == '.' && transcript[9] == ';');
    for (unsigned i = 2; i < 9; ++i)
        if (i != 6) CHECK(transcript[i] >= '0' && transcript[i] <= '9');
}

int main(void)
{
    RadioControl radio = {0};
    CHECK(radio_control_begin_tx(&radio) == MINI_ERR_BAD_HANDLE);
    CHECK(radio_control_set_tone_hz(&radio, 1500) == MINI_ERR_BAD_HANDLE);
    CHECK(radio_control_end_tx(&radio) == MINI_ERR_BAD_HANDLE);
    open_radio(&radio);
    CHECK(radio_control_set_tone_hz(&radio, 1500) == MINI_ERR_NOT_READY);
    CHECK(radio_control_end_tx(&radio) == MINI_OK && !writes);
    CHECK(radio_control_begin_tx(&radio) == MINI_OK && radio.tx_active && radio.rx_required);
    CHECK(strcmp(transcript, "TX;") == 0);
    CHECK(radio_control_begin_tx(&radio) == MINI_ERR_NOT_READY && writes == 1);
    check_tone(&radio, 300, "TA0300.00;");
    check_tone(&radio, 1500, "TA1500.00;");
    check_tone(&radio, 1520.8333f, "TA1520.83;");
    check_tone(&radio, 1543.75f, "TA1543.75;");
    check_tone(&radio, 2700, "TA2700.00;");
    const unsigned bases[] = {300, 1500, 2700};
    for (unsigned b = 0; b < 3; ++b) {
        for (unsigned tone = 0; tone < 8; ++tone) {
            unsigned hundredths = bases[b] * 100 + tone * 625;
            char expected[32];
            snprintf(expected, sizeof(expected), "TA%04u.%02u;", hundredths / 100, hundredths % 100);
            check_tone(&radio, (float)bases[b] + (float)tone * 6.25f, expected);
        }
    }
    check_tone(&radio, 1234.9999f, "TA1234.99;");
    check_tone(&radio, -1.25f, "TA0000.00;");
    check_tone(&radio, 10000.25f, "TA9999.99;");
    check_tone(&radio, FLT_MAX, "TA9999.99;");
    check_tone(&radio, -FLT_MAX, "TA0000.00;");
    unsigned before = writes;
    CHECK(radio_control_set_tone_hz(&radio, NAN) == MINI_ERR_INVALID);
    CHECK(radio_control_set_tone_hz(&radio, INFINITY) == MINI_ERR_INVALID);
    CHECK(writes == before && radio.tx_active);
    transcript[0] = 0;
    CHECK(radio_control_end_tx(&radio) == MINI_OK && !radio.tx_active && !radio.rx_required);
    CHECK(radio_control_end_tx(&radio) == MINI_OK);
    CHECK(radio_control_close(&radio) == MINI_OK);
    CHECK(strcmp(transcript, "RX;<close>") == 0 && closes == 1);

    for (unsigned mode = 0; mode < 2; ++mode) {
        mini_result_t error = mode ? MINI_ERR_IO : MINI_ERR_TIMEOUT;
        for (unsigned command = 1; command <= 3; ++command) {
            open_radio(&radio);
            short_write = mode != 0;
            fail_at = command;
            mini_result_t begun = radio_control_begin_tx(&radio);
            CHECK(begun == (command <= 1 ? error : MINI_OK));
            CHECK(radio.tx_active == (command > 1));
            CHECK(radio.rx_required);
            if (command == 1) {
                before = writes;
                CHECK(radio_control_begin_tx(&radio) == MINI_ERR_NOT_READY);
                CHECK(radio_control_set_tone_hz(&radio, 1500) == MINI_ERR_NOT_READY);
                CHECK(writes == before);
            }
            if (command >= 2) {
                CHECK(radio_control_set_tone_hz(&radio, 1500) == (command == 2 ? error : MINI_OK));
                CHECK(radio.tx_active && radio.rx_required);
            }
            if (command == 3) {
                CHECK(radio_control_end_tx(&radio) == error);
                CHECK(radio.tx_active && radio.rx_required);
            }
            CHECK(radio_control_close(&radio) == MINI_OK && closes == 1);
            CHECK(!radio.stream && !radio.tx_active && !radio.rx_required);
            const char *expected[] = {"TX;RX;<close>",
                "TX;TA1500.00;RX;<close>", "TX;TA1500.00;RX;RX;<close>"};
            CHECK(strcmp(transcript, expected[command - 1]) == 0);
        }
        open_radio(&radio);
        CHECK(radio_control_begin_tx(&radio) == MINI_OK);
        short_write = mode != 0; fail_at = 2;
        CHECK(radio_control_close(&radio) == error && closes == 1 && !radio.stream);
        CHECK(strcmp(transcript, "TX;RX;<close>") == 0);
        CHECK(radio_control_close(&radio) == MINI_OK && closes == 1);
    }

    /* Real controller diagnostic, mocked only below MiniShell; all other services absent. */
    for (unsigned mode = 0; mode < 2; ++mode) {
        for (unsigned command = 0; command <= 7; ++command) {
            reset(); fail_at = command; short_write = mode != 0;
            mini_result_t result = app_controller_cat_test(&api, "/flash/ft8/station.txt", "test:cat", 1500, 500);
            CHECK(result == (command ? (mode ? MINI_ERR_IO : MINI_ERR_TIMEOUT) : MINI_OK));
            CHECK(closes == 1 && sleeps == (command == 0 || command == 7 ? 1u : 0u));
            if (command >= 5 || command == 0) CHECK(strstr(transcript, "RX;<close>"));
            else CHECK(!strstr(transcript, "TX;"));
            if (!command) CHECK(strcmp(transcript, "MD6;FR0;FT0;FA00014074000;TX;TA1500.00;RX;<close>") == 0);
        }
    }
    reset(); sleep_result = MINI_ERR_IO;
    CHECK(app_controller_cat_test(&api, "/flash/ft8/station.txt", "test:cat", 1500, 500) == MINI_ERR_IO);
    CHECK(sleeps == 1 && closes == 1 && strstr(transcript, "TA1500.00;RX;<close>"));
    reset(); file_result = MINI_ERR_ACCESS;
    CHECK(app_controller_cat_test(&api, "/flash/ft8/station.txt", "test:cat", 1500, 500) == MINI_ERR_IO);
    CHECK(!writes && !closes && !sleeps);
    mini_api_t no_time = api; no_time.time_location = NULL;
    CHECK(app_controller_cat_test(&no_time, "/flash/ft8/station.txt", "test:cat", 1500, 500) == MINI_ERR_UNSUPPORTED);
    CHECK(!writes);
    puts("QMX TX bytes, V2 tone formatting, short/error writes, state, diagnostic and RX cleanup PASS");
    return 0;
}
