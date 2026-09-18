#ifndef ADV_FILESYSTEM_RENAME_H
#define ADV_FILESYSTEM_RENAME_H

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* Callbacks return errno values, not -1. rename must have FatFs no-replace
 * semantics: EEXIST leaves both names untouched, including backup collisions.
 * Callers must exclude concurrent namespace mutation; this is not crash-atomic. */
typedef struct {
    int (*rename)(const char *, const char *);
    int (*remove)(const char *);
    int (*stat)(const char *);
    void (*report)(const char *operation, const char *backup, int error);
} adv_rename_ops_t;

static int adv_rename_replace(const adv_rename_ops_t *ops, const char *source,
                              const char *destination, bool same_volume)
{
    if (!same_volume) return EXDEV;
    int error = ops->rename(source, destination);
    if (error != EEXIST) return error;

    const char *slash = strrchr(destination, '/');
    if (slash == NULL) return EINVAL;
    size_t parent = (size_t)(slash - destination + 1);
    char backup[512];
    if (parent >= sizeof(backup)) return ENAMETOOLONG;
    memcpy(backup, destination, parent);
    for (unsigned candidate = 0; candidate < 256u; ++candidate) {
        int n = snprintf(backup + parent, sizeof(backup) - parent,
                         ".msr%04x.bak", candidate);
        if (n < 0 || (size_t)n >= sizeof(backup) - parent) return ENAMETOOLONG;
        /* FAT names are case-insensitive. Never select either operand itself. */
        if (strcasecmp(backup, source) == 0 || strcasecmp(backup, destination) == 0)
            continue;
        /* stat also catches FAT short-name aliases of either operand. The
         * no-replace rename still protects a candidate created after this check. */
        error = ops->stat(backup);
        if (error == 0) continue;
        if (error != ENOENT) return error;
        error = ops->rename(destination, backup);
        if (error == EEXIST) continue;
        if (error != 0) return error;

        error = ops->rename(source, destination);
        if (error != 0) {
            int rollback = ops->rename(backup, destination);
            if (rollback != 0) {
                ops->report("rollback failed; previous destination retained", backup, rollback);
                return EIO;
            }
            return error;
        }
        /* Installation is the commit point. Reporting failure here could cause
         * a caller to retry an already committed record. Preserve any residue. */
        int cleanup = ops->remove(backup);
        if (cleanup != 0) ops->report("committed; backup cleanup failed", backup, cleanup);
        return 0;
    }
    return EEXIST;
}
#endif
