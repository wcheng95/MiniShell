#include "ft8_hash_store.h"

#include <string.h>

#define FT8_HASH22_MASK UINT32_C(0x003FFFFF)
#define FT8_HASH12_MASK UINT32_C(0x00000FFF)
#define FT8_HASH10_MASK UINT32_C(0x000003FF)
#define FT8_HASH_AGE_SHIFT 24u
#define FT8_HASH_STORE_FULL_TRIM_COUNT 50u
#define FT8_HASH_BUCKET_MULTIPLIER 23u

_Static_assert(sizeof(Ft8HashStoreEntry) == 16u,
               "Ft8HashStoreEntry must remain compact");

static uint32_t entry_hash22(const Ft8HashStoreEntry *entry)
{
    return entry->hash_age & FT8_HASH22_MASK;
}

static uint8_t entry_age(const Ft8HashStoreEntry *entry)
{
    return (uint8_t)(entry->hash_age >> FT8_HASH_AGE_SHIFT);
}

static void entry_set(Ft8HashStoreEntry *entry,
                      const char *callsign,
                      uint32_t hash22)
{
    size_t length = strlen(callsign);
    memcpy(entry->callsign, callsign, length + 1u);
    entry->hash_age = hash22 & FT8_HASH22_MASK;
}

static void entry_clear(Ft8HashStoreEntry *entry)
{
    entry->callsign[0] = '\0';
    entry->hash_age = 0u;
}

static uint16_t bucket_from_hash10(uint16_t hash10)
{
    return (uint16_t)(((uint32_t)hash10 * FT8_HASH_BUCKET_MULTIPLIER) %
                      FT8_HASH_STORE_CAPACITY);
}

static int hash_type_params(Ft8HashType type,
                            uint8_t *out_shift,
                            uint32_t *out_mask)
{
    switch (type) {
    case FT8_HASH_22_BITS:
        *out_shift = 0u;
        *out_mask = FT8_HASH22_MASK;
        return 1;
    case FT8_HASH_12_BITS:
        *out_shift = 10u;
        *out_mask = FT8_HASH12_MASK;
        return 1;
    case FT8_HASH_10_BITS:
        *out_shift = 12u;
        *out_mask = FT8_HASH10_MASK;
        return 1;
    default:
        return 0;
    }
}

static uint16_t lookup_hash10(Ft8HashType type, uint32_t hash)
{
    switch (type) {
    case FT8_HASH_10_BITS:
        return (uint16_t)(hash & FT8_HASH10_MASK);
    case FT8_HASH_12_BITS:
        return (uint16_t)((hash >> 2u) & FT8_HASH10_MASK);
    case FT8_HASH_22_BITS:
        return (uint16_t)((hash >> 12u) & FT8_HASH10_MASK);
    default:
        return 0u;
    }
}

void ft8_hash_store_init(Ft8HashStore *store)
{
    ft8_hash_store_clear(store);
}

void ft8_hash_store_clear(Ft8HashStore *store)
{
    if (!store)
        return;
    memset(store, 0, sizeof(*store));
}

void ft8_hash_store_age_slot(Ft8HashStore *store)
{
    size_t i;

    if (!store)
        return;

    for (i = 0u; i < FT8_HASH_STORE_CAPACITY; ++i) {
        Ft8HashStoreEntry *entry = &store->entries[i];
        uint8_t age;

        if (entry->callsign[0] == '\0')
            continue;

        age = entry_age(entry);
        if (age < UINT8_MAX) {
            ++age;
            entry->hash_age = ((uint32_t)age << FT8_HASH_AGE_SHIFT) |
                              entry_hash22(entry);
        }
    }
}

Ft8HashStoreStatus ft8_hash_store_trim(Ft8HashStore *store,
                                       size_t max_entries)
{
    if (!store || max_entries > FT8_HASH_STORE_CAPACITY)
        return FT8_HASH_STORE_ERR_INVALID;

    while ((size_t)store->count > max_entries) {
        int oldest_index = -1;
        uint8_t oldest_age = 0u;
        size_t i;

        for (i = 0u; i < FT8_HASH_STORE_CAPACITY; ++i) {
            const Ft8HashStoreEntry *entry = &store->entries[i];
            uint8_t age;

            if (entry->callsign[0] == '\0')
                continue;

            age = entry_age(entry);
            if (oldest_index < 0 || age > oldest_age) {
                oldest_index = (int)i;
                oldest_age = age;
            }
        }

        if (oldest_index < 0) {
            store->count = 0u;
            break;
        }

        entry_clear(&store->entries[oldest_index]);
        --store->count;
    }

    return FT8_HASH_STORE_OK;
}

Ft8HashStoreStatus ft8_hash_store_save(Ft8HashStore *store,
                                       const char *callsign,
                                       uint32_t hash22)
{
    uint16_t index;
    uint16_t start_index;
    size_t length;

    if (!store || !callsign || callsign[0] == '\0' ||
        hash22 > FT8_HASH22_MASK)
        return FT8_HASH_STORE_ERR_INVALID;

    length = strlen(callsign);
    if (length > FT8_HASH_STORE_CALLSIGN_MAX)
        return FT8_HASH_STORE_ERR_INVALID;

    if (store->count >= FT8_HASH_STORE_CAPACITY) {
        size_t target = FT8_HASH_STORE_CAPACITY - FT8_HASH_STORE_FULL_TRIM_COUNT;
        Ft8HashStoreStatus status = ft8_hash_store_trim(store, target);
        if (status != FT8_HASH_STORE_OK)
            return status;
        if (store->count >= FT8_HASH_STORE_CAPACITY)
            return FT8_HASH_STORE_ERR_FULL;
    }

    index = bucket_from_hash10((uint16_t)((hash22 >> 12u) & FT8_HASH10_MASK));
    start_index = index;

    while (store->entries[index].callsign[0] != '\0') {
        Ft8HashStoreEntry *entry = &store->entries[index];
        uint32_t existing_hash = entry_hash22(entry);

        if (existing_hash == hash22) {
            if (strcmp(entry->callsign, callsign) == 0) {
                entry->hash_age = hash22;
                return FT8_HASH_STORE_OK;
            }

            entry_set(entry, callsign, hash22);
            return FT8_HASH_STORE_OK;
        }

        index = (uint16_t)((index + 1u) % FT8_HASH_STORE_CAPACITY);
        if (index == start_index)
            return FT8_HASH_STORE_ERR_FULL;
    }

    entry_set(&store->entries[index], callsign, hash22);
    ++store->count;
    return FT8_HASH_STORE_OK;
}

Ft8HashStoreStatus ft8_hash_store_lookup(Ft8HashStore *store,
                                         Ft8HashType type,
                                         uint32_t hash,
                                         char *out_callsign,
                                         size_t out_capacity)
{
    uint8_t shift = 0u;
    uint32_t mask = 0u;
    uint16_t start;
    size_t probe;

    if (out_callsign && out_capacity > 0u)
        out_callsign[0] = '\0';

    if (!store || !out_callsign || out_capacity == 0u ||
        !hash_type_params(type, &shift, &mask) || hash > mask)
        return FT8_HASH_STORE_ERR_INVALID;

    start = bucket_from_hash10(lookup_hash10(type, hash));

    /*
     * Trim deliberately leaves holes in probe chains. V2 production lookup
     * scans the complete table so shortened/full hash lookups remain valid
     * after eviction; preserve that behavior here.
     */
    for (probe = 0u; probe < FT8_HASH_STORE_CAPACITY; ++probe) {
        uint16_t index = (uint16_t)((start + probe) % FT8_HASH_STORE_CAPACITY);
        Ft8HashStoreEntry *entry = &store->entries[index];
        uint32_t existing_hash;
        size_t length;

        if (entry->callsign[0] == '\0')
            continue;

        existing_hash = entry_hash22(entry);
        if ((existing_hash >> shift) != hash)
            continue;

        length = strlen(entry->callsign);
        if (length + 1u > out_capacity)
            return FT8_HASH_STORE_ERR_INVALID;

        memcpy(out_callsign, entry->callsign, length + 1u);
        entry->hash_age = existing_hash;
        return FT8_HASH_STORE_OK;
    }

    return FT8_HASH_STORE_NOT_FOUND;
}

size_t ft8_hash_store_count(const Ft8HashStore *store)
{
    return store ? (size_t)store->count : 0u;
}
