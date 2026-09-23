#include <assert.h>
#include <stdio.h>
#include <string.h>
static int fail_write, fail_flush, fail_close;
static size_t write_probe(const void *p, size_t size, size_t count, FILE *file)
{
    return fail_write ? 0 : fwrite(p, size, count, file);
}
static int flush_probe(FILE *file) { return fail_flush ? EOF : fflush(file); }
static int close_probe(FILE *file) { int result = fclose(file); return fail_close ? EOF : result; }
#define fwrite write_probe
#define fflush flush_probe
#define fclose close_probe
#include "../apps/js8chat/tools/js8_activity_log.c"
#undef fwrite
#undef fflush
#undef fclose

int main(int argc, char **argv)
{
    Js8ActivityFields f = {0};
    f.kind = JS8_ACTIVITY_MESSAGE; strcpy(f.from, "A\"\\"); strcpy(f.to, "B");
    f.frequency_millihz = -3125;
    char bytes[1023];
    for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = (char)(i % 256);
    Js8Activity event;
    assert(!js8_activity_build(&f, bytes, sizeof(bytes), &event));
    if (argc == 2) {
        Js8ActivityLog log;
        Js8LogMetadata metadata = {0};
        js8_log_open(&log, argv[1], &metadata);
        js8_log_event(&log, &event); js8_log_flush(&log); js8_log_close(&log);
        assert(!log.error);
        return 0;
    }
    assert(argc == 1);
    int64_t dial = -1;
    assert(!js8_log_parse_dial("14078000", &dial) && dial == 14078000);
    assert(!js8_log_parse_dial("9223372034707292", &dial));
    assert(js8_log_parse_dial("9223372034707293", &dial) == -1);
    const char *bad_dial[] = {"", "-1", "+1", " 1", "1.0", "1x", "99999999999999999999999999"};
    for (unsigned i = 0; i < sizeof(bad_dial)/sizeof(*bad_dial); ++i)
        assert(js8_log_parse_dial(bad_dial[i], &dial) == -1);
    uint64_t utc = 123;
    const char *bad_utc[] = {"", "2026-09-23T05:00:00Z", "20260923t050000Z", "20260923T050000z",
        "20260923T050000Zx", "20260923T05000Z", "20260923T050001Z", "20260923T050060Z",
        "20260923T056000Z", "20260923T240000Z", "20260023T050000Z", "20261323T050000Z",
        "20260900T050000Z", "20260931T050000Z", "19000229T000000Z", "00000101T000000Z",
        "20260923T05000aZ", "20260229T000000Z"};
    for (unsigned i = 0; i < sizeof(bad_utc)/sizeof(*bad_utc); ++i) {
        assert(js8_log_parse_utc(bad_utc[i], &utc) == -1 && utc == 123);
    }
    char output[21];
    assert(!js8_log_parse_utc("20000228T235945Z", &utc));
    assert(!js8_log_format_utc(utc+15, output) && !strcmp(output, "2000-02-29T00:00:00Z"));
    assert(!js8_log_parse_utc("20261231T235945Z", &utc));
    assert(!js8_log_format_utc(utc+15, output) && !strcmp(output, "2027-01-01T00:00:00Z"));
    assert(!js8_log_parse_utc("00010101T000000Z", &utc) && utc == 0);
    assert(!js8_log_parse_utc("99991231T235945Z", &utc));
    assert(!js8_log_format_utc(utc+14, output) && !strcmp(output, "9999-12-31T23:59:59Z"));
    assert(js8_log_format_utc(utc+15, output) == -1);
    for (int failure = 0; failure < 3; ++failure) {
        Js8ActivityLog log = {0}; log.file = tmpfile(); assert(log.file);
        fail_write = failure == 0; fail_flush = failure == 1; fail_close = failure == 2;
        js8_log_event(&log, &event); js8_log_flush(&log); js8_log_close(&log);
        assert(log.error && !strcmp(log.error, failure == 0 ? "write" : failure == 1 ? "flush" : "close"));
        assert(!log.file);
    }
    fail_write = fail_flush = fail_close = 0;
    Js8ActivityLog log = {0}; log.file = tmpfile(); assert(log.file);
    log.metadata.have_utc = 1; log.metadata.start_seconds = UINT64_MAX;
    js8_log_event(&log, &event); assert(!strcmp(log.error, "utc_range"));
    js8_log_close(&log);
    puts("js8_activity_log_test: UTC/dial limits, calendar, injected write/flush/close failures: PASS");
    return 0;
}
