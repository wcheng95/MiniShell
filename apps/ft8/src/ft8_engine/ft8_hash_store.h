#ifndef FT8_HASH_STORE_H
#define FT8_HASH_STORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT8_HASH_STORE_CAPACITY 128u
#define FT8_HASH_STORE_CALLSIGN_MAX 11u
#define FT8_HASH_STORE_CALLSIGN_CAP (FT8_HASH_STORE_CALLSIGN_MAX + 1u)

typedef enum {
    FT8_HASH_22_BITS = 22,
    FT8_HASH_12_BITS = 12,
    FT8_HASH_10_BITS = 10
} Ft8HashType;

typedef enum {
    FT8_HASH_STORE_OK = 0,
    FT8_HASH_STORE_NOT_FOUND = 1,
    FT8_HASH_STORE_ERR_INVALID = -1,
    FT8_HASH_STORE_ERR_FULL = -2
} Ft8HashStoreStatus;

/*
 * Compact V2-compatible storage. The upper byte stores age and the lower
 * 22 bits store the canonical FT8 callsign hash. Bits 22..23 are unused.
 */
typedef struct {
    char callsign[FT8_HASH_STORE_CALLSIGN_CAP];
    uint32_t hash_age;
} Ft8HashStoreEntry;

typedef struct {
    Ft8HashStoreEntry entries[FT8_HASH_STORE_CAPACITY];
    uint16_t count;
} Ft8HashStore;

void ft8_hash_store_init(Ft8HashStore *store);
void ft8_hash_store_clear(Ft8HashStore *store);

/* Explicit lifecycle hook: call once for each FT8 slot to age knowledge. */
void ft8_hash_store_age_slot(Ft8HashStore *store);

/* Save a complete 22-bit FT8 callsign hash and refresh its age to zero. */
Ft8HashStoreStatus ft8_hash_store_save(Ft8HashStore *store,
                                       const char *callsign,
                                       uint32_t hash22);

/*
 * Resolve a 22-, 12-, or 10-bit FT8 callsign hash. A successful lookup
 * refreshes the entry age to zero. out_callsign always receives a C string
 * when a nonzero output capacity is supplied.
 */
Ft8HashStoreStatus ft8_hash_store_lookup(Ft8HashStore *store,
                                         Ft8HashType type,
                                         uint32_t hash,
                                         char *out_callsign,
                                         size_t out_capacity);

/* Explicit bounded eviction helper used by lifecycle/resource policy. */
Ft8HashStoreStatus ft8_hash_store_trim(Ft8HashStore *store,
                                       size_t max_entries);

size_t ft8_hash_store_count(const Ft8HashStore *store);

#ifdef __cplusplus
}
#endif

#endif
