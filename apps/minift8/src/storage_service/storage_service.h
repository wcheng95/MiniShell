#ifndef MINIFT8_STORAGE_SERVICE_H
#define MINIFT8_STORAGE_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#include "minishell/api.h"

typedef struct {
    const mini_fs_api_t *fs;
} StorageService;

bool storage_service_init(StorageService *storage, const mini_fs_api_t *fs);
bool storage_service_ensure_directory(StorageService *storage, const char *path);
bool storage_service_read_text(StorageService *storage, const char *path,
                               char *out, size_t out_size);
bool storage_service_write_text_atomic(StorageService *storage, const char *path,
                                        const char *text);

#endif
