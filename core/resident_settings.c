#include "resident_settings.h"
#include <string.h>

void minishell_resident_settings_parse(const char *data, size_t length,
                                     minishell_resident_settings_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!data || length > MINISHELL_SETTINGS_CAP) return;
    size_t start = 0;
    while (start < length) {
        size_t end = start;
        while (end < length && data[end] != '\r' && data[end] != '\n') ++end;
        const char *line = data + start;
        size_t count = end - start;
        if (count >= 8 && !memcmp(line, "startup=", 8)) {
            size_t bytes = count - 8;
            /* Never execute a valid prefix of a record containing a NUL. */
            if (bytes < sizeof(out->startup) && !memchr(line + 8, '\0', bytes)) {
                memcpy(out->startup, line + 8, bytes);
                out->startup[bytes] = '\0';
            }
        }
        start = end;
        if (start < length && data[start] == '\r') ++start;
        if (start < length && data[start] == '\n') ++start;
    }
}

bool minishell_resident_settings_load(const mini_fs_api_t *fs,
                                    minishell_resident_settings_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!fs || !fs->open || !fs->read || !fs->close) return false;
    mini_file_t file = MINI_FILE_INVALID;
    if (fs->open("/flash/minishell/setting.txt", MINI_FS_READ, &file) != MINI_OK) return false;
    char data[MINISHELL_SETTINGS_CAP];
    size_t used = 0;
    bool complete = false;
    while (used < sizeof(data)) {
        uint32_t count = 0, want = (uint32_t)(sizeof(data) - used);
        if (fs->read(file, data + used, want, &count) != MINI_OK || count > want) break;
        if (!count) { complete = true; break; }
        used += count;
    }
    if (used == sizeof(data)) {
        char extra;
        uint32_t count = 0;
        complete = fs->read(file, &extra, 1, &count) == MINI_OK && count == 0;
    }
    mini_result_t closed = fs->close(file);
    if (complete && closed == MINI_OK) minishell_resident_settings_parse(data, used, out);
    return complete && closed == MINI_OK;
}
