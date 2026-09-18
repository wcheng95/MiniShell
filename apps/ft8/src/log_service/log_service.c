#include "log_service.h"

#include <stdio.h>
#include <string.h>

bool log_service_init(LogService *service, const mini_fs_api_t *fs,
                      const mini_time_location_api_t *time_location,
                      const char *station_path)
{
    if (service == NULL || station_path == NULL) return false;
    const char *slash = strrchr(station_path, '/');
    size_t prefix = slash != NULL ? (size_t)(slash - station_path + 1) : 0u;
    if (prefix >= sizeof(service->path_prefix)) return false;
    memcpy(service->path_prefix, station_path, prefix);
    service->path_prefix[prefix] = '\0';
    service->fs = fs;
    service->time_location = time_location;
    return true;
}

/* Gregorian civil-date conversion driven only by MiniShell UTC. */
static void civil_from_days(int64_t days, int *out_year, unsigned *out_month,
                            unsigned *out_day)
{
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int year = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned day = doy - (153 * mp + 2) / 5 + 1;
    int month = (int)mp + (mp < 10 ? 3 : -9);

    year += month <= 2;
    *out_year = year;
    *out_month = (unsigned)month;
    *out_day = day;
}

static bool utc_fields(const mini_time_location_api_t *time_location,
                       int *out_year, unsigned *out_month, unsigned *out_day,
                       unsigned *out_hour, unsigned *out_minute,
                       unsigned *out_second)
{
    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    int64_t days;
    int64_t sod;

    if (time_location == NULL || out_year == NULL || out_month == NULL ||
        out_day == NULL || out_hour == NULL || out_minute == NULL ||
        out_second == NULL ||
        (time_location->capabilities & MINI_TIMELOC_CAP_UTC) == 0u ||
        time_location->utc_get == NULL ||
        time_location->utc_get(&utc) != MINI_OK) {
        return false;
    }

    days = utc.unix_seconds / 86400;
    sod = utc.unix_seconds % 86400;
    if (sod < 0) {
        sod += 86400;
        --days;
    }

    civil_from_days(days, out_year, out_month, out_day);
    *out_hour = (unsigned)(sod / 3600);
    *out_minute = (unsigned)((sod % 3600) / 60);
    *out_second = (unsigned)(sod % 60);
    return true;
}

static const char *band_frequency_mhz(int band_index)
{
    static const char *const frequencies[] = {
        "3.573", "7.074", "10.136", "14.074", "18.100", "21.074", "28.074"
    };
    if (band_index < 0 || band_index >= (int)(sizeof(frequencies) / sizeof(frequencies[0])))
        return "";
    return frequencies[band_index];
}

static int band_frequency_khz(int band_index)
{
    static const int frequencies[] = {
        3573, 7074, 10136, 14074, 18100, 21074, 28074
    };
    if (band_index < 0 || band_index >= (int)(sizeof(frequencies) / sizeof(frequencies[0])))
        return 0;
    return frequencies[band_index];
}

static bool build_data_path(const LogService *service, const char *name,
                            char *out, size_t out_size)
{
    if (service == NULL || name == NULL || out == NULL || out_size == 0u) return false;
    int written = snprintf(out, out_size, "%s%s", service->path_prefix, name);
    return written >= 0 && (size_t)written < out_size;
}

static bool fs_write_all(const mini_fs_api_t *fs, mini_file_t file,
                         const char *text, size_t length)
{
    size_t total = 0u;

    if (fs == NULL || fs->write == NULL || text == NULL) return false;
    while (total < length) {
        uint32_t written = 0u;
        size_t remaining = length - total;
        uint32_t request = remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
        if (fs->write(file, text + total, request, &written) != MINI_OK || written == 0u)
            return false;
        total += written;
    }
    return true;
}

static bool fs_append_line(const mini_fs_api_t *fs, const char *path,
                           const char *line)
{
    mini_file_t file = MINI_FILE_INVALID;
    bool ok;

    if (fs == NULL || path == NULL || line == NULL ||
        fs->open == NULL || fs->write == NULL || fs->close == NULL) {
        return false;
    }

    if (fs->open(path, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_APPEND, &file) != MINI_OK)
        return false;

    ok = fs_write_all(fs, file, line, strlen(line));
    if (ok && fs->sync != NULL) ok = fs->sync(file) == MINI_OK;
    if (fs->close(file) != MINI_OK) ok = false;
    return ok;
}

bool log_service_write_adif(const LogService *service, const LogStationFacts *station,
                            const LogQsoFacts *event)
{
    int year;
    unsigned month, day, hour, minute, second;
    char filename[32];
    char path[256];
    char date[9];
    char time_on[7];
    char rst_sent_buf[32] = "";
    char rst_rcvd_buf[32] = "";
    char my_grid4[5] = "";
    char line[512];
    const char *freq;
    const char *mode = "FT8";
    size_t my_grid_len;
    int n;

    if (service == NULL || station == NULL || event == NULL ||
        service->fs == NULL || service->time_location == NULL) {
        return false;
    }
    if (!utc_fields(service->time_location, &year, &month, &day,
                    &hour, &minute, &second)) {
        return false;
    }

    n = snprintf(filename, sizeof(filename), "%04d%02u%02u.txt", year, month, day);
    if (n < 0 || (size_t)n >= sizeof(filename) ||
        !build_data_path(service, filename, path, sizeof(path))) {
        return false;
    }
    if (snprintf(date, sizeof(date), "%04d%02u%02u", year, month, day) != 8 ||
        snprintf(time_on, sizeof(time_on), "%02u%02u%02u", hour, minute, second) != 6) {
        return false;
    }

    /* V2 omits unknown -99 reports instead of writing an ADIF -99 value. */
    if (event->snr_tx_known) {
        n = snprintf(rst_sent_buf, sizeof(rst_sent_buf), "<rst_sent:%d>%d ",
                     snprintf(NULL, 0, "%d", (int)event->snr_tx),
                     (int)event->snr_tx);
        if (n < 0 || (size_t)n >= sizeof(rst_sent_buf)) return false;
    }
    if (event->snr_rx_known) {
        n = snprintf(rst_rcvd_buf, sizeof(rst_rcvd_buf), "<rst_rcvd:%d>%d ",
                     snprintf(NULL, 0, "%d", (int)event->snr_rx),
                     (int)event->snr_rx);
        if (n < 0 || (size_t)n >= sizeof(rst_rcvd_buf)) return false;
    }

    my_grid_len = strlen(station->effective_grid);
    if (my_grid_len > 4u) my_grid_len = 4u;
    memcpy(my_grid4, station->effective_grid, my_grid_len);
    my_grid4[my_grid_len] = '\0';

    freq = band_frequency_mhz(station->band_index);
    n = snprintf(line, sizeof(line),
                 "<call:%zu>%s <gridsquare:%zu>%s <mode:%zu>%s"
                 "<qso_date:8>%s <time_on:6>%s <freq:%zu>%s "
                 "<station_callsign:%zu>%s <my_gridsquare:%zu>%s "
                 "%s%s<comment:0> <eor>\n",
                 strlen(event->dxcall), event->dxcall,
                 strlen(event->dxgrid), event->dxgrid,
                 strlen(mode), mode,
                 date, time_on,
                 strlen(freq), freq,
                 strlen(station->callsign), station->callsign,
                 my_grid_len, my_grid4,
                 rst_sent_buf, rst_rcvd_buf);
    if (n < 0 || (size_t)n >= sizeof(line)) return false;

    return fs_append_line(service->fs, path, line);
}

static const char *fd_strip_r(const char *exchange)
{
    if (exchange == NULL) return "";
    while (*exchange == ' ') ++exchange;
    if ((exchange[0] == 'R' || exchange[0] == 'r') && exchange[1] == ' ')
        exchange += 2;
    while (*exchange == ' ') ++exchange;
    return exchange;
}

static const char *fd_section(const char *exchange)
{
    const char *p = fd_strip_r(exchange);
    const char *space = strchr(p, ' ');
    if (space == NULL) return "";
    while (*space == ' ') ++space;
    return space;
}

static bool cabrillo_write_header(const mini_fs_api_t *fs, const char *path,
                                  const char *mycall, const char *location)
{
    mini_file_t file = MINI_FILE_INVALID;
    char header[512];
    int n;
    bool ok;

    if (fs == NULL || path == NULL || mycall == NULL || location == NULL ||
        fs->open == NULL || fs->close == NULL) {
        return false;
    }

    n = snprintf(header, sizeof(header),
                 "START-OF-LOG: 3.0\n"
                 "CREATED-BY: Mini-FT8\n"
                 "CONTEST: ARRL-FIELD-DAY\n"
                 "CALLSIGN: %s\n"
                 "CATEGORY-OPERATOR: SINGLE-OP\n"
                 "CATEGORY-TRANSMITTER: ONE\n"
                 "CATEGORY-ASSISTED: NON-ASSISTED\n"
                 "CATEGORY-BAND: ALL\n"
                 "CATEGORY-MODE: MIXED\n"
                 "CATEGORY-POWER: LOW\n"
                 "CATEGORY-STATION: PORTABLE\n"
                 "LOCATION: %s\n"
                 "OPERATORS: %s\n"
                 "END-OF-LOG:\n",
                 mycall, location, mycall);
    if (n < 0 || (size_t)n >= sizeof(header)) return false;

    if (fs->open(path, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC, &file) != MINI_OK)
        return false;
    ok = fs_write_all(fs, file, header, (size_t)n);
    if (ok && fs->sync != NULL) ok = fs->sync(file) == MINI_OK;
    if (fs->close(file) != MINI_OK) ok = false;
    return ok;
}

static bool cabrillo_append_qso(const mini_fs_api_t *fs, const char *path,
                                const char *mycall, const char *location,
                                const char *qso_line)
{
    static const char end_marker[] = "END-OF-LOG:\n";
    mini_fs_stat_t stat = {.struct_size = sizeof(stat)};
    mini_file_t file = MINI_FILE_INVALID;
    uint64_t marker_pos;
    uint64_t pos = 0u;
    char marker[sizeof(end_marker)];
    uint32_t got = 0u;
    bool ok;

    if (fs == NULL || path == NULL || qso_line == NULL ||
        fs->stat == NULL || fs->open == NULL || fs->read == NULL ||
        fs->seek == NULL || fs->close == NULL) {
        return false;
    }

    if (fs->stat(path, &stat) != MINI_OK || stat.size == 0u) {
        if (!cabrillo_write_header(fs, path, mycall, location)) return false;
        if (fs->stat(path, &stat) != MINI_OK) return false;
    }
    if (stat.size < sizeof(end_marker) - 1u) return false;

    if (fs->open(path, MINI_FS_READ | MINI_FS_WRITE, &file) != MINI_OK) return false;
    marker_pos = stat.size - (sizeof(end_marker) - 1u);
    ok = fs->seek(file, (int64_t)marker_pos, MINI_FS_SEEK_SET, &pos) == MINI_OK &&
         pos == marker_pos;
    if (ok) ok = fs->read(file, marker, sizeof(end_marker) - 1u, &got) == MINI_OK &&
                 got == sizeof(end_marker) - 1u &&
                 memcmp(marker, end_marker, sizeof(end_marker) - 1u) == 0;
    if (ok) ok = fs->seek(file, (int64_t)marker_pos, MINI_FS_SEEK_SET, &pos) == MINI_OK &&
                 pos == marker_pos;
    if (ok) ok = fs_write_all(fs, file, qso_line, strlen(qso_line));
    if (ok) ok = fs_write_all(fs, file, "\n", 1u);
    if (ok) ok = fs_write_all(fs, file, end_marker, sizeof(end_marker) - 1u);
    if (ok && fs->sync != NULL) ok = fs->sync(file) == MINI_OK;
    if (fs->close(file) != MINI_OK) ok = false;
    return ok;
}

bool log_service_write_cabrillo(const LogService *service, const LogStationFacts *station,
                                const LogQsoFacts *event)
{
    int year;
    unsigned month, day, hour, minute, second;
    char path[256];
    char date_ymd[16];
    char time_hhmm[8];
    char qso_line[160];
    const char *my_fd;
    const char *their_fd;
    const char *location;
    int n;

    if (service == NULL || station == NULL || event == NULL ||
        service->fs == NULL || service->time_location == NULL)
        return false;

    my_fd = fd_strip_r(station->fd_exchange);
    their_fd = fd_strip_r(event->fd_rx_exchange);
    location = fd_section(my_fd);
    if (my_fd[0] == '\0' || their_fd[0] == '\0' || location[0] == '\0') return false;

    if (!utc_fields(service->time_location, &year, &month, &day,
                    &hour, &minute, &second)) {
        return false;
    }
    (void)second;
    if (!build_data_path(service, "fieldday.txt", path, sizeof(path))) return false;

    n = snprintf(date_ymd, sizeof(date_ymd), "%04d-%02u-%02u", year, month, day);
    if (n < 0 || (size_t)n >= sizeof(date_ymd)) return false;
    n = snprintf(time_hhmm, sizeof(time_hhmm), "%02u%02u", hour, minute);
    if (n < 0 || (size_t)n >= sizeof(time_hhmm)) return false;

    n = snprintf(qso_line, sizeof(qso_line), "QSO: %d DG %s %s %s %s %s %s",
                 band_frequency_khz(station->band_index),
                 date_ymd, time_hhmm,
                 station->callsign, my_fd,
                 event->dxcall, their_fd);
    if (n < 0 || (size_t)n >= sizeof(qso_line)) return false;

    return cabrillo_append_qso(service->fs, path,
                               station->callsign, location, qso_line);
}

