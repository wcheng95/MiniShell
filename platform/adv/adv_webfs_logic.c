#include "adv_webfs_logic.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

char webfs_password_letter(unsigned char sample)
{
    static const char alphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ";
    const unsigned count = sizeof(alphabet) - 1u;
    return sample < (256u / count) * count ? alphabet[sample % count] : 0;
}

static int hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool webfs_valid_path(const char *path)
{
    if (!path) return false;
    size_t n = 0;
    while (n < WEBFS_PATH_CAP && path[n]) ++n;
    if (!n || n == WEBFS_PATH_CAP) return false;
    size_t root;
    if (strncmp(path, "/flash", 6) == 0) root = 6;
    else if (strncmp(path, "/sd", 3) == 0) root = 3;
    else return false;
    if (path[root] && path[root] != '/') return false;
    /* Backslash is a FAT separator even though MiniShell normalizes '/'. */
    for (size_t i = 0; i < n; ++i)
        if ((unsigned char)path[i] < 32 || path[i] == 127 || path[i] == '\\') return false;
    const char *p = path + root;
    while (*p) {
        if (*p++ != '/') return false;
        const char *start = p;
        while (*p && *p != '/') ++p;
        size_t len = (size_t)(p - start);
        if (!len || len > MINI_FS_NAME_MAX ||
            (len == 1 && start[0] == '.') ||
            (len == 2 && start[0] == '.' && start[1] == '.')) return false;
    }
    return true;
}

static bool decode_path(const char *query, size_t len, char *path, size_t capacity)
{
    if (!path || !capacity) return false;
    path[0] = 0;
    size_t out = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)query[i];
        if (c == '&' || c == '#' || c == '=') return false;
        if (c == '%') {
            if (i + 2 >= len) return false;
            int hi = hex((unsigned char)query[i+1]), lo = hex((unsigned char)query[i+2]);
            if (hi < 0 || lo < 0) return false;
            c = (unsigned char)(hi * 16 + lo);
            i += 2;
        } else if (c == '+') c = ' ';
        if (c < 32 || c == 127 || out + 1 >= capacity || out + 1 >= WEBFS_PATH_CAP) return false;
        path[out++] = (char)c;
    }
    path[out] = 0;
    return webfs_valid_path(path);
}

static size_t query_length(const char *query)
{
    size_t n = 0;
    if (query) while (n < WEBFS_QUERY_CAP && query[n]) ++n;
    return n;
}

bool webfs_query_path(const char *query, char *path, size_t capacity)
{
    size_t n = query_length(query);
    return n >= 5 && n < WEBFS_QUERY_CAP && !strncmp(query, "path=", 5) &&
           decode_path(query + 5, n - 5, path, capacity);
}

bool webfs_query_rename(const char *query, char *from, char *to, size_t capacity)
{
    size_t n = query_length(query);
    if (n < 10 || n >= WEBFS_QUERY_CAP) return false;
    const char *split = memchr(query, '&', n);
    if (!split || memchr(split + 1, '&', n - (size_t)(split + 1 - query))) return false;
    const char *a = query, *b = split + 1;
    size_t na = (size_t)(split - query), nb = n - na - 1;
    if (na >= 5 && nb >= 3 && !strncmp(a, "from=", 5) && !strncmp(b, "to=", 3))
        return decode_path(a + 5, na - 5, from, capacity) && decode_path(b + 3, nb - 3, to, capacity);
    if (na >= 3 && nb >= 5 && !strncmp(a, "to=", 3) && !strncmp(b, "from=", 5))
        return decode_path(b + 5, nb - 5, from, capacity) && decode_path(a + 3, na - 3, to, capacity);
    return false;
}

bool webfs_json_string(const char *text, char *out, size_t capacity)
{
    static const char digits[] = "0123456789abcdef";
    if (!text || !out || capacity < 3) return false;
    size_t n = 0;
    out[n++] = '"';
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        size_t need = (*p < 32 || *p == 127) ? 6 : (*p == '"' || *p == '\\') ? 2 : 1;
        if (n + need + 2 > capacity) return false;
        if (need == 6) {
            memcpy(out+n, "\\u00", 4); n += 4;
            out[n++] = digits[*p >> 4]; out[n++] = digits[*p & 15];
        } else {
            if (need == 2) out[n++] = '\\';
            out[n++] = (char)*p;
        }
    }
    out[n++] = '"'; out[n] = 0;
    return true;
}

mini_result_t webfs_list(const mini_fs_api_t *fs, webfs_buffers_t *b,
                         webfs_emit_fn emit, void *ctx)
{
    if (!webfs_valid_path(b->path)) return MINI_ERR_INVALID;
    mini_fs_space_t space = {.struct_size = sizeof(space)};
    mini_result_t rc = fs->space(b->path, &space);
    if (rc != MINI_OK) return rc;
    mini_dir_t dir = MINI_DIR_INVALID;
    rc = fs->dir_open(b->path, &dir);
    if (rc != MINI_OK) return rc;
    int n = snprintf(b->transfer, sizeof(b->transfer),
        "{\"total\":%" PRIu64 ",\"used\":%" PRIu64 ",\"free\":%" PRIu64 ",\"entries\":[",
        space.total_bytes, space.used_bytes, space.free_bytes);
    if (!emit(ctx, b->transfer, (size_t)n)) rc = MINI_ERR_IO;
    bool first = true;
    while (rc == MINI_OK) {
        if (!emit(ctx, "", 0)) { rc = MINI_ERR_IO; break; }
        mini_fs_dir_entry_t entry = {.struct_size = sizeof(entry)};
        uint32_t present = 0;
        rc = fs->dir_read(dir, &entry, &present);
        if (rc != MINI_OK || !present) break;
        if (!memchr(entry.name, 0, sizeof(entry.name))) { rc = MINI_ERR_NAME_TOO_LONG; break; }
        if (!strcmp(entry.name, ".") || !strcmp(entry.name, "..")) continue;
        if (entry.type != MINI_FS_TYPE_FILE && entry.type != MINI_FS_TYPE_DIRECTORY) continue;
        n = snprintf(b->child, sizeof(b->child), "%s/%s", b->path, entry.name);
        if (n < 0 || (size_t)n >= sizeof(b->child)) { rc = MINI_ERR_NAME_TOO_LONG; break; }
        if (strchr(entry.name, '/') || !webfs_valid_path(b->child)) { rc = MINI_ERR_INVALID; break; }
        mini_fs_stat_t info = {.struct_size = sizeof(info)};
        rc = fs->stat(b->child, &info);
        if (rc != MINI_OK) break;
        /* Worst case: 255 six-byte escapes, plus fixed entry metadata < 2048. */
        if (!webfs_json_string(entry.name, b->transfer, sizeof(b->transfer))) {
            rc = MINI_ERR_NAME_TOO_LONG; break;
        }
        if (!emit(ctx, first ? "{\"name\":" : ",{\"name\":", first ? 8 : 9) ||
            !emit(ctx, b->transfer, strlen(b->transfer))) { rc = MINI_ERR_IO; break; }
        n = snprintf(b->transfer, sizeof(b->transfer), " ,\"type\":\"%s\",\"size\":%" PRIu64 "}",
                     entry.type == MINI_FS_TYPE_DIRECTORY ? "dir" : "file", info.size);
        if (!emit(ctx, b->transfer, (size_t)n)) { rc = MINI_ERR_IO; break; }
        first = false;
    }
    mini_result_t closed = fs->dir_close(dir);
    if (rc == MINI_OK) rc = closed;
    if (rc == MINI_OK && !emit(ctx, "]}", 2)) rc = MINI_ERR_IO;
    return rc;
}

mini_result_t webfs_file(const mini_fs_api_t *fs, webfs_buffers_t *b,
                         webfs_emit_fn emit, void *ctx)
{
    if (!webfs_valid_path(b->path)) return MINI_ERR_INVALID;
    mini_fs_stat_t info = {.struct_size = sizeof(info)};
    mini_result_t rc = fs->stat(b->path, &info);
    if (rc != MINI_OK) return rc;
    if (info.type != MINI_FS_TYPE_FILE) return MINI_ERR_IS_DIR;
    mini_file_t file = MINI_FILE_INVALID;
    rc = fs->open(b->path, MINI_FS_READ, &file);
    if (rc != MINI_OK) return rc;
    uint64_t remaining = info.size;
    while (remaining && rc == MINI_OK) {
        if (!emit(ctx, "", 0)) { rc = MINI_ERR_IO; break; }
        uint32_t count = 0;
        uint32_t want = remaining < sizeof(b->transfer) ? (uint32_t)remaining : sizeof(b->transfer);
        rc = fs->read(file, b->transfer, want, &count);
        if (rc != MINI_OK) break;
        if (!count || count > want) { rc = MINI_ERR_IO; break; }
        if (!emit(ctx, b->transfer, count)) { rc = MINI_ERR_IO; break; }
        remaining -= count;
    }
    mini_result_t closed = fs->close(file);
    return rc == MINI_OK ? closed : rc;
}

bool webfs_mutable_path(const char *path)
{
    return webfs_valid_path(path) && strcmp(path, "/flash") && strcmp(path, "/sd");
}

mini_result_t webfs_mutate(const mini_fs_api_t *fs, webfs_mutation_t operation,
                           const char *path, const char *to)
{
    if (!webfs_mutable_path(path)) return MINI_ERR_INVALID;
    switch (operation) {
    case WEBFS_MKDIR: return fs->mkdir(path);
    case WEBFS_REMOVE_FILE: return fs->remove_file(path);
    case WEBFS_RMDIR: return fs->rmdir(path);
    case WEBFS_RENAME: {
        if (!webfs_mutable_path(to)) return MINI_ERR_INVALID;
        size_t parent = (size_t)(strrchr(path, '/') - path);
        if ((size_t)(strrchr(to, '/') - to) != parent || strncmp(path, to, parent))
            return MINI_ERR_INVALID;
        return fs->rename(path, to);
    }
    default: return MINI_ERR_INVALID;
    }
}

static bool same_ascii_name(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return *a == *b;
}

mini_result_t webfs_upload(const mini_fs_api_t *fs, webfs_buffers_t *b, size_t length,
                           webfs_receive_fn receive, webfs_random_fn random, void *ctx)
{
    if (!webfs_mutable_path(b->path)) return MINI_ERR_INVALID;
    mini_fs_stat_t info = {.struct_size = sizeof(info)};
    mini_result_t rc = fs->stat(b->path, &info);
    if (rc == MINI_OK && info.type != MINI_FS_TYPE_FILE) return MINI_ERR_IS_DIR;
    if (rc != MINI_OK && rc != MINI_ERR_NOT_FOUND) return rc;
    size_t parent = (size_t)(strrchr(b->path, '/') - b->path + 1);
    const size_t name_size = sizeof(".webfs-upload-00000000.tmp");
    if (parent + name_size > sizeof(b->child)) return MINI_ERR_NAME_TOO_LONG;
    memcpy(b->child, b->path, parent);
    mini_file_t file = MINI_FILE_INVALID;
    rc = MINI_ERR_EXISTS;
    for (unsigned attempt = 0; attempt < WEBFS_TEMP_ATTEMPTS; ++attempt) {
        snprintf(b->child + parent, sizeof(b->child) - parent,
                 ".webfs-upload-%08" PRIx32 ".tmp", random(ctx));
        /* FAT is case-insensitive; never create the destination as our temp. */
        if (same_ascii_name(b->child, b->path)) continue;
        rc = fs->open(b->child, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_EXCL, &file);
        if (rc != MINI_ERR_EXISTS) break;
    }
    if (rc != MINI_OK) return rc;
    while (length && rc == MINI_OK) {
        uint32_t want = length < sizeof(b->transfer) ? (uint32_t)length : sizeof(b->transfer);
        uint32_t count = 0;
        rc = receive(ctx, b->transfer, want, &count);
        if (rc != MINI_OK) break;
        if (!count || count > want) { rc = MINI_ERR_IO; break; }
        uint32_t offset = 0;
        while (offset < count) {
            uint32_t written = 0;
            rc = receive(ctx, NULL, 0, &written);
            if (rc != MINI_OK) break;
            rc = fs->write(file, b->transfer + offset, count - offset, &written);
            if (rc != MINI_OK) break;
            if (!written || written > count - offset) { rc = MINI_ERR_IO; break; }
            offset += written;
        }
        length -= count;
    }
    uint32_t ignored = 0;
    if (rc == MINI_OK) rc = receive(ctx, NULL, 0, &ignored);
    if (rc == MINI_OK) rc = fs->sync(file);
    /* MiniShell consumes the handle even when backend close reports failure. */
    mini_result_t closed = fs->close(file);
    if (rc == MINI_OK) rc = closed;
    if (rc == MINI_OK) rc = receive(ctx, NULL, 0, &ignored);
    if (rc == MINI_OK) rc = fs->rename(b->child, b->path);
    if (rc != MINI_OK) (void)fs->remove_file(b->child);
    return rc;
}
