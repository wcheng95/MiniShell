#include "log_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static struct {
    char text[4096];
    size_t size;
    size_t position;
    bool exists;
    bool opened;
    unsigned syncs;
    unsigned closes;
} files[2];
static int fail_open = -1;
static bool fail_sync;
static bool fail_close;

static unsigned path_index(const char *path)
{
    if (strcmp(path, "/flash/ft8/20240102.txt") == 0) return 0;
    CHECK(strcmp(path, "/flash/ft8/fieldday.txt") == 0);
    return 1;
}

static unsigned file_index(mini_file_t file)
{
    CHECK(file >= 1 && file <= 2 && files[file - 1].opened);
    return file - 1;
}

static mini_result_t fake_open(const char *path, uint32_t flags, mini_file_t *out)
{
    unsigned i = path_index(path);
    if ((int)i == fail_open) return MINI_ERR_IO;
    CHECK(!files[i].opened);
    if (i == 0) CHECK(flags == (MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_APPEND));
    else CHECK(flags == (MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC) ||
               flags == (MINI_FS_READ | MINI_FS_WRITE));
    if (!files[i].exists && !(flags & MINI_FS_CREATE)) return MINI_ERR_NOT_FOUND;
    if (flags & MINI_FS_TRUNC) files[i].size = 0;
    files[i].exists = true;
    files[i].opened = true;
    files[i].position = (flags & MINI_FS_APPEND) ? files[i].size : 0;
    *out = i + 1;
    return MINI_OK;
}

static mini_result_t fake_write(mini_file_t file, const void *data, uint32_t size,
                                uint32_t *written)
{
    unsigned i = file_index(file);
    /* Exercise the existing full-write loop with partial progress. */
    if (size > 17) size = 17;
    CHECK(files[i].position + size < sizeof(files[i].text));
    memcpy(files[i].text + files[i].position, data, size);
    files[i].position += size;
    if (files[i].position > files[i].size) files[i].size = files[i].position;
    files[i].text[files[i].size] = '\0';
    *written = size;
    return MINI_OK;
}

static mini_result_t fake_read(mini_file_t file, void *data, uint32_t size,
                               uint32_t *got)
{
    unsigned i = file_index(file);
    size_t available = files[i].size - files[i].position;
    if (size > available) size = (uint32_t)available;
    memcpy(data, files[i].text + files[i].position, size);
    files[i].position += size;
    *got = size;
    return MINI_OK;
}

static mini_result_t fake_seek(mini_file_t file, int64_t offset, uint32_t origin,
                               uint64_t *position)
{
    unsigned i = file_index(file);
    CHECK(origin == MINI_FS_SEEK_SET && offset >= 0 && (uint64_t)offset <= files[i].size);
    files[i].position = (size_t)offset;
    *position = (uint64_t)offset;
    return MINI_OK;
}

static mini_result_t fake_stat(const char *path, mini_fs_stat_t *out)
{
    unsigned i = path_index(path);
    if (!files[i].exists) return MINI_ERR_NOT_FOUND;
    out->size = files[i].size;
    out->type = MINI_FS_TYPE_FILE;
    return MINI_OK;
}

static mini_result_t fake_sync(mini_file_t file)
{
    ++files[file_index(file)].syncs;
    return fail_sync ? MINI_ERR_IO : MINI_OK;
}

static mini_result_t fake_close(mini_file_t file)
{
    unsigned i = file_index(file);
    files[i].opened = false;
    ++files[i].closes;
    return fail_close ? MINI_ERR_IO : MINI_OK;
}

static mini_result_t fake_utc(mini_utc_time_t *utc)
{
    utc->unix_seconds = 1704164645; /* 2024-01-02 03:04:05 UTC */
    utc->nanoseconds = 0;
    return MINI_OK;
}

static const char header[] =
    "START-OF-LOG: 3.0\nCREATED-BY: Mini-FT8\nCONTEST: ARRL-FIELD-DAY\n"
    "CALLSIGN: AG6AQ\nCATEGORY-OPERATOR: SINGLE-OP\nCATEGORY-TRANSMITTER: ONE\n"
    "CATEGORY-ASSISTED: NON-ASSISTED\nCATEGORY-BAND: ALL\nCATEGORY-MODE: MIXED\n"
    "CATEGORY-POWER: LOW\nCATEGORY-STATION: PORTABLE\nLOCATION: SCV\nOPERATORS: AG6AQ\n";

int main(void)
{
    const mini_fs_api_t fs = {
        .struct_size = sizeof(fs), .open = fake_open, .write = fake_write,
        .read = fake_read, .seek = fake_seek, .stat = fake_stat,
        .sync = fake_sync, .close = fake_close
    };
    const mini_time_location_api_t time = {
        .struct_size = sizeof(time), .capabilities = MINI_TIMELOC_CAP_UTC,
        .utc_get = fake_utc
    };
    LogService log;
    /* Persistent/manual CM87 is deliberately absent from the logging interface. */
    LogStationFacts station = {"AG6AQ", "CM97ab", " R 1B SCV", 3};
    LogQsoFacts qso = {"W6ABC", "CM88", " R 2A ORG", -12, 5, true, true};
    CHECK(log_service_init(&log, &fs, &time, "/flash/ft8/station.txt"));
    CHECK(log_service_write_adif(&log, &station, &qso));
    CHECK(strcmp(files[0].text,
        "<call:5>W6ABC <gridsquare:4>CM88 <mode:3>FT8<qso_date:8>20240102 "
        "<time_on:6>030405 <freq:6>14.074 <station_callsign:5>AG6AQ "
        "<my_gridsquare:4>CM97 <rst_sent:3>-12 <rst_rcvd:1>5 <comment:0> <eor>\n") == 0);
    CHECK(files[0].syncs == 1 && files[0].closes == 1);

    memset(files, 0, sizeof(files));
    station.band_index = 1;
    qso.snr_tx = qso.snr_rx = -99;
    qso.snr_tx_known = qso.snr_rx_known = false;
    CHECK(log_service_write_adif(&log, &station, &qso));
    CHECK(strcmp(files[0].text,
        "<call:5>W6ABC <gridsquare:4>CM88 <mode:3>FT8<qso_date:8>20240102 "
        "<time_on:6>030405 <freq:5>7.074 <station_callsign:5>AG6AQ "
        "<my_gridsquare:4>CM97 <comment:0> <eor>\n") == 0);

    station.band_index = 3;
    CHECK(log_service_write_cabrillo(&log, &station, &qso));
    char expected[4096];
    snprintf(expected, sizeof(expected), "%s%s", header,
        "QSO: 14074 DG 2024-01-02 0304 AG6AQ 1B SCV W6ABC 2A ORG\nEND-OF-LOG:\n");
    CHECK(strcmp(files[1].text, expected) == 0);
    CHECK(files[1].syncs == 2 && files[1].closes == 2);
    station.band_index = 1;
    CHECK(log_service_write_cabrillo(&log, &station, &qso));
    snprintf(expected, sizeof(expected), "%s%s", header,
        "QSO: 14074 DG 2024-01-02 0304 AG6AQ 1B SCV W6ABC 2A ORG\n"
        "QSO: 7074 DG 2024-01-02 0304 AG6AQ 1B SCV W6ABC 2A ORG\nEND-OF-LOG:\n");
    CHECK(strcmp(files[1].text, expected) == 0);
    CHECK(files[1].syncs == 3 && files[1].closes == 3);

    fail_open = 0;
    CHECK(!log_service_write_adif(&log, &station, &qso));
    CHECK(log_service_write_cabrillo(&log, &station, &qso));
    fail_open = 1;
    CHECK(log_service_write_adif(&log, &station, &qso));
    CHECK(!log_service_write_cabrillo(&log, &station, &qso));
    fail_open = -1;
    fail_sync = true;
    CHECK(!log_service_write_adif(&log, &station, &qso));
    CHECK(!files[0].opened);
    fail_sync = false;
    fail_close = true;
    CHECK(!log_service_write_cabrillo(&log, &station, &qso));
    CHECK(!files[1].opened);
    puts("ft8_log_service_test: PASS");
    return 0;
}
