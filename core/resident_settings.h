#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "minishell/api.h"

#define MINISHELL_SETTINGS_CAP 1024u

typedef struct {
    char startup[MINISHELL_SETTINGS_CAP];
} minishell_resident_settings_t;

void minishell_resident_settings_parse(const char *data, size_t length,
                                     minishell_resident_settings_t *out);
/* Unavailable, incomplete or oversized files leave all settings at defaults. */
bool minishell_resident_settings_load(const mini_fs_api_t *fs,
                                    minishell_resident_settings_t *out);
