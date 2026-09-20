#include "adv_webfs_settings.h"
#include <string.h>

static bool value_valid(const char *value, size_t length, size_t minimum, size_t maximum)
{
    if (length < minimum || length > maximum) return false;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)value[i] < 0x20 || (unsigned char)value[i] > 0x7e) return false;
    return true;
}

bool webfs_settings_parse(const char *data, size_t length, webfs_credentials_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!data || length > WEBFS_SETTINGS_CAP) return false;
    bool ssid = false, password = false;
    size_t start = 0;
    while (start < length) {
        size_t end = start;
        while (end < length && data[end] != '\r' && data[end] != '\n') ++end;
        size_t first = start;
        while (first < end && (data[first] == ' ' || data[first] == '\t')) ++first;
        if (first < end && data[first] != '#') {
            const char *equal = memchr(data + start, '=', end - start);
            size_t key = equal ? (size_t)(equal - data - start) : 0;
            bool is_ssid = equal && key == 4 && !memcmp(data + start, "SSID", 4);
            bool is_password = equal && key == 2 && !memcmp(data + start, "PW", 2);
            if (is_ssid || is_password) {
                size_t count = (size_t)(data + end - equal - 1);
                if ((is_ssid ? ssid : password) ||
                    !value_valid(equal + 1, count, is_ssid ? 1 : 8, is_ssid ? 32 : 63))
                    goto invalid;
                char *destination = is_ssid ? out->ssid : out->password;
                memcpy(destination, equal + 1, count);
                if (is_ssid) ssid = true;
                else password = true;
            }
        }
        start = end;
        if (start < length && data[start] == '\r') ++start;
        if (start < length && data[start] == '\n') ++start;
    }
    if (ssid && password) return true;
invalid:
    memset(out, 0, sizeof(*out));
    return false;
}

bool webfs_settings_load(const mini_fs_api_t *fs, webfs_credentials_t *out)
{
    memset(out, 0, sizeof(*out));
    mini_file_t file = MINI_FILE_INVALID;
    if (fs->open("/flash/minishell/setting.txt", MINI_FS_READ, &file) != MINI_OK) return false;
    char data[WEBFS_SETTINGS_CAP];
    size_t used = 0;
    bool complete = false;
    while (used < sizeof(data)) {
        uint32_t count = 0;
        uint32_t want = (uint32_t)(sizeof(data) - used);
        if (fs->read(file, data + used, want, &count) != MINI_OK || count > want) break;
        if (!count) { complete = true; break; }
        used += count;
    }
    if (used == sizeof(data)) {
        /* Distinguish an exact 1024-byte file from a valid prefix of a larger one. */
        char extra;
        uint32_t count = 0;
        complete = fs->read(file, &extra, 1, &count) == MINI_OK && count == 0;
    }
    mini_result_t closed = fs->close(file);
    bool valid = complete && closed == MINI_OK && webfs_settings_parse(data, used, out);
    memset(data, 0, sizeof(data));
    return valid;
}
