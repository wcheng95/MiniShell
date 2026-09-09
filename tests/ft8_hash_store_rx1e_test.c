#include <stdio.h>
#include <string.h>

#include "ft8_hash_store.h"

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static int lookup_equals(Ft8HashStore *store,
                         Ft8HashType type,
                         uint32_t hash,
                         const char *expected)
{
    char callsign[FT8_HASH_STORE_CALLSIGN_CAP];
    Ft8HashStoreStatus status = ft8_hash_store_lookup(store,
                                                      type,
                                                      hash,
                                                      callsign,
                                                      sizeof(callsign));
    return status == FT8_HASH_STORE_OK && strcmp(callsign, expected) == 0;
}

static int test_widths_and_independence(void)
{
    Ft8HashStore a;
    Ft8HashStore b;
    const uint32_t h22 = UINT32_C(0x2ABCDE);

    ft8_hash_store_init(&a);
    ft8_hash_store_init(&b);

    CHECK(ft8_hash_store_count(&a) == 0u);
    CHECK(ft8_hash_store_save(&a, "W1XYZ", h22) == FT8_HASH_STORE_OK);
    CHECK(ft8_hash_store_count(&a) == 1u);
    CHECK(ft8_hash_store_count(&b) == 0u);

    CHECK(lookup_equals(&a, FT8_HASH_22_BITS, h22, "W1XYZ"));
    CHECK(lookup_equals(&a, FT8_HASH_12_BITS, h22 >> 10u, "W1XYZ"));
    CHECK(lookup_equals(&a, FT8_HASH_10_BITS, h22 >> 12u, "W1XYZ"));

    {
        char out[FT8_HASH_STORE_CALLSIGN_CAP] = "dirty";
        CHECK(ft8_hash_store_lookup(&b,
                                    FT8_HASH_22_BITS,
                                    h22,
                                    out,
                                    sizeof(out)) == FT8_HASH_STORE_NOT_FOUND);
        CHECK(out[0] == '\0');
    }

    return 0;
}

static int test_same_hash_replacement(void)
{
    Ft8HashStore store;
    const uint32_t h22 = UINT32_C(0x155123);

    ft8_hash_store_init(&store);
    CHECK(ft8_hash_store_save(&store, "K1AAA", h22) == FT8_HASH_STORE_OK);
    ft8_hash_store_age_slot(&store);
    CHECK(ft8_hash_store_save(&store, "K1BBB", h22) == FT8_HASH_STORE_OK);
    CHECK(ft8_hash_store_count(&store) == 1u);
    CHECK(lookup_equals(&store, FT8_HASH_22_BITS, h22, "K1BBB"));
    return 0;
}

static int test_lookup_survives_trim_hole(void)
{
    Ft8HashStore store;
    const uint32_t old_hash = UINT32_C(0x155001);
    const uint32_t keep_hash = UINT32_C(0x155002);

    /* Both hashes have the same top 10 bits and therefore the same bucket. */
    CHECK((old_hash >> 12u) == (keep_hash >> 12u));

    ft8_hash_store_init(&store);
    CHECK(ft8_hash_store_save(&store, "OLD1", old_hash) == FT8_HASH_STORE_OK);
    ft8_hash_store_age_slot(&store);
    CHECK(ft8_hash_store_save(&store, "KEEP1", keep_hash) == FT8_HASH_STORE_OK);

    CHECK(ft8_hash_store_trim(&store, 1u) == FT8_HASH_STORE_OK);
    CHECK(ft8_hash_store_count(&store) == 1u);
    CHECK(lookup_equals(&store, FT8_HASH_22_BITS, keep_hash, "KEEP1"));
    return 0;
}

static int test_lookup_refreshes_age(void)
{
    Ft8HashStore store;
    const uint32_t a_hash = UINT32_C(0x100001);
    const uint32_t b_hash = UINT32_C(0x200001);

    ft8_hash_store_init(&store);
    CHECK(ft8_hash_store_save(&store, "A1AAA", a_hash) == FT8_HASH_STORE_OK);
    ft8_hash_store_age_slot(&store);
    ft8_hash_store_age_slot(&store);
    CHECK(ft8_hash_store_save(&store, "B1BBB", b_hash) == FT8_HASH_STORE_OK);
    ft8_hash_store_age_slot(&store);

    /* A is older, but a successful lookup refreshes A to age zero. */
    CHECK(lookup_equals(&store, FT8_HASH_22_BITS, a_hash, "A1AAA"));
    CHECK(ft8_hash_store_trim(&store, 1u) == FT8_HASH_STORE_OK);
    CHECK(lookup_equals(&store, FT8_HASH_22_BITS, a_hash, "A1AAA"));

    {
        char out[FT8_HASH_STORE_CALLSIGN_CAP];
        CHECK(ft8_hash_store_lookup(&store,
                                    FT8_HASH_22_BITS,
                                    b_hash,
                                    out,
                                    sizeof(out)) == FT8_HASH_STORE_NOT_FOUND);
    }

    return 0;
}

static int test_full_table_v2_trim_policy(void)
{
    Ft8HashStore store;
    char callsign[FT8_HASH_STORE_CALLSIGN_CAP];

    ft8_hash_store_init(&store);

    for (unsigned i = 0u; i < FT8_HASH_STORE_CAPACITY; ++i) {
        uint32_t hash22 = ((uint32_t)i << 12u) | (uint32_t)i;
        snprintf(callsign, sizeof(callsign), "C%03u", i);
        CHECK(ft8_hash_store_save(&store, callsign, hash22) == FT8_HASH_STORE_OK);
    }
    CHECK(ft8_hash_store_count(&store) == FT8_HASH_STORE_CAPACITY);

    ft8_hash_store_age_slot(&store);
    CHECK(ft8_hash_store_save(&store,
                              "NEWCALL",
                              UINT32_C(0x3FF777)) == FT8_HASH_STORE_OK);

    /* V2 trims 50 entries when insertion finds the 128-entry table full. */
    CHECK(ft8_hash_store_count(&store) == 79u);
    CHECK(lookup_equals(&store,
                        FT8_HASH_22_BITS,
                        UINT32_C(0x3FF777),
                        "NEWCALL"));
    return 0;
}

static int test_validation(void)
{
    Ft8HashStore store;
    char out[FT8_HASH_STORE_CALLSIGN_CAP];

    ft8_hash_store_init(&store);
    CHECK(ft8_hash_store_save(NULL, "W1XYZ", 1u) == FT8_HASH_STORE_ERR_INVALID);
    CHECK(ft8_hash_store_save(&store, "", 1u) == FT8_HASH_STORE_ERR_INVALID);
    CHECK(ft8_hash_store_save(&store,
                              "TOO-LONG-CALL",
                              1u) == FT8_HASH_STORE_ERR_INVALID);
    CHECK(ft8_hash_store_save(&store,
                              "W1XYZ",
                              UINT32_C(0x400000)) == FT8_HASH_STORE_ERR_INVALID);
    CHECK(ft8_hash_store_lookup(&store,
                                (Ft8HashType)11,
                                1u,
                                out,
                                sizeof(out)) == FT8_HASH_STORE_ERR_INVALID);
    CHECK(ft8_hash_store_lookup(&store,
                                FT8_HASH_10_BITS,
                                UINT32_C(0x400),
                                out,
                                sizeof(out)) == FT8_HASH_STORE_ERR_INVALID);
    CHECK(ft8_hash_store_trim(&store,
                              FT8_HASH_STORE_CAPACITY + 1u) == FT8_HASH_STORE_ERR_INVALID);
    return 0;
}

int main(void)
{
    CHECK(test_widths_and_independence() == 0);
    CHECK(test_same_hash_replacement() == 0);
    CHECK(test_lookup_survives_trim_hole() == 0);
    CHECK(test_lookup_refreshes_age() == 0);
    CHECK(test_full_table_v2_trim_policy() == 0);
    CHECK(test_validation() == 0);

    puts("ft8_hash_store_rx1e_test: PASS");
    return 0;
}
