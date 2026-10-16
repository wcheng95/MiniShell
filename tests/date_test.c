#include <assert.h>
#include <stdio.h>
#include <string.h>
#define main date_app_main
#include "../apps/date/main/date.c"
#undef main

static char output[256];
static unsigned gets, sets, fail_get;
static int fail_set;
static int64_t clock_seconds, requested, readback_offset;
static void write_output(const char *text)
{
    assert(strlen(output) + strlen(text) < sizeof(output));
    strcat(output, text);
}
static mini_result_t get_utc(mini_utc_time_t *value)
{
    assert(value->struct_size == sizeof(*value));
    if (++gets == fail_get) return MINI_ERR_IO;
    value->unix_seconds = clock_seconds;
    value->nanoseconds = 123u;
    /* A second pre-set read would cross midnight and break preservation. */
    clock_seconds += 86400;
    return MINI_OK;
}
static mini_result_t set_utc(const mini_utc_time_t *value)
{
    assert(value->struct_size == sizeof(*value));
    assert(value->nanoseconds == 0);
    ++sets;
    requested = value->unix_seconds;
    if (fail_set) return MINI_ERR_IO;
    clock_seconds = requested + readback_offset;
    return MINI_OK;
}
static mini_console_api_t console = {.write = write_output};
static mini_time_location_api_t time_api = {
    .capabilities = MINI_TIMELOC_CAP_SET_UTC, .utc_get = get_utc, .utc_set = set_utc
};
static const mini_api_t api = {.console = &console, .time_location = &time_api};
const mini_api_t *mini_api_get(void) { return &api; }

static void reset(void)
{
    output[0] = '\0';
    gets = sets = fail_get = 0;
    fail_set = 0;
    requested = readback_offset = 0;
    clock_seconds = 1792022398; /* 2026-10-14 23:59:58 UTC */
    time_api.capabilities = MINI_TIMELOC_CAP_SET_UTC;
    time_api.utc_set = set_utc;
}
static int run(const char *a, const char *b, const char *c)
{
    char *argv[] = {"date", (char *)a, (char *)b, (char *)c, NULL};
    return date_app_main(c ? 4 : b ? 3 : a ? 2 : 1, argv);
}
static void success(const char *a, const char *b, int64_t seconds, const char *line)
{
    reset();
    assert(run(a, b, NULL) == 0);
    assert(sets == 1 && gets == (b ? 1u : 2u));
    assert(requested == seconds);
    assert(strcmp(output, line) == 0);
}
static void invalid(const char *a, const char *b, const char *c)
{
    reset();
    assert(run(a, b, c) == 2);
    assert(sets == 0 && gets == 0);
    assert(strstr(output, "usage: date") != NULL);
}
int main(void)
{
    reset();
    assert(run(NULL, NULL, NULL) == 0);
    assert(gets == 1 && sets == 0);
    assert(strcmp(output, "2026-10-14 23:59:58 UTC\n") == 0);
    success("20261015", NULL, 1792108798, "2026-10-15 23:59:58 UTC\n");
    success("143012", NULL, 1791988212, "2026-10-14 14:30:12 UTC\n");
    success("20261015", "143012", 1792074612, "2026-10-15 14:30:12 UTC\n");
    success("143012", "20261015", 1792074612, "2026-10-15 14:30:12 UTC\n");
    success("202610", NULL, 1792009570, "2026-10-14 20:26:10 UTC\n");
    success("20280229", NULL, 1835481598, "2028-02-29 23:59:58 UTC\n");
    success("19700101", "000000", 0, "1970-01-01 00:00:00 UTC\n");
    success("99991231", "235959", 253402300799LL, "9999-12-31 23:59:59 UTC\n");
    const char *bad[] = {"20260229", "21000229", "240000", "126060", "2026-10-15",
        "14:30:12", "2026-10-15T14:30:12", "1430", "19691231",
        "20260001", "20261301", "20260431", "20261000", "2026101x", "1x3012",
        " 143012", "143012 ", "14 012", "202610150", ""};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) invalid(bad[i], NULL, NULL);
    invalid("20261015", "20261016", NULL);
    invalid("143000", "154000", NULL);
    invalid("2026-10-15", "14:30:12", NULL);
    invalid("20261015", "143000", "extra");
    for (int i = 0; i < 2; ++i) {
        reset();
        if (i) time_api.utc_set = NULL;
        else time_api.capabilities = 0;
        assert(run("143012", NULL, NULL) == 1);
        assert(sets == 0 && gets == 0);
        assert(strcmp(output, "date: setting time unsupported\n") == 0);
    }
    reset(); fail_get = 1;
    assert(run(NULL, NULL, NULL) == 1 && gets == 1 && sets == 0);
    assert(strcmp(output, "date: time unavailable\n") == 0);
    reset(); fail_get = 1;
    assert(run("143012", NULL, NULL) == 1 && gets == 1 && sets == 0);
    assert(strcmp(output, "date: time unavailable\n") == 0);
    reset(); fail_get = 2;
    assert(run("143012", NULL, NULL) == 1 && gets == 2 && sets == 1);
    assert(strcmp(output, "date: time unavailable\n") == 0);
    reset(); fail_set = 1;
    assert(run("143012", NULL, NULL) == 1 && gets == 1 && sets == 1);
    assert(strcmp(output, "date: set failed\n") == 0);
    reset(); readback_offset = 1;
    assert(run("20261015", "143012", NULL) == 0);
    assert(strcmp(output, "2026-10-15 14:30:13 UTC\n") == 0);
    puts("date tests passed");
    return 0;
}
