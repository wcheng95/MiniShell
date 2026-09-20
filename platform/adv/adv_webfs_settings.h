#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "minishell/api.h"

#define WEBFS_SETTINGS_CAP 1024u
#define WEBFS_SSID_CAP 33u
#define WEBFS_PASSWORD_CAP 64u

typedef struct {
    char ssid[WEBFS_SSID_CAP];
    char password[WEBFS_PASSWORD_CAP];
} webfs_credentials_t;

/* Failure clears the entire pair; input is a byte range, not a C string. */
bool webfs_settings_parse(const char *data, size_t length, webfs_credentials_t *out);
bool webfs_settings_load(const mini_fs_api_t *fs, webfs_credentials_t *out);
