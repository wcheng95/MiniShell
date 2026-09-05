#include "test_support.h"

bool test_time_location(void)
{
    fake_reset();
    g_fake.mono_us = 1000000u;
    minishell_services_port_t p = fake_full_port();
    minishell_services_configure(&p);

    const mini_time_location_api_t *tl = mini_api_get()->time_location;
    TEST_CHECK(tl != NULL);
    TEST_CHECK((tl->capabilities & MINI_TIMELOC_CAP_UTC) != 0u);
    TEST_EQ(tl->monotonic_us(), 1000000u);

    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    TEST_EQ(tl->utc_get(&utc), MINI_ERR_NOT_READY);

    utc.unix_seconds = 100;
    utc.nanoseconds = 250000000u;
    TEST_EQ(tl->utc_set(&utc), MINI_OK);
    TEST_EQ(g_fake.utc_store_calls, 1u);
    TEST_EQ(tl->monotonic_us(), 1000000u);

    g_fake.mono_us += 1750000u;
    utc.struct_size = sizeof(utc);
    TEST_EQ(tl->utc_get(&utc), MINI_OK);
    TEST_EQ(utc.unix_seconds, 102);
    TEST_EQ(utc.nanoseconds, 0u);

    g_fake.utc_store_fail = true;
    mini_utc_time_t badset = {.struct_size = sizeof(badset), .unix_seconds = 999, .nanoseconds = 1};
    TEST_EQ(tl->utc_set(&badset), MINI_ERR_IO);
    g_fake.utc_store_fail = false;
    utc.struct_size = sizeof(utc);
    TEST_EQ(tl->utc_get(&utc), MINI_OK);
    TEST_CHECK(utc.unix_seconds < 999);

    badset.nanoseconds = 1000000000u;
    TEST_EQ(tl->utc_set(&badset), MINI_ERR_INVALID);

    uint64_t before_sleep = tl->monotonic_us();
    TEST_EQ(tl->sleep_ms(25), MINI_OK);
    TEST_EQ(tl->monotonic_us(), before_sleep + 25000u);
    TEST_EQ(tl->sleep_ms(0), MINI_OK);

    mini_geo_point_t point = {.struct_size = sizeof(point)};
    TEST_EQ(tl->location_default_get(&point), MINI_ERR_NOT_READY);
    point.latitude_e7 = 340522000;
    point.longitude_e7 = -1182437000;
    TEST_EQ(tl->location_default_set(&point), MINI_OK);
    TEST_EQ(g_fake.default_store_calls, 1u);

    mini_location_t loc = {.struct_size = sizeof(loc)};
    TEST_EQ(tl->location_get(&loc), MINI_OK);
    TEST_EQ(loc.source, MINI_LOCATION_SOURCE_DEFAULT);
    TEST_EQ(loc.latitude_e7, point.latitude_e7);
    TEST_EQ(loc.updated_monotonic_us, 0u);

    g_fake.mono_us += 5000u;
    TEST_EQ(minishell_services_live_location_update(377749000, -1224194000), MINI_OK);
    uint64_t live_stamp = g_fake.mono_us;
    g_fake.mono_us += 123456u;
    loc.struct_size = sizeof(loc);
    TEST_EQ(tl->location_get(&loc), MINI_OK);
    TEST_EQ(loc.source, MINI_LOCATION_SOURCE_LIVE);
    TEST_EQ(loc.updated_monotonic_us, live_stamp);

    mini_time_location_snapshot_t snap = {.struct_size = sizeof(snap)};
    TEST_EQ(tl->snapshot_get(&snap), MINI_OK);
    TEST_CHECK((snap.valid_fields & MINI_TIMELOC_SNAPSHOT_UTC_VALID) != 0u);
    TEST_CHECK((snap.valid_fields & MINI_TIMELOC_SNAPSHOT_LOCATION_VALID) != 0u);
    TEST_EQ(snap.monotonic_us, g_fake.mono_us);
    TEST_EQ(snap.location_source, MINI_LOCATION_SOURCE_LIVE);
    TEST_EQ(snap.location_updated_monotonic_us, live_stamp);

    minishell_services_live_location_clear();
    loc.struct_size = sizeof(loc);
    TEST_EQ(tl->location_get(&loc), MINI_OK);
    TEST_EQ(loc.source, MINI_LOCATION_SOURCE_DEFAULT);

    TEST_EQ(tl->location_default_clear(), MINI_OK);
    TEST_EQ(g_fake.default_clear_calls, 1u);
    loc.struct_size = sizeof(loc);
    TEST_EQ(tl->location_get(&loc), MINI_ERR_NOT_READY);

    point.struct_size = sizeof(point);
    point.latitude_e7 = 900000001;
    point.longitude_e7 = 0;
    TEST_EQ(tl->location_default_set(&point), MINI_ERR_INVALID);
    TEST_EQ(minishell_services_live_location_update(0, 1800000001), MINI_ERR_INVALID);

    fake_reset();
    minishell_services_port_t minimal = fake_minimal_port();
    minimal.monotonic_us = p.monotonic_us;
    minimal.sleep_ms = p.sleep_ms;
    minishell_services_configure(&minimal);
    tl = mini_api_get()->time_location;
    TEST_CHECK(tl != NULL);
    TEST_EQ(tl->capabilities, 0u);
    utc.struct_size = sizeof(utc);
    TEST_EQ(tl->utc_get(&utc), MINI_ERR_UNSUPPORTED);
    point.struct_size = sizeof(point);
    TEST_EQ(tl->location_default_get(&point), MINI_ERR_UNSUPPORTED);
    snap.struct_size = sizeof(snap);
    TEST_EQ(tl->snapshot_get(&snap), MINI_OK);
    TEST_EQ(snap.valid_fields, 0u);
    mini_utc_time_t too_small = {.struct_size = sizeof(uint32_t)};
    TEST_EQ(tl->utc_get(&too_small), MINI_ERR_UNSUPPORTED);
    return true;
}
