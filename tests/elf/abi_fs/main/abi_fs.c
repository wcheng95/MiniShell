#include "abi_test.h"

static mini_result_t write_all(const mini_fs_api_t *fs,
                               mini_file_t file,
                               const uint8_t *data,
                               uint32_t size)
{
    uint32_t total = 0u;
    while (total < size) {
        uint32_t n = 0u;
        mini_result_t r = fs->write(file, data + total, size - total, &n);
        if (r != MINI_OK) return r;
        if (n == 0u) return MINI_ERR_IO;
        total += n;
    }
    return MINI_OK;
}

static mini_result_t read_exact(const mini_fs_api_t *fs,
                                mini_file_t file,
                                uint8_t *data,
                                uint32_t size)
{
    uint32_t total = 0u;
    while (total < size) {
        uint32_t n = 0u;
        mini_result_t r = fs->read(file, data + total, size - total, &n);
        if (r != MINI_OK) return r;
        if (n == 0u) return MINI_ERR_IO;
        total += n;
    }
    return MINI_OK;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = NULL;
    int rc = abi_test_base(&api);
    if (rc != 0) return rc;

    if (!ABI_HAS_API_FIELD(api, fs) || api->fs == NULL)
        return abi_test_fail(api, "abi_fs", "service unavailable", 20);

    const mini_fs_api_t *fs = api->fs;
    if (fs->struct_size < ABI_FIELD_END(mini_fs_api_t, stat) ||
        fs->open == NULL || fs->close == NULL || fs->read == NULL ||
        fs->write == NULL || fs->seek == NULL || fs->sync == NULL || fs->stat == NULL)
        return abi_test_fail(api, "abi_fs", "v0 table incomplete", 21);

    static const char path[] = "/sd/abi_fs_test.tmp";
    static const uint8_t payload[] = "MiniShell Filesystem ABI";
    uint8_t readback[sizeof(payload) - 1u];
    mini_file_t file = MINI_FILE_INVALID;

    if (fs->open(path, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC, &file) != MINI_OK)
        return abi_test_fail(api, "abi_fs", "create/open failed", 22);

    if (write_all(fs, file, payload, sizeof(payload) - 1u) != MINI_OK ||
        fs->sync(file) != MINI_OK || fs->close(file) != MINI_OK)
        return abi_test_fail(api, "abi_fs", "write/sync/close failed", 23);

    mini_fs_stat_t st = {0};
    st.struct_size = sizeof(st);
    if (fs->stat(path, &st) != MINI_OK || st.type != MINI_FS_TYPE_FILE ||
        st.size != (uint64_t)(sizeof(payload) - 1u))
        return abi_test_fail(api, "abi_fs", "stat mismatch", 24);

    if (fs->open(path, MINI_FS_READ, &file) != MINI_OK)
        return abi_test_fail(api, "abi_fs", "read open failed", 25);

    if (read_exact(fs, file, readback, sizeof(readback)) != MINI_OK)
        return abi_test_fail(api, "abi_fs", "readback failed", 26);

    for (uint32_t i = 0; i < sizeof(readback); ++i) {
        if (readback[i] != payload[i])
            return abi_test_fail(api, "abi_fs", "readback mismatch", 27);
    }

    uint8_t extra = 0u;
    uint32_t n = 99u;
    if (fs->read(file, &extra, 1u, &n) != MINI_OK || n != 0u)
        return abi_test_fail(api, "abi_fs", "EOF contract", 28);

    if (fs->close(file) != MINI_OK || fs->close(file) != MINI_ERR_BAD_HANDLE)
        return abi_test_fail(api, "abi_fs", "handle invalidation", 29);

    abi_test_line(api, "abi_fs", "PASS");
    return 0;
}
