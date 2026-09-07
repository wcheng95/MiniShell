#include "storage_service.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool storage_ready(const StorageService *storage)
{
    return storage != NULL && storage->fs != NULL;
}

bool storage_service_init(StorageService *storage, const mini_fs_api_t *fs)
{
    if (storage == NULL || fs == NULL) return false;
    if (fs->open == NULL || fs->close == NULL || fs->read == NULL ||
        fs->write == NULL || fs->sync == NULL || fs->stat == NULL ||
        fs->rename == NULL || fs->remove_file == NULL || fs->mkdir == NULL) {
        return false;
    }
    storage->fs = fs;
    return true;
}

bool storage_service_ensure_directory(StorageService *storage, const char *path)
{
    if (!storage_ready(storage) || path == NULL) return false;

    mini_fs_stat_t st = {.struct_size = sizeof(st)};
    mini_result_t result = storage->fs->stat(path, &st);
    if (result == MINI_OK) return st.type == MINI_FS_TYPE_DIRECTORY;
    if (result != MINI_ERR_NOT_FOUND) return false;

    result = storage->fs->mkdir(path);
    return result == MINI_OK || result == MINI_ERR_EXISTS;
}

bool storage_service_read_text(StorageService *storage, const char *path,
                               char *out, size_t out_size)
{
    if (!storage_ready(storage) || path == NULL || out == NULL || out_size < 2u) {
        return false;
    }

    mini_file_t file = MINI_FILE_INVALID;
    if (storage->fs->open(path, MINI_FS_READ, &file) != MINI_OK) return false;

    size_t total = 0u;
    bool ok = true;
    while (total < out_size - 1u) {
        size_t remaining = out_size - 1u - total;
        uint32_t request = remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
        uint32_t nread = 0u;
        mini_result_t result = storage->fs->read(file, out + total, request, &nread);
        if (result != MINI_OK) {
            ok = false;
            break;
        }
        if (nread == 0u) break;
        total += nread;
    }

    if (ok && total == out_size - 1u) {
        char extra = '\0';
        uint32_t nread = 0u;
        if (storage->fs->read(file, &extra, 1u, &nread) != MINI_OK || nread != 0u) {
            ok = false;
        }
    }

    if (storage->fs->close(file) != MINI_OK) ok = false;
    if (!ok) return false;

    out[total] = '\0';
    return true;
}

bool storage_service_write_text_atomic(StorageService *storage, const char *path,
                                        const char *text)
{
    if (!storage_ready(storage) || path == NULL || text == NULL) return false;

    char tmp_path[512];
    int written_path = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (written_path < 0 || (size_t)written_path >= sizeof(tmp_path)) return false;

    mini_file_t file = MINI_FILE_INVALID;
    mini_result_t result = storage->fs->open(
        tmp_path, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC, &file);
    if (result != MINI_OK) return false;

    const size_t length = strlen(text);
    size_t total = 0u;
    bool ok = true;
    while (total < length) {
        size_t remaining = length - total;
        uint32_t request = remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
        uint32_t nwritten = 0u;
        result = storage->fs->write(file, text + total, request, &nwritten);
        if (result != MINI_OK || nwritten == 0u) {
            ok = false;
            break;
        }
        total += nwritten;
    }

    if (ok && storage->fs->sync(file) != MINI_OK) ok = false;
    if (storage->fs->close(file) != MINI_OK) ok = false;

    if (!ok) {
        (void)storage->fs->remove_file(tmp_path);
        return false;
    }

    result = storage->fs->rename(tmp_path, path);
    if (result != MINI_OK) {
        (void)storage->fs->remove_file(tmp_path);
        return false;
    }
    return true;
}
