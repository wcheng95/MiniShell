#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "minishell/api.h"

static int decimal2(const char *p)
{
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
    return (p[0] - '0') * 10 + (p[1] - '0');
}

static int decimal4(const char *p)
{
    int a = decimal2(p);
    int b = decimal2(p + 2);
    return a < 0 || b < 0 ? -1 : a * 100 + b;
}

static int is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && is_leap(year)) return 29;
    return days[month - 1];
}

static int parse_date_text(const char *text, int *year, int *month, int *day)
{
    if (text == NULL || strlen(text) != 10u || text[4] != '-' || text[7] != '-') return 0;
    int y = decimal4(text);
    int m = decimal2(text + 5);
    int d = decimal2(text + 8);
    if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 || d > days_in_month(y, m)) return 0;
    *year = y;
    *month = m;
    *day = d;
    return 1;
}

static int parse_time_text(const char *text, int *hour, int *minute, int *second)
{
    if (text == NULL || strlen(text) != 8u || text[2] != ':' || text[5] != ':') return 0;
    int h = decimal2(text);
    int m = decimal2(text + 3);
    int s = decimal2(text + 6);
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return 0;
    *hour = h;
    *minute = m;
    *second = s;
    return 1;
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2u;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned shifted_month = (unsigned)((int)month + (month > 2u ? -3 : 9));
    unsigned doy = (153u * shifted_month + 2u) / 5u + day - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *year, unsigned *month, unsigned *day)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int y = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp = (5u * doy + 2u) / 153u;
    unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    unsigned m = mp < 10u ? mp + 3u : mp - 9u;
    y += m <= 2u;
    *year = y;
    *month = m;
    *day = d;
}

static int make_seconds(int year, int month, int day,
                        int hour, int minute, int second,
                        int64_t *out_seconds)
{
    int64_t days = days_from_civil(year, (unsigned)month, (unsigned)day);
    if (days > (INT64_MAX - 86399) / 86400) return 0;
    *out_seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    return 1;
}

static void print_utc(const mini_console_api_t *console, int64_t seconds)
{
    int64_t days = seconds / 86400;
    int64_t sod = seconds % 86400;
    if (sod < 0) {
        sod += 86400;
        --days;
    }

    int year;
    unsigned month;
    unsigned day;
    civil_from_days(days, &year, &month, &day);
    unsigned hour = (unsigned)(sod / 3600);
    unsigned minute = (unsigned)((sod % 3600) / 60);
    unsigned second = (unsigned)(sod % 60);

    char line[40];
    (void)snprintf(line, sizeof(line), "%04d-%02u-%02u %02u:%02u:%02u UTC\n",
                   year, month, day, hour, minute, second);
    console->write(line);
}

static int read_and_print(const mini_api_t *api)
{
    mini_utc_time_t now = {.struct_size = sizeof(now)};
    if (api->time_location->utc_get(&now) != MINI_OK) {
        api->console->write("date: time unavailable\n");
        return 1;
    }
    print_utc(api->console, now.unix_seconds);
    return 0;
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->console == NULL || api->console->write == NULL ||
        api->time_location == NULL || api->time_location->utc_get == NULL) {
        return 2;
    }

    if (argc == 1) return read_and_print(api);

    int year, month, day, hour, minute, second;
    int valid = 0;
    if (argc == 2 && strlen(argv[1]) == 19u && argv[1][10] == 'T') {
        char date_part[11];
        memcpy(date_part, argv[1], 10u);
        date_part[10] = '\0';
        valid = parse_date_text(date_part, &year, &month, &day) &&
                parse_time_text(argv[1] + 11, &hour, &minute, &second);
    } else if (argc == 3) {
        valid = parse_date_text(argv[1], &year, &month, &day) &&
                parse_time_text(argv[2], &hour, &minute, &second);
    }

    if (!valid) {
        api->console->write("usage: date [YYYY-MM-DD HH:MM:SS]\n");
        return 2;
    }
    if ((api->time_location->capabilities & MINI_TIMELOC_CAP_SET_UTC) == 0u ||
        api->time_location->utc_set == NULL) {
        api->console->write("date: setting time unsupported\n");
        return 1;
    }

    int64_t seconds;
    if (!make_seconds(year, month, day, hour, minute, second, &seconds)) {
        api->console->write("date: invalid time\n");
        return 2;
    }

    mini_utc_time_t value = {
        .struct_size = sizeof(value),
        .unix_seconds = seconds,
        .nanoseconds = 0u,
    };
    if (api->time_location->utc_set(&value) != MINI_OK) {
        api->console->write("date: set failed\n");
        return 1;
    }
    return read_and_print(api);
}
