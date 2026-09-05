#pragma once

#include "minishell/api.h"

typedef enum {
    CP_COPY_OK = 0,
    CP_COPY_ERR_INVALID,
    CP_COPY_ERR_SAME_PATH,
    CP_COPY_ERR_SOURCE_STAT,
    CP_COPY_ERR_SOURCE_IS_DIR,
    CP_COPY_ERR_DEST_STAT,
    CP_COPY_ERR_DEST_IS_DIR,
    CP_COPY_ERR_OPEN_SOURCE,
    CP_COPY_ERR_OPEN_DEST,
    CP_COPY_ERR_READ,
    CP_COPY_ERR_WRITE,
    CP_COPY_ERR_SYNC,
    CP_COPY_ERR_CLOSE_DEST,
    CP_COPY_ERR_CLOSE_SOURCE,
} cp_copy_result_t;

cp_copy_result_t cp_copy_file(const mini_fs_api_t *fs,
                              const char *source_path,
                              const char *destination_path);
