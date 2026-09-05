#include <string.h>

#include "services_internal.h"

typedef struct {
    bool utc_valid;
    int64_t utc_anchor_seconds;
    uint32_t utc_anchor_nanoseconds;
    uint64_t utc_anchor_monotonic_us;
    bool default_valid;
    int32_t default_latitude_e7;
    int32_t default_longitude_e7;
    bool live_valid;
    int32_t live_latitude_e7;
    int32_t live_longitude_e7;
    uint64_t live_updated_monotonic_us;
} time_location_state_t;

static time_location_state_t s_state;
static mini_time_location_api_t s_time_api;
static bool s_available;

static bool valid_geo(int32_t latitude_e7, int32_t longitude_e7)
{
    return latitude_e7 >= -900000000 && latitude_e7 <= 900000000 &&
           longitude_e7 >= -1800000000 && longitude_e7 <= 1800000000;
}

static uint64_t monotonic_now(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    return port->monotonic_us != NULL ? port->monotonic_us(port->ctx) : 0u;
}

static void set_utc_anchor(int64_t seconds, uint32_t nanoseconds, uint64_t mono_us)
{
    s_state.utc_valid = true;
    s_state.utc_anchor_seconds = seconds;
    s_state.utc_anchor_nanoseconds = nanoseconds;
    s_state.utc_anchor_monotonic_us = mono_us;
}

static void utc_at(uint64_t mono_us, int64_t *out_seconds, uint32_t *out_nanoseconds)
{
    uint64_t elapsed_us = mono_us - s_state.utc_anchor_monotonic_us;
    int64_t seconds = s_state.utc_anchor_seconds + (int64_t)(elapsed_us / 1000000u);
    uint64_t nanoseconds = (uint64_t)s_state.utc_anchor_nanoseconds + (elapsed_us % 1000000u) * 1000u;
    seconds += (int64_t)(nanoseconds / 1000000000u);
    nanoseconds %= 1000000000u;
    *out_seconds = seconds;
    *out_nanoseconds = (uint32_t)nanoseconds;
}

static uint64_t tl_monotonic_us(void) { return monotonic_now(); }

static mini_result_t tl_sleep_ms(uint32_t milliseconds)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!s_available || port->sleep_ms == NULL) return MINI_ERR_UNSUPPORTED;
    return port->sleep_ms(port->ctx, milliseconds);
}

static mini_result_t tl_utc_get(mini_utc_time_t *out_time)
{
    const uint32_t v0_size = MINI_FIELD_END(mini_utc_time_t, nanoseconds);
    if ((s_time_api.capabilities & MINI_TIMELOC_CAP_UTC) == 0u) return MINI_ERR_UNSUPPORTED;
    if (out_time == NULL || out_time->struct_size < v0_size) return MINI_ERR_INVALID;
    if (!s_state.utc_valid) return MINI_ERR_NOT_READY;
    int64_t seconds;
    uint32_t nanoseconds;
    utc_at(monotonic_now(), &seconds, &nanoseconds);
    out_time->unix_seconds = seconds;
    out_time->nanoseconds = nanoseconds;
    return MINI_OK;
}

mini_result_t minishell_services_utc_sync(int64_t seconds, uint32_t nanoseconds, bool persist)
{
    const minishell_services_port_t *port = minishell_services_port();
    if ((s_time_api.capabilities & MINI_TIMELOC_CAP_UTC) == 0u) return MINI_ERR_UNSUPPORTED;
    if (nanoseconds >= 1000000000u) return MINI_ERR_INVALID;
    if (persist) {
        if ((s_time_api.capabilities & MINI_TIMELOC_CAP_SET_UTC) == 0u || port->utc_store == NULL) return MINI_ERR_UNSUPPORTED;
        mini_result_t result = port->utc_store(port->ctx, seconds, nanoseconds);
        if (result != MINI_OK) return result;
    }
    set_utc_anchor(seconds, nanoseconds, monotonic_now());
    return MINI_OK;
}

static mini_result_t tl_utc_set(const mini_utc_time_t *time)
{
    const uint32_t v0_size = MINI_FIELD_END(mini_utc_time_t, nanoseconds);
    if ((s_time_api.capabilities & MINI_TIMELOC_CAP_SET_UTC) == 0u) return MINI_ERR_UNSUPPORTED;
    if (time == NULL || time->struct_size < v0_size || time->nanoseconds >= 1000000000u) return MINI_ERR_INVALID;
    return minishell_services_utc_sync(time->unix_seconds, time->nanoseconds, true);
}

static mini_result_t effective_location(int32_t *out_latitude_e7, int32_t *out_longitude_e7,
                                        uint32_t *out_source, uint64_t *out_updated_us)
{
    if ((s_time_api.capabilities & MINI_TIMELOC_CAP_LOCATION) == 0u) return MINI_ERR_UNSUPPORTED;
    if (s_state.live_valid) {
        *out_latitude_e7 = s_state.live_latitude_e7;
        *out_longitude_e7 = s_state.live_longitude_e7;
        *out_source = MINI_LOCATION_SOURCE_LIVE;
        *out_updated_us = s_state.live_updated_monotonic_us;
        return MINI_OK;
    }
    if (s_state.default_valid) {
        *out_latitude_e7 = s_state.default_latitude_e7;
        *out_longitude_e7 = s_state.default_longitude_e7;
        *out_source = MINI_LOCATION_SOURCE_DEFAULT;
        *out_updated_us = 0u;
        return MINI_OK;
    }
    return MINI_ERR_NOT_READY;
}

static mini_result_t tl_location_get(mini_location_t *out_location)
{
    const uint32_t v0_size = MINI_FIELD_END(mini_location_t, updated_monotonic_us);
    if ((s_time_api.capabilities & MINI_TIMELOC_CAP_LOCATION) == 0u) return MINI_ERR_UNSUPPORTED;
    if (out_location == NULL || out_location->struct_size < v0_size) return MINI_ERR_INVALID;
    int32_t lat;
    int32_t lon;
    uint32_t source;
    uint64_t updated;
    mini_result_t result = effective_location(&lat, &lon, &source, &updated);
    if (result != MINI_OK) return result;
    out_location->latitude_e7 = lat;
    out_location->longitude_e7 = lon;
    out_location->source = source;
    out_location->reserved0 = 0u;
    out_location->updated_monotonic_us = updated;
    return MINI_OK;
}

static mini_result_t tl_default_get(mini_geo_point_t *out_location)
{
    const uint32_t v0_size = MINI_FIELD_END(mini_geo_point_t, longitude_e7);
    if ((s_time_api.capabilities & MINI_TIMELOC_CAP_DEFAULT_LOCATION) == 0u) return MINI_ERR_UNSUPPORTED;
    if (out_location == NULL || out_location->struct_size < v0_size) return MINI_ERR_INVALID;
    if (!s_state.default_valid) return MINI_ERR_NOT_READY;
    out_location->latitude_e7 = s_state.default_latitude_e7;
    out_location->longitude_e7 = s_state.default_longitude_e7;
    return MINI_OK;
}

static mini_result_t tl_default_set(const mini_geo_point_t *location)
{
    const minishell_services_port_t *port = minishell_services_port();
    const uint32_t v0_size = MINI_FIELD_END(mini_geo_point_t, longitude_e7);
    if ((s_time_api.capabilities & MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION) == 0u) return MINI_ERR_UNSUPPORTED;
    if (location == NULL || location->struct_size < v0_size || !valid_geo(location->latitude_e7, location->longitude_e7)) return MINI_ERR_INVALID;
    if (port->default_location_store == NULL) return MINI_ERR_UNSUPPORTED;
    mini_result_t result = port->default_location_store(port->ctx, location->latitude_e7, location->longitude_e7);
    if (result != MINI_OK) return result;
    s_state.default_valid = true;
    s_state.default_latitude_e7 = location->latitude_e7;
    s_state.default_longitude_e7 = location->longitude_e7;
    return MINI_OK;
}

static mini_result_t tl_default_clear(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    if ((s_time_api.capabilities & MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION) == 0u) return MINI_ERR_UNSUPPORTED;
    if (port->default_location_clear == NULL) return MINI_ERR_UNSUPPORTED;
    mini_result_t result = port->default_location_clear(port->ctx);
    if (result != MINI_OK) return result;
    s_state.default_valid = false;
    s_state.default_latitude_e7 = 0;
    s_state.default_longitude_e7 = 0;
    return MINI_OK;
}

mini_result_t minishell_services_live_location_update(int32_t latitude_e7, int32_t longitude_e7)
{
    if ((s_time_api.capabilities & MINI_TIMELOC_CAP_LOCATION) == 0u) return MINI_ERR_UNSUPPORTED;
    if (!valid_geo(latitude_e7, longitude_e7)) return MINI_ERR_INVALID;
    s_state.live_valid = true;
    s_state.live_latitude_e7 = latitude_e7;
    s_state.live_longitude_e7 = longitude_e7;
    s_state.live_updated_monotonic_us = monotonic_now();
    return MINI_OK;
}

void minishell_services_live_location_clear(void)
{
    s_state.live_valid = false;
    s_state.live_latitude_e7 = 0;
    s_state.live_longitude_e7 = 0;
    s_state.live_updated_monotonic_us = 0u;
}

static mini_result_t tl_snapshot_get(mini_time_location_snapshot_t *out_snapshot)
{
    const uint32_t v0_size = MINI_FIELD_END(mini_time_location_snapshot_t, location_updated_monotonic_us);
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (out_snapshot == NULL || out_snapshot->struct_size < v0_size) return MINI_ERR_INVALID;
    uint64_t now = monotonic_now();
    out_snapshot->reserved_header = 0u;
    out_snapshot->valid_fields = 0u;
    out_snapshot->monotonic_us = now;
    out_snapshot->utc_unix_seconds = 0;
    out_snapshot->utc_nanoseconds = 0u;
    out_snapshot->reserved0 = 0u;
    out_snapshot->latitude_e7 = 0;
    out_snapshot->longitude_e7 = 0;
    out_snapshot->location_source = 0u;
    out_snapshot->reserved1 = 0u;
    out_snapshot->location_updated_monotonic_us = 0u;
    if ((s_time_api.capabilities & MINI_TIMELOC_CAP_UTC) != 0u && s_state.utc_valid) {
        utc_at(now, &out_snapshot->utc_unix_seconds, &out_snapshot->utc_nanoseconds);
        out_snapshot->valid_fields |= MINI_TIMELOC_SNAPSHOT_UTC_VALID;
    }
    int32_t lat;
    int32_t lon;
    uint32_t source;
    uint64_t updated;
    if (effective_location(&lat, &lon, &source, &updated) == MINI_OK) {
        out_snapshot->latitude_e7 = lat;
        out_snapshot->longitude_e7 = lon;
        out_snapshot->location_source = source;
        out_snapshot->location_updated_monotonic_us = updated;
        out_snapshot->valid_fields |= MINI_TIMELOC_SNAPSHOT_LOCATION_VALID;
    }
    return MINI_OK;
}

void minishell_time_location_service_configure(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    memset(&s_state, 0, sizeof(s_state));
    memset(&s_time_api, 0, sizeof(s_time_api));
    s_available = port->monotonic_us != NULL && port->sleep_ms != NULL;
    s_time_api.struct_size = sizeof(s_time_api);
    s_time_api.monotonic_us = tl_monotonic_us;
    s_time_api.sleep_ms = tl_sleep_ms;
    s_time_api.utc_get = tl_utc_get;
    s_time_api.utc_set = tl_utc_set;
    s_time_api.location_get = tl_location_get;
    s_time_api.location_default_get = tl_default_get;
    s_time_api.location_default_set = tl_default_set;
    s_time_api.location_default_clear = tl_default_clear;
    s_time_api.snapshot_get = tl_snapshot_get;
    if (!s_available) {
        s_time_api.capabilities = 0u;
        return;
    }
    uint64_t caps = port->time_location_capabilities;
    if ((caps & MINI_TIMELOC_CAP_SET_UTC) != 0u) {
        caps |= MINI_TIMELOC_CAP_UTC;
        if (port->utc_store == NULL) caps &= ~MINI_TIMELOC_CAP_SET_UTC;
    }
    if ((caps & MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION) != 0u) {
        caps |= MINI_TIMELOC_CAP_DEFAULT_LOCATION | MINI_TIMELOC_CAP_LOCATION;
        if (port->default_location_store == NULL || port->default_location_clear == NULL) caps &= ~MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION;
    }
    if ((caps & MINI_TIMELOC_CAP_DEFAULT_LOCATION) != 0u) caps |= MINI_TIMELOC_CAP_LOCATION;
    s_time_api.capabilities = caps;
    if ((caps & MINI_TIMELOC_CAP_UTC) != 0u && port->utc_load != NULL) {
        int64_t seconds = 0;
        uint32_t nanoseconds = 0u;
        if (port->utc_load(port->ctx, &seconds, &nanoseconds) == MINI_OK && nanoseconds < 1000000000u) {
            set_utc_anchor(seconds, nanoseconds, monotonic_now());
        }
    }
    if ((caps & MINI_TIMELOC_CAP_DEFAULT_LOCATION) != 0u && port->default_location_load != NULL) {
        int32_t lat = 0;
        int32_t lon = 0;
        if (port->default_location_load(port->ctx, &lat, &lon) == MINI_OK && valid_geo(lat, lon)) {
            s_state.default_valid = true;
            s_state.default_latitude_e7 = lat;
            s_state.default_longitude_e7 = lon;
        }
    }
}

bool minishell_time_location_service_available(void) { return s_available; }
const mini_time_location_api_t *minishell_time_location_service_api(void) { return &s_time_api; }
