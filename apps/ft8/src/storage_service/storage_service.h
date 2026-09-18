#ifndef FT8_STORAGE_SERVICE_H
#define FT8_STORAGE_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#include "minishell/api.h"

typedef struct {
    const mini_fs_api_t *fs;
} StorageService;

typedef enum {
    STORAGE_READ_FOUND,
    STORAGE_READ_NOT_FOUND,
    STORAGE_READ_ERROR
} StorageReadResult;

bool storage_service_init(StorageService *storage, const mini_fs_api_t *fs);
bool storage_service_ensure_directory(StorageService *storage, const char *path);
/* Output is valid and NUL terminated only on FOUND. */
StorageReadResult storage_service_read_text(StorageService *storage, const char *path,
                                           char *out, size_t out_size);
bool storage_service_write_text_atomic(StorageService *storage, const char *path,
                                        const char *text);

#endif
