#include "adv_filesystem_rename.h"
#include "storage_service.h"

#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
static struct { char path[512]; char data[128]; } files[260];
static int faults[8], calls, removes, reports, remove_error, stat_error, syncs, closes;
static const char *src = "/flash/ft8/station.txt.tmp";
static const char *dst = "/flash/ft8/station.txt";
static const char *bak = "/flash/ft8/.msr0000.bak";

static int find(const char *path)
{
    for (int i = 0; i < 260; ++i)
        if (files[i].path[0] && strcasecmp(files[i].path, path) == 0) return i;
    return -1;
}
static int put(const char *path, const char *data)
{
    int i = find(path);
    if (i < 0) for (i = 0; i < 260 && files[i].path[0]; ++i) {}
    CHECK(i < 260);
    snprintf(files[i].path, sizeof(files[i].path), "%s", path);
    snprintf(files[i].data, sizeof(files[i].data), "%s", data);
    return i;
}
static void content(const char *path, const char *data)
{
    int i = find(path);
    CHECK(i >= 0 && strcmp(files[i].data, data) == 0);
}
static int rename_fake(const char *a, const char *b)
{
    ++calls;
    if (calls < 8 && faults[calls]) return faults[calls];
    int i = find(a);
    if (i < 0) return ENOENT;
    if (find(b) >= 0) return EEXIST;
    snprintf(files[i].path, sizeof(files[i].path), "%s", b);
    return 0;
}
static int remove_fake(const char *path)
{
    ++removes;
    if (remove_error) return remove_error;
    int i = find(path);
    if (i < 0) return ENOENT;
    files[i].path[0] = '\0';
    return 0;
}
static void report_fake(const char *operation, const char *backup, int error)
{
    CHECK(operation != NULL && find(backup) >= 0 && error != 0);
    ++reports;
}
static int stat_fake(const char *path)
{
    return stat_error ? stat_error : (find(path) >= 0 ? 0 : ENOENT);
}
static const adv_rename_ops_t ops = {rename_fake, remove_fake, stat_fake, report_fake};
static void reset(bool destination)
{
    memset(files, 0, sizeof(files));
    memset(faults, 0, sizeof(faults));
    calls = removes = reports = remove_error = stat_error = syncs = closes = 0;
    put(src, "new");
    if (destination) put(dst, "previous");
}
static int replace(void) { return adv_rename_replace(&ops, src, dst, true); }

/* Public Filesystem-shaped adapter reaches the actual ADV helper with FatFs
 * no-replace semantics; the production FT8 shared safe-save code is unchanged. */
static mini_result_t save_open(const char *path, uint32_t flags, mini_file_t *file)
{
    CHECK(flags == (MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC));
    *file = (mini_file_t)(put(path, "") + 1);
    return MINI_OK;
}
static mini_result_t save_write(mini_file_t file, const void *buf, uint32_t size, uint32_t *written)
{
    CHECK(file > 0 && file <= 260);
    size_t length = strlen(files[file - 1].data);
    CHECK(length + size < sizeof(files[0].data));
    memcpy(files[file - 1].data + length, buf, size);
    files[file - 1].data[length + size] = '\0';
    *written = size;
    return MINI_OK;
}
static mini_result_t save_sync(mini_file_t file) { CHECK(file > 0); ++syncs; return MINI_OK; }
static mini_result_t save_close(mini_file_t file) { CHECK(file > 0); ++closes; return MINI_OK; }
static mini_result_t save_rename(const char *a, const char *b)
{
    CHECK(syncs == closes && syncs > 0);
    return adv_rename_replace(&ops, a, b, true) == 0 ? MINI_OK : MINI_ERR_IO;
}
static mini_result_t save_remove(const char *path) { return remove_fake(path) == 0 ? MINI_OK : MINI_ERR_IO; }

int main(void)
{
    reset(false);
    CHECK(replace() == 0 && calls == 1 && removes == 0);
    content(dst, "new"); CHECK(find(src) < 0);
    reset(true);
    CHECK(replace() == 0 && calls == 3 && removes == 1);
    content(dst, "new"); CHECK(find(src) < 0 && find(bak) < 0);
    reset(true); put(bak, "unrelated");
    CHECK(replace() == 0 && calls == 3 && removes == 1);
    content(bak, "unrelated"); content(dst, "new");
    CHECK(find("/flash/ft8/.msr0001.bak") < 0);
    reset(true); faults[2] = EEXIST; /* candidate appeared after stat */
    CHECK(replace() == 0 && calls == 4 && removes == 1);
    content(dst, "new"); CHECK(find(bak) < 0);
    reset(true); faults[1] = EACCES;
    CHECK(replace() == EACCES && calls == 1 && removes == 0);
    content(dst, "previous"); content(src, "new");
    reset(true); stat_error = EIO;
    CHECK(replace() == EIO && calls == 1 && removes == 0);
    content(dst, "previous"); content(src, "new");
    reset(true); faults[2] = ENOSPC;
    CHECK(replace() == ENOSPC && calls == 2 && removes == 0);
    content(dst, "previous"); content(src, "new");
    reset(true); faults[3] = EACCES;
    CHECK(replace() == EACCES && calls == 4 && removes == 0);
    content(dst, "previous"); content(src, "new"); CHECK(find(bak) < 0);
    reset(true); faults[3] = EACCES; faults[4] = EROFS;
    CHECK(replace() == EIO && reports == 1 && removes == 0);
    CHECK(find(dst) < 0); content(bak, "previous"); content(src, "new");
    reset(true); remove_error = EIO;
    CHECK(replace() == 0 && reports == 1);
    content(dst, "new"); content(bak, "previous");
    reset(true);
    CHECK(adv_rename_replace(&ops, src, "/sd/station.txt", false) == EXDEV && calls == 0);
    content(dst, "previous"); content(src, "new");
    reset(true);
    for (unsigned i = 0; i < 256; ++i) {
        char path[80]; snprintf(path, sizeof(path), "/flash/ft8/.msr%04x.bak", i);
        put(path, "unrelated");
    }
    CHECK(replace() == EEXIST && removes == 0);
    content(dst, "previous"); content(src, "new"); content(bak, "unrelated");
    reset(true); put("/flash/ft8/.MSR0000.BAK", "alias");
    CHECK(adv_rename_replace(&ops, src, "/flash/ft8/.MSR0000.BAK", true) == 0);
    content("/flash/ft8/.MSR0000.BAK", "new");
    reset(true); put(bak, "source");
    CHECK(adv_rename_replace(&ops, bak, dst, true) == 0);
    content(dst, "source"); CHECK(find(bak) < 0);
    reset(true);
    char long_path[520]; memset(long_path, 'x', sizeof(long_path));
    long_path[0] = '/'; long_path[505] = '/'; long_path[519] = '\0';
    faults[1] = EEXIST;
    CHECK(adv_rename_replace(&ops, src, long_path, true) == ENAMETOOLONG && calls == 1);
    content(dst, "previous");

    const mini_fs_api_t fs = {.open = save_open, .write = save_write, .sync = save_sync,
        .close = save_close, .rename = save_rename, .remove_file = save_remove};
    StorageService storage = {.fs = &fs};
    reset(true);
    CHECK(storage_service_write_text_atomic(&storage, dst, "skip_tx1=1\n"));
    content(dst, "skip_tx1=1\n"); CHECK(find(src) < 0 && find(bak) < 0);
    calls = 0;
    CHECK(storage_service_write_text_atomic(&storage, dst, "skip_tx1=0\n"));
    content(dst, "skip_tx1=0\n"); CHECK(find(src) < 0 && find(bak) < 0);
    calls = 0; faults[3] = EIO;
    CHECK(!storage_service_write_text_atomic(&storage, dst, "skip_tx1=1\n"));
    content(dst, "skip_tx1=0\n"); CHECK(find(src) < 0 && find(bak) < 0);
    puts("ADV rename replacement and FT8 shared safe-save: PASS");
    return 0;
}
