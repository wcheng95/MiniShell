#include "js8_compound.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct compound_vector {
    const char *payload;
    unsigned app_class;
    const char *call;
    unsigned extra, subtype;
    const char *grid;
};
struct call_vector { uint64_t packed; const char *call; };
struct grid_vector { uint16_t packed; const char *grid; };
#include "js8_compound_vectors.h"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void unpack(const char *text, uint8_t bits[75])
{
    assert(strlen(text) == 75);
    for (unsigned i = 0; i < 75; ++i) bits[i] = (uint8_t)(text[i] - '0');
}

static void set_bits(uint8_t *bits, unsigned count, unsigned value)
{
    for (unsigned i = 0; i < count; ++i) bits[i] = (value >> (count-1-i)) & 1;
}

static void unchanged(const void *out, size_t size)
{
    const unsigned char *p = out;
    for (size_t i = 0; i < size; ++i) assert(p[i] == 0xa5);
}

#define REJECT(fn, bits, obj) do { \
    memset(&(obj), 0xa5, sizeof(obj)); \
    assert(fn(bits, &(obj)) == -1); \
    unchanged(&(obj), sizeof(obj)); \
} while (0)

int main(void)
{
    char call[12], grid[5];
    for (unsigned i = 0; i < COUNT(call_vectors); ++i) {
        memset(call, 0xa5, sizeof(call));
        assert(js8_callsign50_unpack(call_vectors[i].packed, call) == 0);
        assert(!strcmp(call, call_vectors[i].call));
    }
    for (unsigned i = 0; i < COUNT(grid_vectors); ++i) {
        assert(js8_grid_unpack(grid_vectors[i].packed, grid) == 0);
        assert(!strcmp(grid, grid_vectors[i].grid));
    }
    memset(call, 0xa5, sizeof(call));
    assert(js8_callsign50_unpack(UINT64_C(1) << 50, call) == -1);
    unchanged(call, sizeof(call));
    assert(js8_callsign50_unpack(UINT64_MAX, call) == -1);
    unchanged(call, sizeof(call));
    assert(js8_callsign50_unpack(0, NULL) == -1);
    assert(js8_grid_unpack(0, NULL) == -1);

    uint8_t bits[75], saved[75];
    Js8CompoundFields raw;
    Js8BeaconFrame beacon;
    Js8CompoundIdentity identity;
    for (unsigned i = 0; i < COUNT(compound_vectors); ++i) {
        const struct compound_vector *v = &compound_vectors[i];
        unpack(v->payload, bits);
        for (unsigned tx = 0; tx < 8; ++tx) {
            set_bits(bits + 72, 3, tx);
            memcpy(saved, bits, sizeof(bits));
            assert(js8_compound_fields_decode(bits, &raw) == 0);
            assert((unsigned)raw.app_class == v->app_class);
            assert(!strcmp(raw.callsign, v->call));
            assert(raw.extra16 == v->extra && raw.bits3 == v->subtype);
            if (v->app_class == 0) {
                assert(js8_beacon_decode(bits, &beacon) == 0);
                assert(!strcmp(beacon.callsign, v->call) && !strcmp(beacon.grid, v->grid));
                assert(beacon.has_grid == (v->grid[0] != '\0'));
                assert((unsigned)beacon.is_cq == (v->extra >> 15) && beacon.subtype == v->subtype);
            } else {
                REJECT(js8_beacon_decode, bits, beacon);
            }
            if (v->app_class == 1) {
                assert(js8_compound_identity_decode(bits, &identity) == 0);
                assert(!strcmp(identity.callsign, v->call) && !strcmp(identity.grid, v->grid));
                assert(identity.has_grid == (v->grid[0] != '\0'));
                assert(identity.extra16 == v->extra && identity.bits3 == v->subtype);
            } else {
                REJECT(js8_compound_identity_decode, bits, identity);
            }
            assert(!memcmp(saved, bits, sizeof(bits)));
        }
    }
    static const char *const cq_names[] = {
        "CQ CQ CQ", "CQ DX", "CQ QRP", "CQ CONTEST", "CQ FIELD", "CQ FD", "CQ CQ", "CQ"
    };
    unpack(compound_vectors[0].payload, bits);
    for (unsigned subtype = 0; subtype < 8; ++subtype) {
        set_bits(bits + 69, 3, subtype);
        for (unsigned i = 0; i < COUNT(grid_vectors); ++i) {
            unsigned value = grid_vectors[i].packed;
            set_bits(bits, 3, 1);
            set_bits(bits + 53, 16, value);
            assert(js8_compound_identity_decode(bits, &identity) == 0);
            assert(!strcmp(identity.grid, grid_vectors[i].grid));
            assert(identity.has_grid == (value <= 32400));
            assert(identity.extra16 == value && identity.bits3 == subtype);
            if (value > 32767) continue;
            for (unsigned cq = 0; cq < 2; ++cq) {
                set_bits(bits, 3, 0);
                set_bits(bits + 53, 16, value | (cq << 15));
                assert(js8_beacon_decode(bits, &beacon) == 0);
                assert(beacon.subtype == subtype && (unsigned)beacon.is_cq == cq);
                assert(!strcmp(beacon.grid, grid_vectors[i].grid));
                assert(beacon.has_grid == (value <= 32400));
                assert(!strcmp(js8_beacon_name(beacon.is_cq, beacon.subtype), cq ? cq_names[subtype] : "HB"));
            }
        }
    }
    assert(!strcmp(js8_beacon_name(-1, 0), "INVALID"));
    assert(!strcmp(js8_beacon_name(2, 0), "INVALID"));
    for (unsigned i = 8; i < 256; ++i)
        assert(!strcmp(js8_beacon_name(0, (uint8_t)i), "INVALID"));
    for (unsigned prefix = 3; prefix < 8; ++prefix) {
        set_bits(bits, 3, prefix);
        REJECT(js8_compound_fields_decode, bits, raw);
        REJECT(js8_beacon_decode, bits, beacon);
        REJECT(js8_compound_identity_decode, bits, identity);
    }
    REJECT(js8_compound_fields_decode, NULL, raw);
    REJECT(js8_beacon_decode, NULL, beacon);
    REJECT(js8_compound_identity_decode, NULL, identity);
    assert(js8_compound_fields_decode(bits, NULL) == -1);
    assert(js8_beacon_decode(bits, NULL) == -1);
    assert(js8_compound_identity_decode(bits, NULL) == -1);
    for (unsigned cls = 0; cls < 3; ++cls) {
        unpack(compound_vectors[0].payload, bits);
        set_bits(bits, 3, cls);
        for (unsigned i = 0; i < 75; ++i) {
            uint8_t original = bits[i];
            for (unsigned bad = 2; bad < 256; ++bad) {
                bits[i] = (uint8_t)bad;
                REJECT(js8_compound_fields_decode, bits, raw);
                REJECT(js8_beacon_decode, bits, beacon);
                REJECT(js8_compound_identity_decode, bits, identity);
            }
            bits[i] = original;
        }
    }
    /* Empty and out-of-conventional-range callsigns are not rejected upstream. */
    memset(bits, 0, sizeof(bits));
    assert(js8_beacon_decode(bits, &beacon) == 0);
    assert(!strcmp(beacon.callsign, "000000000") && !strcmp(beacon.grid, "RA90"));
    memset(bits + 3, 1, 50);
    assert(js8_beacon_decode(bits, &beacon) == 0);
    assert(!strcmp(beacon.callsign, "PS4F5BSV5"));
    puts("js8_compound_test: PASS");
    return 0;
}
