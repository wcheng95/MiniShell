#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "minishell/api.h"
#include "minishell_transfer.h"

#define MFT_MAGIC_0 'M'
#define MFT_MAGIC_1 'F'
#define MFT_MAGIC_2 'T'
#define MFT_MAGIC_3 '1'

#define MFT_HEADER_SIZE       16u
#define MFT_BUFFER_SIZE       1024u
#define MFT_PATH_MAX          512u
#define MFT_IO_TIMEOUT_MS     5000u
#define MFT_MAX_FILE_SIZE     0xFFFFFFFFull
#define MFT_TEMP_SUFFIX       ".mft.part"

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

static minishell_transfer_port_t s_port;
static bool s_configured;

void minishell_transfer_configure(const minishell_transfer_port_t *port)
{
    memset(&s_port, 0, sizeof(s_port));
    if (port != NULL) s_port = *port;
    s_configured = s_port.read != NULL && s_port.write != NULL;
}

static int raw_write_all(const uint8_t *buffer, size_t size)
{
    size_t total = 0u;
    while (total < size) {
        int n = s_port.write(s_port.ctx, buffer + total, size - total, MFT_IO_TIMEOUT_MS);
        if (n <= 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

static int raw_read_exact(uint8_t *buffer, size_t size)
{
    size_t total = 0u;
    while (total < size) {
        int n = s_port.read(s_port.ctx, buffer + total, size - total, MFT_IO_TIMEOUT_MS);
        if (n <= 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

static void drain_input(void)
{
    if (!s_configured) return;
    uint8_t scratch[32];
    while (s_port.read(s_port.ctx, scratch, sizeof(scratch), 0u) > 0) {
    }
}

static int send_text(const char *text)
{
    return raw_write_all((const uint8_t *)text, strlen(text));
}

static int send_format(const char *format, ...)
{
    char line[160];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(line)) return -1;
    return raw_write_all((const uint8_t *)line, (size_t)n);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    for (size_t i = 0u; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static uint64_t load_le64(const uint8_t *p)
{
    uint64_t value = 0u;
    for (unsigned i = 0u; i < 8u; ++i) value |= (uint64_t)p[i] << (8u * i);
    return value;
}

static uint32_t load_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static const mini_fs_api_t *filesystem_api(void)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->struct_size < FIELD_END(mini_api_t, fs) || api->fs == NULL) return NULL;

    const mini_fs_api_t *fs = api->fs;
    if (fs->struct_size < FIELD_END(mini_fs_api_t, stat) ||
        fs->open == NULL || fs->close == NULL || fs->read == NULL ||
        fs->write == NULL || fs->seek == NULL || fs->sync == NULL || fs->stat == NULL) {
        return NULL;
    }
    return fs;
}

static int make_temporary_path(const char *path, char *temporary, size_t size)
{
    if (path == NULL || path[0] != '/' || path[1] == '\0') return -1;
    size_t path_len = strlen(path);
    size_t suffix_len = sizeof(MFT_TEMP_SUFFIX) - 1u;
    if (path_len + suffix_len + 1u > size) return -1;
    memcpy(temporary, path, path_len);
    memcpy(temporary + path_len, MFT_TEMP_SUFFIX, suffix_len + 1u);
    return 0;
}

static void cleanup_temporary(const mini_fs_api_t *fs, mini_file_t *file, const char *temporary)
{
    if (file != NULL && *file != MINI_FILE_INVALID) {
        (void)fs->close(*file);
        *file = MINI_FILE_INVALID;
    }
    if (s_port.remove_file != NULL && temporary != NULL) {
        s_port.remove_file(s_port.ctx, temporary);
    }
}

static int receive_payload(const mini_fs_api_t *fs,
                           mini_file_t file,
                           uint64_t size,
                           uint32_t expected_crc)
{
    uint8_t buffer[MFT_BUFFER_SIZE];
    uint64_t remaining = size;
    uint32_t crc = 0xFFFFFFFFu;

    while (remaining > 0u) {
        size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        if (raw_read_exact(buffer, chunk) != 0) return -1;

        uint32_t offset = 0u;
        while (offset < (uint32_t)chunk) {
            uint32_t written = 0u;
            mini_result_t result = fs->write(file, buffer + offset,
                                             (uint32_t)chunk - offset, &written);
            if (result != MINI_OK || written == 0u) return -2;
            offset += written;
        }

        crc = crc32_update(crc, buffer, chunk);
        remaining -= chunk;
    }

    crc ^= 0xFFFFFFFFu;
    return crc == expected_crc ? 0 : -3;
}

int minishell_transfer_put(const char *destination_path)
{
    if (!s_configured) return -1;

    const mini_fs_api_t *fs = filesystem_api();
    if (fs == NULL || s_port.replace_file == NULL) {
        (void)send_text("MFT1 ERROR service-unavailable\n");
        return -1;
    }

    char temporary[MFT_PATH_MAX];
    if (make_temporary_path(destination_path, temporary, sizeof(temporary)) != 0) {
        (void)send_text("MFT1 ERROR invalid-path\n");
        return -1;
    }

    /* The sender waits for READY, so it is safe to discard CR/LF or other bytes
     * left by the terminal before entering binary protocol mode. */
    drain_input();
    if (send_text("MFT1 PUT READY\n") != 0) return -1;

    uint8_t header[MFT_HEADER_SIZE];
    if (raw_read_exact(header, sizeof(header)) != 0) {
        (void)send_text("MFT1 ERROR header-timeout\n");
        return -1;
    }

    if (header[0] != MFT_MAGIC_0 || header[1] != MFT_MAGIC_1 ||
        header[2] != MFT_MAGIC_2 || header[3] != MFT_MAGIC_3) {
        (void)send_text("MFT1 ERROR bad-header\n");
        return -1;
    }

    uint64_t size = load_le64(&header[4]);
    uint32_t expected_crc = load_le32(&header[12]);
    if (size > MFT_MAX_FILE_SIZE) {
        (void)send_text("MFT1 ERROR file-too-large\n");
        return -1;
    }

    if (s_port.remove_file != NULL) s_port.remove_file(s_port.ctx, temporary);

    mini_file_t file = MINI_FILE_INVALID;
    mini_result_t result = fs->open(temporary,
                                    MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC,
                                    &file);
    if (result != MINI_OK) {
        (void)send_format("MFT1 ERROR open %ld\n", (long)result);
        return -1;
    }

    int receive_result = receive_payload(fs, file, size, expected_crc);
    if (receive_result != 0) {
        cleanup_temporary(fs, &file, temporary);
        if (receive_result == -3) (void)send_text("MFT1 ERROR crc\n");
        else if (receive_result == -1) (void)send_text("MFT1 ERROR payload-timeout\n");
        else (void)send_text("MFT1 ERROR write\n");
        return -1;
    }

    result = fs->sync(file);
    if (result != MINI_OK) {
        cleanup_temporary(fs, &file, temporary);
        (void)send_format("MFT1 ERROR sync %ld\n", (long)result);
        return -1;
    }

    result = fs->close(file);
    file = MINI_FILE_INVALID;
    if (result != MINI_OK) {
        cleanup_temporary(fs, &file, temporary);
        (void)send_format("MFT1 ERROR close %ld\n", (long)result);
        return -1;
    }

    if (s_port.replace_file(s_port.ctx, temporary, destination_path) != 0) {
        cleanup_temporary(fs, NULL, temporary);
        (void)send_text("MFT1 ERROR publish\n");
        return -1;
    }

    if (send_format("MFT1 OK %llu %08lx\n",
                    (unsigned long long)size,
                    (unsigned long)expected_crc) != 0) {
        return -1;
    }
    return 0;
}

static int compute_file_crc(const mini_fs_api_t *fs,
                            mini_file_t file,
                            uint64_t size,
                            uint32_t *out_crc)
{
    uint8_t buffer[MFT_BUFFER_SIZE];
    uint64_t remaining = size;
    uint32_t crc = 0xFFFFFFFFu;

    while (remaining > 0u) {
        uint32_t request = remaining > sizeof(buffer) ? sizeof(buffer) : (uint32_t)remaining;
        uint32_t read_count = 0u;
        mini_result_t result = fs->read(file, buffer, request, &read_count);
        if (result != MINI_OK || read_count == 0u) return -1;
        crc = crc32_update(crc, buffer, read_count);
        remaining -= read_count;
    }

    *out_crc = crc ^ 0xFFFFFFFFu;
    return 0;
}

static int send_file_payload(const mini_fs_api_t *fs, mini_file_t file, uint64_t size)
{
    uint8_t buffer[MFT_BUFFER_SIZE];
    uint64_t remaining = size;

    while (remaining > 0u) {
        uint32_t request = remaining > sizeof(buffer) ? sizeof(buffer) : (uint32_t)remaining;
        uint32_t read_count = 0u;
        mini_result_t result = fs->read(file, buffer, request, &read_count);
        if (result != MINI_OK || read_count == 0u) return -1;
        if (raw_write_all(buffer, read_count) != 0) return -1;
        remaining -= read_count;
    }
    return 0;
}

int minishell_transfer_get(const char *source_path)
{
    if (!s_configured) return -1;

    const mini_fs_api_t *fs = filesystem_api();
    if (fs == NULL || source_path == NULL || source_path[0] != '/') {
        (void)send_text("MFT1 ERROR invalid-request\n");
        return -1;
    }

    mini_fs_stat_t stat_info = {0};
    stat_info.struct_size = sizeof(stat_info);
    mini_result_t result = fs->stat(source_path, &stat_info);
    if (result != MINI_OK || stat_info.type != MINI_FS_TYPE_FILE) {
        (void)send_format("MFT1 ERROR stat %ld\n", (long)result);
        return -1;
    }

    mini_file_t file = MINI_FILE_INVALID;
    result = fs->open(source_path, MINI_FS_READ, &file);
    if (result != MINI_OK) {
        (void)send_format("MFT1 ERROR open %ld\n", (long)result);
        return -1;
    }

    uint32_t crc = 0u;
    if (compute_file_crc(fs, file, stat_info.size, &crc) != 0) {
        (void)fs->close(file);
        (void)send_text("MFT1 ERROR read\n");
        return -1;
    }

    uint64_t position = 0u;
    result = fs->seek(file, 0, MINI_FS_SEEK_SET, &position);
    if (result != MINI_OK || position != 0u) {
        (void)fs->close(file);
        (void)send_text("MFT1 ERROR seek\n");
        return -1;
    }

    drain_input();
    if (send_format("MFT1 GET %llu %08lx\n",
                    (unsigned long long)stat_info.size,
                    (unsigned long)crc) != 0) {
        (void)fs->close(file);
        return -1;
    }

    if (send_file_payload(fs, file, stat_info.size) != 0) {
        (void)fs->close(file);
        return -1;
    }

    result = fs->close(file);
    if (result != MINI_OK) return -1;

    if (send_text("\nMFT1 OK\n") != 0) return -1;
    return 0;
}
