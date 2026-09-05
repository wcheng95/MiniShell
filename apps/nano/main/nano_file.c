#include "nano_file.h"

#include <stddef.h>
#include <stdint.h>

static bool text_byte_allowed(uint8_t byte)
{
    return byte == '\n' || byte == '\r' || byte == '\t' ||
           (byte >= 0x20u && byte <= 0x7eu);
}

static int close_with_result(const mini_fs_api_t *fs, mini_file_t file, int result)
{
    mini_result_t close_result = fs->close(file);
    if (result == NANO_FILE_OK && close_result != MINI_OK) return NANO_FILE_ERR_IO;
    return result;
}

int nano_file_load(const mini_memory_api_t *memory,
                   const mini_fs_api_t *fs,
                   const char *path,
                   nano_buffer_t *buffer)
{
    if (memory == NULL || fs == NULL || path == NULL || buffer == NULL) {
        return NANO_FILE_ERR_PATH;
    }

    mini_fs_stat_t stat_info = {
        .struct_size = sizeof(mini_fs_stat_t),
    };
    mini_result_t result = fs->stat(path, &stat_info);
    if (result == MINI_ERR_NOT_FOUND) {
        return nano_buffer_assign(buffer, NULL, 0u) ? NANO_FILE_NEW : NANO_FILE_ERR_MEMORY;
    }
    if (result != MINI_OK) return NANO_FILE_ERR_IO;
    if (stat_info.type != MINI_FS_TYPE_FILE) return NANO_FILE_ERR_TYPE;
    if (stat_info.size > NANO_MAX_BYTES) return NANO_FILE_ERR_TOO_LARGE;

    uint32_t size = (uint32_t)stat_info.size;
    if (size == 0u) {
        return nano_buffer_assign(buffer, NULL, 0u) ? NANO_FILE_OK : NANO_FILE_ERR_MEMORY;
    }

    void *allocation = NULL;
    if (memory->alloc(size, &allocation) != MINI_OK || allocation == NULL) {
        return NANO_FILE_ERR_MEMORY;
    }
    uint8_t *data = (uint8_t *)allocation;

    mini_file_t file = MINI_FILE_INVALID;
    result = fs->open(path, MINI_FS_READ, &file);
    if (result != MINI_OK) {
        (void)memory->free(allocation);
        return NANO_FILE_ERR_IO;
    }

    uint32_t total = 0u;
    int load_result = NANO_FILE_OK;
    while (total < size) {
        uint32_t count = 0u;
        result = fs->read(file, data + total, size - total, &count);
        if (result != MINI_OK || count == 0u) {
            load_result = NANO_FILE_ERR_IO;
            break;
        }
        total += count;
    }
    load_result = close_with_result(fs, file, load_result);

    if (load_result == NANO_FILE_OK) {
        uint32_t out = 0u;
        for (uint32_t i = 0u; i < total; ++i) {
            uint8_t byte = data[i];
            if (!text_byte_allowed(byte)) {
                load_result = NANO_FILE_ERR_TEXT;
                break;
            }
            if (byte == '\r') {
                if (i + 1u < total && data[i + 1u] == '\n') ++i;
                data[out++] = '\n';
            } else {
                data[out++] = byte;
            }
        }
        if (load_result == NANO_FILE_OK &&
            !nano_buffer_assign(buffer, (const char *)data, out)) {
            load_result = NANO_FILE_ERR_MEMORY;
        }
    }

    (void)memory->free(allocation);
    return load_result;
}

int nano_file_save(const mini_fs_api_t *fs,
                   const char *path,
                   nano_buffer_t *buffer)
{
    if (fs == NULL || path == NULL || buffer == NULL) return NANO_FILE_ERR_PATH;

    mini_file_t file = MINI_FILE_INVALID;
    mini_result_t result = fs->open(path,
                                    MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC,
                                    &file);
    if (result != MINI_OK) return NANO_FILE_ERR_IO;

    uint32_t offset = 0u;
    int save_result = NANO_FILE_OK;
    while (offset < buffer->length) {
        uint32_t written = 0u;
        result = fs->write(file,
                           buffer->data + offset,
                           buffer->length - offset,
                           &written);
        if (result != MINI_OK || written == 0u) {
            save_result = NANO_FILE_ERR_IO;
            break;
        }
        offset += written;
    }

    if (save_result == NANO_FILE_OK && fs->sync(file) != MINI_OK) {
        save_result = NANO_FILE_ERR_IO;
    }
    save_result = close_with_result(fs, file, save_result);

    if (save_result == NANO_FILE_OK) nano_buffer_mark_clean(buffer);
    return save_result;
}

const char *nano_file_result_text(int result)
{
    switch (result) {
    case NANO_FILE_OK: return "OK";
    case NANO_FILE_NEW: return "new file";
    case NANO_FILE_ERR_PATH: return "invalid path";
    case NANO_FILE_ERR_TYPE: return "not a regular file";
    case NANO_FILE_ERR_TOO_LARGE: return "file exceeds 64 KiB editor limit";
    case NANO_FILE_ERR_MEMORY: return "not enough memory";
    case NANO_FILE_ERR_TEXT: return "unsupported non-ASCII/control data";
    default: return "filesystem I/O error";
    }
}
