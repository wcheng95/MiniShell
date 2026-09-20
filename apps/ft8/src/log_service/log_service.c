#include "log_service.h"
#include "config_service.h"

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

static void band_frequency_mhz(int band_index, char out[16])
{
    uint32_t hz = config_service_band_dial_hz(band_index);
    out[0] = '\0';
    if (hz) (void)snprintf(out, 16, "%lu.%03lu", (unsigned long)(hz / 1000000u),
                          (unsigned long)((hz % 1000000u) / 1000u));
}

static int band_frequency_khz(int band_index)
{
    return (int)(config_service_band_dial_hz(band_index) / 1000u);
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

static bool fs_read_exact(const mini_fs_api_t *fs, mini_file_t file,
                           char *buffer, uint32_t size)
{
    uint32_t total = 0u;
    while (total < size) {
        uint32_t got = 0u;
        if (fs->read(file, buffer + total, size - total, &got) != MINI_OK ||
            got == 0u || got > size - total) return false;
        total += got;
    }
    return true;
}

/* Final is read-only until rename. Every acquired handle gets one close attempt. */
static bool fs_commit_record(const mini_fs_api_t *fs, const char *path,
                              const char *new_header, const char *record,
                              const char *end_marker)
{
    mini_fs_stat_t stat = {.struct_size = sizeof(stat)};
    mini_file_t source = MINI_FILE_INVALID;
    mini_file_t temp = MINI_FILE_INVALID;
    char temp_path[260];
    char buffer[512];
    bool ok = false;
    bool temp_created = false;
    size_t marker_size = strlen(end_marker);
    uint64_t copy_size = 0u;

    if (fs == NULL || fs->stat == NULL || fs->open == NULL ||
        fs->read == NULL || fs->write == NULL || fs->sync == NULL ||
        fs->close == NULL || fs->rename == NULL || fs->remove_file == NULL ||
        (marker_size != 0u && fs->seek == NULL)) return false;
    int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(temp_path)) return false;

    mini_result_t result = fs->stat(path, &stat);
    bool exists = result == MINI_OK;
    if (!exists && result != MINI_ERR_NOT_FOUND) return false;
    if (exists) {
        if (stat.type != MINI_FS_TYPE_FILE || stat.size < marker_size) return false;
        copy_size = stat.size - marker_size;
        if (fs->open(path, MINI_FS_READ, &source) != MINI_OK) return false;
        if (marker_size != 0u) {
            uint64_t position = 0u;
            if (copy_size > INT64_MAX || marker_size > sizeof(buffer) ||
                fs->seek(source, (int64_t)copy_size, MINI_FS_SEEK_SET, &position) != MINI_OK ||
                position != copy_size ||
                !fs_read_exact(fs, source, buffer, (uint32_t)marker_size) ||
                memcmp(buffer, end_marker, marker_size) != 0 ||
                fs->seek(source, 0, MINI_FS_SEEK_SET, &position) != MINI_OK ||
                position != 0u) goto cleanup;
        }
    }

    if (fs->open(temp_path, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC,
                 &temp) != MINI_OK) goto cleanup;
    temp_created = true;
    while (copy_size != 0u) {
        uint32_t chunk = copy_size > sizeof(buffer) ? sizeof(buffer) : (uint32_t)copy_size;
        if (!fs_read_exact(fs, source, buffer, chunk) ||
            !fs_write_all(fs, temp, buffer, chunk)) goto cleanup;
        copy_size -= chunk;
    }
    if (!exists && !fs_write_all(fs, temp, new_header, strlen(new_header))) goto cleanup;
    if (!fs_write_all(fs, temp, record, strlen(record)) ||
        !fs_write_all(fs, temp, end_marker, marker_size)) goto cleanup;
    ok = true;

cleanup:
    if (source != MINI_FILE_INVALID && fs->close(source) != MINI_OK) ok = false;
    if (temp_created) {
        if (ok && fs->sync(temp) != MINI_OK) ok = false;
        if (fs->close(temp) != MINI_OK) ok = false;
        if (ok) ok = fs->rename(temp_path, path) == MINI_OK;
        if (!ok) (void)fs->remove_file(temp_path);
    }
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
    char freq[16];
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

    band_frequency_mhz(station->band_index, freq);
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

    return fs_commit_record(service->fs, path, "", line, "");
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

static bool cabrillo_header(char *header, size_t header_size,
                            const char *mycall, const char *location)
{
    int n;
    n = snprintf(header, header_size,
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
                 "OPERATORS: %s\n",
                 mycall, location, mycall);
    if (n < 0 || (size_t)n >= header_size) return false;

    return true;
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
    char header[512];
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

    n = snprintf(qso_line, sizeof(qso_line), "QSO: %d DG %s %s %s %s %s %s\n",
                 band_frequency_khz(station->band_index),
                 date_ymd, time_hhmm,
                 station->callsign, my_fd,
                 event->dxcall, their_fd);
    if (n < 0 || (size_t)n >= sizeof(qso_line)) return false;

    if (!cabrillo_header(header, sizeof(header), station->callsign, location)) return false;
    return fs_commit_record(service->fs, path, header, qso_line, "END-OF-LOG:\n");
}

bool log_service_write_rt(const LogService *service, bool transmit, int band_index,
                           const char *text, int8_t snr_db, int16_t offset_hz)
{
    int year;
    unsigned month, day, hour, minute, second;
    char name[32], path[256], frequency[16], line[192];
    if (!service || !text || !*text || strlen(text) >= 64 ||
        strchr(text, '\n') || strchr(text, '\r') || !service->fs ||
        !service->fs->open || !service->fs->write || !service->fs->sync || !service->fs->close ||
        !utc_fields(service->time_location, &year, &month, &day, &hour, &minute, &second) ||
        year < 0 || year > 9999) return false;
    band_frequency_mhz(band_index, frequency);
    if (!*frequency) return false;
    (void)snprintf(name, sizeof(name), "RT%02d%02u%02u.txt", year % 100, month, day);
    if (!build_data_path(service, name, path, sizeof(path))) return false;
    int n = snprintf(line, sizeof(line), "%c [%04d%02u%02u %02u%02u%02u][%s] %s ",
                     transmit ? 'T' : 'R', year, month, day, hour, minute, second, frequency, text);
    if (n < 0 || (size_t)n >= sizeof(line)) return false;
    int tail = transmit ? snprintf(line + n, sizeof(line) - (size_t)n, "%d\n", offset_hz)
                        : snprintf(line + n, sizeof(line) - (size_t)n, "%d %d\n", snr_db, offset_hz);
    if (tail < 0 || (size_t)tail >= sizeof(line) - (size_t)n) return false;
    mini_file_t file = MINI_FILE_INVALID;
    if (service->fs->open(path, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_APPEND, &file) != MINI_OK)
        return false;
    bool ok = fs_write_all(service->fs, file, line, (size_t)(n + tail));
    if (ok && service->fs->sync(file) != MINI_OK) ok = false;
    if (service->fs->close(file) != MINI_OK) ok = false;
    return ok;
}

/* V3 emits one length-delimited ADIF record per line. Never search inside a
 * field value for another tag: comments can contain arbitrary tag-like text. */
static bool qso_record(const char *line, const char *date, LogQsoSummary *out)
{
    unsigned fields = 0;
    bool end = false;
    memset(out, 0, sizeof(*out));
    while (*line) {
        while (*line == ' ' || *line == '\t' || *line == '\r') ++line;
        if (!*line) break;
        if (end || *line++ != '<') return false;
        char tag[32]; size_t name = 0;
        while (*line && *line != ':' && *line != '>') {
            if (name + 1 >= sizeof(tag)) return false;
            unsigned char c = (unsigned char)*line++;
            tag[name++] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        }
        tag[name] = 0;
        if (*line == '>' && strcmp(tag, "eor") == 0) { end = true; ++line; continue; }
        if (*line++ != ':') return false;
        size_t length = 0;
        if (*line < '0' || *line > '9') return false;
        while (*line >= '0' && *line <= '9') {
            length = length * 10 + (unsigned)(*line++ - '0');
            if (length >= 512) return false;
        }
        if (*line++ != '>' || strlen(line) < length) return false;
        unsigned bit = 0;
        if (strcmp(tag, "call") == 0) {
            bit = 1;
            if (!length || length >= sizeof(out->call)) return false;
            for (size_t i = 0; i < length; ++i)
                if (!((line[i] >= 'A' && line[i] <= 'Z') ||
                      (line[i] >= 'a' && line[i] <= 'z') ||
                      (line[i] >= '0' && line[i] <= '9') || line[i] == '/')) return false;
            memcpy(out->call, line, length);
        } else if (strcmp(tag, "qso_date") == 0) {
            bit = 2;
            if (length != 8 || memcmp(line, date, 8)) return false;
        } else if (strcmp(tag, "time_on") == 0) {
            bit = 4;
            if (length != 6) return false;
            for (size_t i = 0; i < length; ++i) if (line[i] < '0' || line[i] > '9') return false;
            out->hour = (uint8_t)((line[0]-'0')*10 + line[1]-'0');
            out->minute = (uint8_t)((line[2]-'0')*10 + line[3]-'0');
            if (out->hour > 23 || out->minute > 59 || line[4] > '5') return false;
        } else if (strcmp(tag, "freq") == 0) {
            bit = 8;
            char frequency[16];
            bool found = false;
            for (int band = 0; band < config_service_band_count(0); ++band) {
                band_frequency_mhz(band, frequency);
                if (strlen(frequency) == length && memcmp(line, frequency, length) == 0) {
                    snprintf(out->band, sizeof(out->band), "%s", config_service_band_name(0, band));
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        if (fields & bit) return false;
        fields |= bit;
        line += length;
    }
    return end && fields == 15;
}

static bool qso_keep_record(LogQsoPage *out, uint32_t requested, const char *line, const char *date)
{
    LogQsoSummary row;
    if (!qso_record(line, date, &row)) return true;
    if (out->total_count == UINT32_MAX) return false;
    uint32_t page = out->total_count / LOG_QSO_PAGE_ROWS;
    uint32_t index = out->total_count % LOG_QSO_PAGE_ROWS;
    if (page <= requested) {
        if (!index) {
            memset(out->rows, 0, sizeof(out->rows));
            out->row_count = 0;
            out->page_index = page;
        }
        out->rows[index] = row;
        ++out->row_count;
    }
    ++out->total_count;
    return true;
}

void log_service_read_qso_page(const LogService *service, uint32_t page_index, LogQsoPage *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->page_count = 1;
    int year; unsigned month, day, hour, minute, second;
    if (!service || !utc_fields(service->time_location, &year, &month, &day, &hour, &minute, &second) ||
        year < 0 || year > 9999) {
        out->status = LOG_QSO_VIEW_UTC_UNAVAILABLE;
        return;
    }
    char date[9], filename[16], path[256];
    snprintf(date, sizeof(date), "%04d%02u%02u", year, month, day);
    snprintf(filename, sizeof(filename), "%s.txt", date);
    const mini_fs_api_t *fs = service->fs;
    if (!fs || !fs->open || !fs->read || !fs->close ||
        !build_data_path(service, filename, path, sizeof(path))) {
        out->status = LOG_QSO_VIEW_READ_ERROR;
        return;
    }
    mini_file_t file = MINI_FILE_INVALID;
    mini_result_t result = fs->open(path, MINI_FS_READ, &file);
    if (result == MINI_ERR_NOT_FOUND) return;
    if (result != MINI_OK) { out->status = LOG_QSO_VIEW_READ_ERROR; return; }
    char line[512], chunk[128];
    size_t used = 0;
    bool oversized = false, ok = true;
    for (;;) {
        uint32_t got = 0;
        if (fs->read(file, chunk, sizeof(chunk), &got) != MINI_OK || got > sizeof(chunk)) {
            ok = false; break;
        }
        if (!got) {
            if (used && !oversized) { line[used] = 0; ok = qso_keep_record(out, page_index, line, date); }
            break;
        }
        for (uint32_t i = 0; i < got; ++i) {
            if (chunk[i] == '\n') {
                if (!oversized) { line[used] = 0; ok = qso_keep_record(out, page_index, line, date); }
                used = 0; oversized = false;
                if (!ok) break;
            } else if (!chunk[i] || used == sizeof(line) - 1) {
                oversized = true; // Discard through newline, including embedded NUL.
            } else if (!oversized) line[used++] = chunk[i];
        }
        if (!ok) break;
    }
    if (fs->close(file) != MINI_OK) ok = false;
    if (!ok) {
        memset(out, 0, sizeof(*out));
        out->status = LOG_QSO_VIEW_READ_ERROR;
        out->page_count = 1;
    } else if (out->total_count) {
        out->page_count = out->total_count / LOG_QSO_PAGE_ROWS + (out->total_count % LOG_QSO_PAGE_ROWS != 0);
    }
}
