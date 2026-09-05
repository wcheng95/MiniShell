#include "abi_test.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = NULL;
    int rc = abi_test_base(&api);
    if (rc != 0) return rc;

    if (!ABI_HAS_API_FIELD(api, time_location) || api->time_location == NULL)
        return abi_test_fail(api, "abi_time_location", "service unavailable", 30);

    const mini_time_location_api_t *tl = api->time_location;
    if (tl->struct_size < ABI_FIELD_END(mini_time_location_api_t, snapshot_get) ||
        tl->monotonic_us == NULL || tl->sleep_ms == NULL || tl->snapshot_get == NULL)
        return abi_test_fail(api, "abi_time_location", "v0 baseline incomplete", 31);

    uint64_t before = tl->monotonic_us();
    if (tl->sleep_ms(2u) != MINI_OK)
        return abi_test_fail(api, "abi_time_location", "sleep_ms failed", 32);
    uint64_t after = tl->monotonic_us();
    if (after <= before)
        return abi_test_fail(api, "abi_time_location", "monotonic did not advance", 33);

    mini_time_location_snapshot_t snapshot = {0};
    snapshot.struct_size = sizeof(snapshot);
    if (tl->snapshot_get(&snapshot) != MINI_OK || snapshot.monotonic_us < after)
        return abi_test_fail(api, "abi_time_location", "snapshot failed", 34);

    if ((tl->capabilities & MINI_TIMELOC_CAP_UTC) != 0u) {
        mini_utc_time_t utc = {0};
        utc.struct_size = sizeof(utc);
        mini_result_t r = tl->utc_get(&utc);
        if (r != MINI_OK && r != MINI_ERR_NOT_READY)
            return abi_test_fail(api, "abi_time_location", "utc_get result", 35);
    }

    if ((tl->capabilities & MINI_TIMELOC_CAP_DEFAULT_LOCATION) != 0u) {
        mini_geo_point_t point = {0};
        point.struct_size = sizeof(point);
        mini_result_t r = tl->location_default_get(&point);
        if (r != MINI_OK && r != MINI_ERR_NOT_READY)
            return abi_test_fail(api, "abi_time_location", "default location result", 36);
    }

    if ((tl->capabilities & MINI_TIMELOC_CAP_LOCATION) != 0u) {
        mini_location_t location = {0};
        location.struct_size = sizeof(location);
        mini_result_t r = tl->location_get(&location);
        if (r != MINI_OK && r != MINI_ERR_NOT_READY)
            return abi_test_fail(api, "abi_time_location", "location_get result", 37);
    }

    abi_test_line(api, "abi_time_location", "PASS");
    return 0;
}
