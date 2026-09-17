#include "app_controller_tx.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static int64_t clamp_monotonic_ms(uint64_t monotonic_us)
{
    uint64_t ms = monotonic_us / 1000u;
    return ms > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)ms;
}

static bool utc_slot_position(const mini_time_location_api_t *time_location,
                              int64_t *out_slot_id,
                              uint16_t *out_ms_into_slot)
{
    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    int64_t slot_id;
    int64_t second_in_slot;
    uint32_t ms;

    if (time_location == NULL || out_slot_id == NULL || out_ms_into_slot == NULL ||
        (time_location->capabilities & MINI_TIMELOC_CAP_UTC) == 0u ||
        time_location->utc_get == NULL ||
        time_location->utc_get(&utc) != MINI_OK ||
        utc.nanoseconds >= 1000000000u) {
        return false;
    }

    slot_id = utc.unix_seconds / 15;
    second_in_slot = utc.unix_seconds % 15;
    if (second_in_slot < 0) {
        second_in_slot += 15;
        --slot_id;
    }

    ms = (uint32_t)second_in_slot * 1000u + utc.nanoseconds / 1000000u;
    if (ms >= 15000u) return false;

    *out_slot_id = slot_id;
    *out_ms_into_slot = (uint16_t)ms;
    return true;
}

/* Howard Hinnant-style civil date conversion, driven only by MiniShell UTC. */
static void civil_from_days(int64_t days, int *out_year, unsigned *out_month,
                            unsigned *out_day)
{
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);                 /* [0, 146096] */
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int year = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned day = doy - (153 * mp + 2) / 5 + 1;
    unsigned month = mp + (mp < 10 ? 3 : (unsigned)-9);

    year += month <= 2;
    *out_year = year;
    *out_month = month;
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
    /* V2/default FT8 dial frequencies, matching V3's 80/40/30/20/17/15/10m order. */
    static const char *const frequencies[] = {
        "3.573", "7.074", "10.136", "14.074", "18.100", "21.074", "28.074"
    };
    if (band_index < 0 || band_index >= (int)(sizeof(frequencies) / sizeof(frequencies[0])))
        return "";
    return frequencies[band_index];
}

static bool build_data_path(const AppController *app, const char *name,
                            char *out, size_t out_size)
{
    const char *slash;
    size_t prefix;
    int written;

    if (app == NULL || name == NULL || out == NULL || out_size == 0u) return false;
    slash = strrchr(app->station_path, '/');
    if (slash == NULL) {
        written = snprintf(out, out_size, "%s", name);
    } else {
        prefix = (size_t)(slash - app->station_path + 1);
        if (prefix >= out_size) return false;
        memcpy(out, app->station_path, prefix);
        written = snprintf(out + prefix, out_size - prefix, "%s", name);
        if (written >= 0) written += (int)prefix;
    }
    return written >= 0 && (size_t)written < out_size;
}

static bool fs_append_line(const mini_fs_api_t *fs, const char *path,
                           const char *line)
{
    mini_file_t file = MINI_FILE_INVALID;
    size_t length;
    size_t total = 0u;

    if (fs == NULL || path == NULL || line == NULL ||
        fs->open == NULL || fs->write == NULL || fs->close == NULL) {
        return false;
    }

    if (fs->open(path, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_APPEND, &file) != MINI_OK)
        return false;

    length = strlen(line);
    while (total < length) {
        uint32_t written = 0u;
        size_t remaining = length - total;
        uint32_t request = remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
        if (fs->write(file, line + total, request, &written) != MINI_OK || written == 0u) {
            (void)fs->close(file);
            return false;
        }
        total += written;
    }

    if (fs->sync != NULL && fs->sync(file) != MINI_OK) {
        (void)fs->close(file);
        return false;
    }
    return fs->close(file) == MINI_OK;
}

static bool write_v2_adif_log(AppController *app, const AutoSeqLogEvent *event)
{
    int year;
    unsigned month, day, hour, minute, second;
    char filename[32];
    char path[256];
    char date[9];
    char time_on[7];
    char rst_sent[16];
    char rst_rcvd[16];
    char line[512];
    const char *freq;
    const char *mode = "FT8";
    int n;

    if (app == NULL || event == NULL || event->adif_eligible == 0u ||
        app->api == NULL || app->api->fs == NULL || app->api->time_location == NULL) {
        return false;
    }
    if (!utc_fields(app->api->time_location, &year, &month, &day,
                    &hour, &minute, &second)) {
        return false;
    }

    n = snprintf(filename, sizeof(filename), "%04d%02u%02u.txt", year, month, day);
    if (n < 0 || (size_t)n >= sizeof(filename) ||
        !build_data_path(app, filename, path, sizeof(path))) {
        return false;
    }
    if (snprintf(date, sizeof(date), "%04d%02u%02u", year, month, day) != 8 ||
        snprintf(time_on, sizeof(time_on), "%02u%02u%02u", hour, minute, second) != 6) {
        return false;
    }

    n = snprintf(rst_sent, sizeof(rst_sent), "%d", (int)event->snr_tx);
    if (n < 0 || (size_t)n >= sizeof(rst_sent)) return false;
    n = snprintf(rst_rcvd, sizeof(rst_rcvd), "%d", (int)event->snr_rx);
    if (n < 0 || (size_t)n >= sizeof(rst_rcvd)) return false;

    freq = band_frequency_mhz(app->config.band_index);
    n = snprintf(line, sizeof(line),
                 "<call:%zu>%s <gridsquare:%zu>%s <mode:%zu>%s"
                 "<qso_date:8>%s <time_on:6>%s <freq:%zu>%s "
                 "<station_callsign:%zu>%s <my_gridsquare:%zu>%s "
                 "<rst_sent:%zu>%s <rst_rcvd:%zu>%s <comment:0> <eor>\n",
                 strlen(event->dxcall), event->dxcall,
                 strlen(event->dxgrid), event->dxgrid,
                 strlen(mode), mode,
                 date, time_on,
                 strlen(freq), freq,
                 strlen(app->config.callsign), app->config.callsign,
                 strlen(app->config.grid), app->config.grid,
                 strlen(rst_sent), rst_sent,
                 strlen(rst_rcvd), rst_rcvd);
    if (n < 0 || (size_t)n >= sizeof(line)) return false;

    return fs_append_line(app->api->fs, path, line);
}

static void remove_pending_cq(AppController *app)
{
    size_t i;

    if (app == NULL) return;
    for (i = 0u; i < auto_seq_active_count(&app->auto_seq); ++i) {
        QsoContext ctx;
        if (!auto_seq_get_active_context(&app->auto_seq, i, &ctx)) return;
        if (ctx.state == AUTO_SEQ_STATE_CALLING &&
            (ctx.flags & AUTO_SEQ_FLAG_FREETEXT) == 0u &&
            strcmp(ctx.dxcall, "CQ") == 0) {
            (void)auto_seq_drop_index(&app->auto_seq, i, 0);
            return;
        }
    }
}

bool app_controller_set_beacon_mode(AppController *app, TxBeaconMode mode)
{
    TxBeaconMode previous;

    if (app == NULL) return false;
    previous = tx_lifecycle_get_beacon_mode(&app->tx.lifecycle);
    if (previous == mode) return true;
    if (!tx_lifecycle_set_beacon_mode(&app->tx.lifecycle, mode)) return false;

    /* A parity/mode change must not leave a stale one-shot CQ in the queue. */
    remove_pending_cq(app);
    return true;
}

TxBeaconMode app_controller_get_beacon_mode(const AppController *app)
{
    return app != NULL ? tx_lifecycle_get_beacon_mode(&app->tx.lifecycle)
                       : TX_BEACON_OFF;
}

bool app_controller_observe_tx_slot(AppController *app,
                                    int64_t slot_id,
                                    uint16_t ms_into_slot,
                                    int64_t now_ms,
                                    bool *out_model_changed)
{
    TxSlotBoundary boundary;
    AutoSeqTxIntent intent;
    AutoSeqLogEvent log_event;
    bool has_boundary;
    bool has_intent;
    AutoSeqResult cq_result;

    if (app == NULL || out_model_changed == NULL) return false;
    *out_model_changed = false;

    if (!tx_lifecycle_observe(&app->tx.lifecycle, slot_id, ms_into_slot,
                              &boundary, &has_boundary)) {
        return false;
    }
    if (!has_boundary) return true;

    has_intent = auto_seq_prepare_tx_intent(&app->auto_seq, &intent);

    /* V2 beacon rule: only create CQ when no QSO/FreeText/CQ intent exists. */
    if (!has_intent && tx_lifecycle_beacon_matches(&app->tx.lifecycle, boundary.parity)) {
        cq_result = auto_seq_start_cq(&app->auto_seq, boundary.parity);
        if (cq_result != AUTO_SEQ_OK && cq_result != AUTO_SEQ_IGNORED)
            return false;
        has_intent = auto_seq_prepare_tx_intent(&app->auto_seq, &intent);
    }

    if (!has_intent || intent.tx_parity != boundary.parity) return true;

    /* TX-start boundary: snapshot intent and logging eligibility before tick. */
    app->tx.last_intent = intent;
    app->tx.last_intent_valid = 1u;
    app->tx.last_tx_slot_id = boundary.slot_id;
    ++app->tx.simulated_tx_count;

    app->tx.last_log_event_valid = 0u;
    if (auto_seq_prepare_log_event(&app->auto_seq, &log_event)) {
        bool adif_written = false;
        bool cabrillo_written = false;

        app->tx.last_log_event = log_event;
        app->tx.last_log_event_valid = 1u;

        /* MiniFT8-V2 writes ADIF at this TX-start eligibility point. File I/O
         * is supplied solely by MiniShell FS; UTC comes solely from Time/Location. */
        if (log_event.adif_eligible != 0u)
            adif_written = write_v2_adif_log(app, &log_event);

        /* Cabrillo Field Day is intentionally left unacknowledged until its
         * V2 writer is ported; this preserves retry/no-duplicate semantics. */
        (void)auto_seq_ack_log_event(&app->auto_seq, &log_event,
                                     adif_written, cabrillo_written);
    }

    /* AS-7 transmitter is synchronous simulation: completion immediately ticks policy. */
    if (!auto_seq_tick(&app->auto_seq, now_ms)) return false;

    *out_model_changed = true;
    return true;
}

bool app_controller_step_tx(AppController *app, bool *out_model_changed)
{
    const mini_time_location_api_t *time_location;
    int64_t slot_id;
    uint16_t ms_into_slot;
    int64_t now_ms = 0;

    if (app == NULL || out_model_changed == NULL) return false;
    *out_model_changed = false;

    if (app->api == NULL || app->api->time_location == NULL) return true;
    time_location = app->api->time_location;
    if (!utc_slot_position(time_location, &slot_id, &ms_into_slot)) return true;
    if (time_location->monotonic_us != NULL)
        now_ms = clamp_monotonic_ms(time_location->monotonic_us());

    return app_controller_observe_tx_slot(app, slot_id, ms_into_slot,
                                          now_ms, out_model_changed);
}
