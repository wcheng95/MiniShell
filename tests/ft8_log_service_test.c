#include "log_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

typedef struct {
    char text[32768];
    size_t size;
    size_t position;
    bool exists;
    bool opened;
    bool synced;
    unsigned syncs;
    unsigned closes;
} FakeFile;
/* final ADIF, final Cabrillo, and their respective temporary paths */
static FakeFile files[4];
static FakeFile committed[2];
static bool guard_final;
static unsigned operations, fail_at, commits;
static int fail_open = -1;
static bool fail_sync, fail_close, fail_remove;
static bool zero_read, zero_write, directory;

static void check_final(void)
{
    if (!guard_final) return;
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(files[i].exists == committed[i].exists);
        CHECK(files[i].size == committed[i].size);
        CHECK(memcmp(files[i].text, committed[i].text, files[i].size) == 0);
    }
}

static bool fault(void)
{
    check_final();
    return ++operations == fail_at;
}

static unsigned path_index(const char *path)
{
    const char *paths[] = {"/flash/ft8/20240102.txt", "/flash/ft8/fieldday.txt",
                          "/flash/ft8/20240102.txt.tmp", "/flash/ft8/fieldday.txt.tmp"};
    for (unsigned i = 0; i < 4; ++i) if (strcmp(path, paths[i]) == 0) return i;
    CHECK(false);
    return 0;
}

static unsigned file_index(mini_file_t file)
{
    CHECK(file >= 1 && file <= 4 && files[file - 1].opened);
    return file - 1;
}

static mini_result_t fake_open(const char *path, uint32_t flags, mini_file_t *out)
{
    unsigned i = path_index(path);
    /* This assertion applies even to failed opens. Final is never writable. */
    CHECK(flags == (i < 2 ? MINI_FS_READ :
                    (MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC)));
    if (fault() || (int)i == fail_open) return MINI_ERR_IO;
    CHECK(!files[i].opened);
    if (!files[i].exists && !(flags & MINI_FS_CREATE)) return MINI_ERR_NOT_FOUND;
    if (flags & MINI_FS_TRUNC) {
        files[i].size = 0;
        files[i].text[0] = '\0';
    }
    files[i].exists = files[i].opened = true;
    files[i].synced = false;
    files[i].position = 0;
    *out = i + 1;
    return MINI_OK;
}

static mini_result_t fake_write(mini_file_t file, const void *data, uint32_t size,
                                uint32_t *written)
{
    unsigned i = file_index(file);
    CHECK(i >= 2);
    *written = 0;
    if (fault()) return MINI_ERR_IO;
    if (zero_write) return MINI_OK;
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
    CHECK(i < 2);
    *got = 0;
    if (fault()) return MINI_ERR_IO;
    if (zero_read) return MINI_OK;
    size_t available = files[i].size - files[i].position;
    if (size > available) size = (uint32_t)available;
    if (size > 7) size = 7;
    memcpy(data, files[i].text + files[i].position, size);
    files[i].position += size;
    *got = size;
    return MINI_OK;
}

static mini_result_t fake_seek(mini_file_t file, int64_t offset, uint32_t origin,
                               uint64_t *position)
{
    unsigned i = file_index(file);
    if (fault()) return MINI_ERR_IO;
    CHECK(origin == MINI_FS_SEEK_SET && offset >= 0 && (uint64_t)offset <= files[i].size);
    files[i].position = (size_t)offset;
    *position = (uint64_t)offset;
    return MINI_OK;
}

static mini_result_t fake_stat(const char *path, mini_fs_stat_t *out)
{
    unsigned i = path_index(path);
    CHECK(i < 2);
    if (fault()) return MINI_ERR_IO;
    if (!files[i].exists) return MINI_ERR_NOT_FOUND;
    out->size = files[i].size;
    out->type = directory ? MINI_FS_TYPE_DIRECTORY : MINI_FS_TYPE_FILE;
    return MINI_OK;
}

static mini_result_t fake_sync(mini_file_t file)
{
    unsigned i = file_index(file);
    CHECK(i >= 2);
    ++files[i].syncs;
    if (fault() || fail_sync) return MINI_ERR_IO;
    files[i].synced = true;
    return MINI_OK;
}

static mini_result_t fake_close(mini_file_t file)
{
    unsigned i = file_index(file);
    files[i].opened = false;
    ++files[i].closes;
    return fault() || fail_close ? MINI_ERR_IO : MINI_OK;
}

static mini_result_t fake_rename(const char *old_path, const char *new_path)
{
    unsigned source = path_index(old_path), dest = path_index(new_path);
    CHECK(source == dest + 2);
    CHECK(files[source].exists && files[source].synced);
    for (unsigned i = 0; i < 4; ++i) CHECK(!files[i].opened);
    if (fault()) return MINI_ERR_IO;
    files[dest] = files[source];
    files[source].exists = false;
    ++commits;
    if (guard_final) committed[dest] = files[dest];
    return MINI_OK;
}

static mini_result_t fake_remove(const char *path)
{
    unsigned i = path_index(path);
    CHECK(i >= 2 && !files[i].opened);
    check_final();
    if (fail_remove) return MINI_ERR_IO;
    files[i].exists = false;
    return MINI_OK;
}

static void reset(void)
{
    memset(files, 0, sizeof(files));
    guard_final = false;
    operations = fail_at = commits = 0;
    fail_open = -1;
    fail_sync = fail_close = fail_remove = false;
    zero_read = zero_write = directory = false;
}

static void seed(unsigned i, const char *text)
{
    CHECK(strlen(text) < sizeof(files[i].text));
    strcpy(files[i].text, text);
    files[i].size = strlen(text);
    files[i].exists = true;
}

static void guard(void)
{
    for (unsigned i = 0; i < 2; ++i) committed[i] = files[i];
    guard_final = true;
}

static void check_closed(void)
{
    for (unsigned i = 0; i < 4; ++i) CHECK(!files[i].opened);
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

typedef bool (*Writer)(const LogService *, const LogStationFacts *, const LogQsoFacts *);

static void test_persistence(const LogService *log, const LogStationFacts *station,
                              const LogQsoFacts *qso)
{
    const Writer writers[] = {log_service_write_adif, log_service_write_cabrillo};
    const char *initial[] = {"previous ADIF bytes\n", "existing header\nEND-OF-LOG:\n"};
    for (unsigned format = 0; format < 2; ++format) {
        for (unsigned existing = 0; existing < 2; ++existing) {
            reset();
            if (existing) seed(format, initial[format]);
            guard();
            CHECK(writers[format](log, station, qso));
            CHECK(commits == 1);
            FakeFile expected = files[format];
            unsigned steps = operations;
            /* Fail every operation in a successful trace, including every partial
             * read/write, both closes, marker seeks, sync and commit rename. */
            for (unsigned step = 1; step <= steps; ++step) {
                reset();
                if (existing) seed(format, initial[format]);
                seed(format + 2, "stale temporary content which must never reach final");
                guard();
                fail_at = step;
                fail_remove = true; /* Retry must also tolerate cleanup failure. */
                CHECK(!writers[format](log, station, qso));
                CHECK(operations >= step && commits == 0);
                check_final();
                check_closed();
                fail_at = 0;
                CHECK(writers[format](log, station, qso));
                CHECK(commits == 1);
                CHECK(files[format].size == expected.size);
                CHECK(memcmp(files[format].text, expected.text, expected.size) == 0);
                CHECK(!files[format + 2].exists);
                check_closed();
            }
            printf("format %u existing %u: %u failure/retry points passed\n",
                   format, existing, steps);
        }
        for (unsigned mode = 0; mode < 3; ++mode) {
            reset();
            seed(format, initial[format]);
            guard();
            zero_read = mode == 0;
            zero_write = mode == 1;
            directory = mode == 2;
            CHECK(!writers[format](log, station, qso));
            CHECK(commits == 0);
            check_final();
            check_closed();
        }
        /* Exercise more than one copy buffer, including embedded NUL bytes. */
        reset();
        for (unsigned i = 0; i < 1600; ++i) files[format].text[i] = (char)(i % 127);
        files[format].size = 1600;
        files[format].exists = true;
        if (format == 1) {
            memcpy(files[format].text + 1600, "END-OF-LOG:\n", 12);
            files[format].size += 12;
        }
        guard();
        CHECK(writers[format](log, station, qso));
        for (unsigned i = 0; i < 1600; ++i) CHECK(files[format].text[i] == (char)(i % 127));
        check_closed();
    }
    const char *malformed[] = {"", "END", "header\nEND-OF-LOG:",
                               "header\nEND-OF-LOG:\ntrailing", "header\nBAD-OF-LOG:\n"};
    for (unsigned i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
        reset();
        seed(1, malformed[i]);
        guard();
        CHECK(!log_service_write_cabrillo(log, station, qso));
        CHECK(commits == 0);
        check_final();
        check_closed();
    }
}

static void test_qso_pages(const LogService *log)
{
    LogQsoPage page;
    reset(); log_service_read_qso_page(log, 0, &page);
    CHECK(page.status == LOG_QSO_VIEW_OK && !page.total_count && page.page_count == 1);
    const char *one = "<call:5>W6ABC<qso_date:8>20240102<time_on:6>030405<freq:6>14.074<eor>\n";
    seed(0, one); unsigned before = files[0].closes;
    log_service_read_qso_page(log, 0, &page);
    CHECK(page.status == LOG_QSO_VIEW_OK && page.total_count == 1 && page.row_count == 1);
    CHECK(page.rows[0].hour == 3 && page.rows[0].minute == 4);
    CHECK(strcmp(page.rows[0].band, "20m") == 0 && strcmp(page.rows[0].call, "W6ABC") == 0);
    CHECK(files[0].closes == before + 1); check_closed();
    CHECK(strcmp(files[0].text, one) == 0); // Reading never rewrites the log.

    const unsigned sizes[] = {6, 7, 13, 200};
    for (size_t n = 0; n < sizeof(sizes)/sizeof(sizes[0]); ++n) {
        reset(); files[0].exists = true;
        for (unsigned i = 0; i < sizes[n]; ++i) {
            int count = snprintf(files[0].text + files[0].size, sizeof(files[0].text) - files[0].size,
                "<call:6>W%03uAA<qso_date:8>20240102<time_on:6>%02u%02u00<freq:5>7.074<eor>\n",
                i, i/60, i%60);
            CHECK(count > 0); files[0].size += (size_t)count;
        }
        unsigned pages = (sizes[n] + 5)/6;
        for (unsigned index = 0; index <= pages; ++index) {
            log_service_read_qso_page(log, index, &page);
            unsigned actual = index < pages ? index : pages - 1;
            CHECK(page.status == LOG_QSO_VIEW_OK && page.total_count == sizes[n]);
            CHECK(page.page_count == pages && page.page_index == actual);
            unsigned rows = sizes[n] - actual * 6; if (rows > 6) rows = 6;
            CHECK(page.row_count == rows);
            for (unsigned i = 0; i < rows; ++i) {
                char call[16]; snprintf(call, sizeof(call), "W%03uAA", actual*6+i);
                CHECK(strcmp(page.rows[i].call, call) == 0 && strcmp(page.rows[i].band, "40m") == 0);
            }
            check_closed();
        }
        if (sizes[n] == 200) CHECK(files[0].size > 8192);
    }

    const char *bad[] = {
        "<call:999999999999999999999>W6ABC<eor>", "<call:5>W6ABC",
        "<call:5>W6ABC<qso_date:8>20240102<time_on:6>240405<freq:6>14.074<eor>",
        "<call:5>W6ABC<qso_date:8>20240101<time_on:6>030405<freq:6>14.074<eor>",
        "<call:5>W6ABC<qso_date:8>20240102<time_on:6>030405<freq:6>99.999<eor>",
        "<call:5>W6ABC<call:5>W6ABC<qso_date:8>20240102<time_on:6>030405<freq:6>14.074<eor>",
        "<call:5>W6ABC<qso_date:8>20240102<time_on:6>030405<freq:6>14.074",
        "<", "<x:", "<x:9>xx"
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        reset(); seed(0, bad[i]);
        log_service_read_qso_page(log, 0, &page);
        CHECK(page.status == LOG_QSO_VIEW_OK && page.total_count == 0);
        strcat(files[0].text, "\n"); strcat(files[0].text, one); files[0].size = strlen(files[0].text);
        log_service_read_qso_page(log, 0, &page);
        CHECK(page.total_count == 1 && page.rows[0].hour == 3);
    }
    reset(); memset(files[0].text, 'X', 700); strcpy(files[0].text + 700, "\n");
    strcat(files[0].text, one); files[0].size = strlen(files[0].text); files[0].exists = true;
    log_service_read_qso_page(log, 0, &page); CHECK(page.total_count == 1);
    reset(); seed(0, "<CALL:5>W6ABC<comment:8><call:1><QSO_DATE:8>20240102<TIME_ON:6>030405<FREQ:6>14.074<EOR>");
    log_service_read_qso_page(log, 0, &page); CHECK(page.total_count == 1); // EOF without newline.
    reset(); seed(0, one); files[0].text[10] = 0;
    log_service_read_qso_page(log, 0, &page); CHECK(page.total_count == 0);

    reset(); seed(0, one); log_service_read_qso_page(log, 0, &page);
    unsigned count = operations;
    for (unsigned failure = 1; failure <= count; ++failure) {
        reset(); seed(0, one); fail_at = failure;
        log_service_read_qso_page(log, 0, &page);
        CHECK(page.status == LOG_QSO_VIEW_READ_ERROR && !page.row_count && page.page_count == 1);
        CHECK(files[0].closes == (failure == 1 ? 0u : 1u)); check_closed();
    }
    reset(); LogService unavailable = *log; unavailable.time_location = NULL;
    log_service_read_qso_page(&unavailable, 0, &page);
    CHECK(page.status == LOG_QSO_VIEW_UTC_UNAVAILABLE && !operations);
    puts("daily QSO streaming, partial reads, pagination, malformed records and faults PASS");
}

int main(void)
{
    const mini_fs_api_t fs = {
        .struct_size = sizeof(fs), .open = fake_open, .write = fake_write,
        .read = fake_read, .seek = fake_seek, .stat = fake_stat,
        .sync = fake_sync, .close = fake_close,
        .rename = fake_rename, .remove_file = fake_remove
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
    const char *mhz[] = {"3.573", "7.074", "10.136", "14.074", "18.100", "21.074", "28.074"};
    const char *khz[] = {"3573", "7074", "10136", "14074", "18100", "21074", "28074"};
    for (int band = 0; band < 7; ++band) {
        reset();
        station.band_index = band;
        CHECK(log_service_write_adif(&log, &station, &qso));
        CHECK(log_service_write_cabrillo(&log, &station, &qso));
        char field[32];
        snprintf(field, sizeof(field), "<freq:%zu>%s ", strlen(mhz[band]), mhz[band]);
        CHECK(strstr(files[0].text, field));
        snprintf(field, sizeof(field), "QSO: %s DG ", khz[band]);
        CHECK(strstr(files[1].text, field));
    }
    reset();
    station.band_index = 3;
    CHECK(log_service_write_adif(&log, &station, &qso));
    CHECK(strcmp(files[0].text,
        "<call:5>W6ABC <gridsquare:4>CM88 <mode:3>FT8<qso_date:8>20240102 "
        "<time_on:6>030405 <freq:6>14.074 <station_callsign:5>AG6AQ "
        "<my_gridsquare:4>CM97 <rst_sent:3>-12 <rst_rcvd:1>5 <comment:0> <eor>\n") == 0);
    CHECK(files[0].syncs == 1 && files[0].closes == 1);
    FakeFile first_adif = files[0];
    CHECK(log_service_write_adif(&log, &station, &qso));
    CHECK(files[0].size == 2 * first_adif.size);
    CHECK(memcmp(files[0].text, first_adif.text, first_adif.size) == 0);
    CHECK(memcmp(files[0].text + first_adif.size, first_adif.text, first_adif.size) == 0);

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
    CHECK(files[1].syncs == 1 && files[1].closes == 1);
    station.band_index = 1;
    CHECK(log_service_write_cabrillo(&log, &station, &qso));
    snprintf(expected, sizeof(expected), "%s%s", header,
        "QSO: 14074 DG 2024-01-02 0304 AG6AQ 1B SCV W6ABC 2A ORG\n"
        "QSO: 7074 DG 2024-01-02 0304 AG6AQ 1B SCV W6ABC 2A ORG\nEND-OF-LOG:\n");
    CHECK(strcmp(files[1].text, expected) == 0);
    CHECK(files[1].syncs == 2 && files[1].closes == 2);

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
    test_persistence(&log, &station, &qso);
    test_qso_pages(&log);
    puts("ft8_log_service_test: PASS");
    return 0;
}
