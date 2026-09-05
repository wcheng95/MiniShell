#include "cp_copy.h"

#include <stddef.h>
#include <stdint.h>

#define CP_BUFFER_SIZE 1024u

static bool same_path_text(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return false;
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

cp_copy_result_t cp_copy_file(const mini_fs_api_t *fs,
                              const char *source_path,
                              const char *destination_path)
{
    if (fs == NULL || source_path == NULL || destination_path == NULL ||
        fs->open == NULL || fs->close == NULL || fs->read == NULL ||
        fs->write == NULL || fs->sync == NULL || fs->stat == NULL) {
        return CP_COPY_ERR_INVALID;
    }

    if (same_path_text(source_path, destination_path)) {
        return CP_COPY_ERR_SAME_PATH;
    }

    mini_fs_stat_t source_stat;
    source_stat.struct_size = sizeof(source_stat);
    mini_result_t result = fs->stat(source_path, &source_stat);
    if (result != MINI_OK) return CP_COPY_ERR_SOURCE_STAT;
    if (source_stat.type != MINI_FS_TYPE_FILE) return CP_COPY_ERR_SOURCE_IS_DIR;

    mini_fs_stat_t destination_stat;
    destination_stat.struct_size = sizeof(destination_stat);
    result = fs->stat(destination_path, &destination_stat);
    if (result == MINI_OK) {
        if (destination_stat.type != MINI_FS_TYPE_FILE) {
            return CP_COPY_ERR_DEST_IS_DIR;
        }
    } else if (result != MINI_ERR_NOT_FOUND) {
        return CP_COPY_ERR_DEST_STAT;
    }

    mini_file_t source = MINI_FILE_INVALID;
    mini_file_t destination = MINI_FILE_INVALID;
    cp_copy_result_t copy_result = CP_COPY_OK;

    result = fs->open(source_path, MINI_FS_READ, &source);
    if (result != MINI_OK) return CP_COPY_ERR_OPEN_SOURCE;

    result = fs->open(destination_path,
                      MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC,
                      &destination);
    if (result != MINI_OK) {
        copy_result = CP_COPY_ERR_OPEN_DEST;
        goto close_source;
    }

    uint8_t buffer[CP_BUFFER_SIZE];
    for (;;) {
        uint32_t read_count = 0u;
        result = fs->read(source, buffer, sizeof(buffer), &read_count);
        if (result != MINI_OK) {
            copy_result = CP_COPY_ERR_READ;
            break;
        }
        if (read_count == 0u) break;

        uint32_t offset = 0u;
        while (offset < read_count) {
            uint32_t written = 0u;
            result = fs->write(destination,
                               buffer + offset,
                               read_count - offset,
                               &written);
            if (result != MINI_OK || written == 0u) {
                copy_result = CP_COPY_ERR_WRITE;
                break;
            }
            offset += written;
        }
        if (copy_result != CP_COPY_OK) break;
    }

    if (copy_result == CP_COPY_OK && fs->sync(destination) != MINI_OK) {
        copy_result = CP_COPY_ERR_SYNC;
    }

    if (fs->close(destination) != MINI_OK && copy_result == CP_COPY_OK) {
        copy_result = CP_COPY_ERR_CLOSE_DEST;
    }

close_source:
    if (fs->close(source) != MINI_OK && copy_result == CP_COPY_OK) {
        copy_result = CP_COPY_ERR_CLOSE_SOURCE;
    }

    return copy_result;
}
