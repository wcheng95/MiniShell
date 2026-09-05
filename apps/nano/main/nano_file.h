#pragma once

#include "minishell/api.h"
#include "nano_buffer.h"

enum {
    NANO_FILE_OK = 0,
    NANO_FILE_NEW = 1,
    NANO_FILE_ERR_PATH = -1,
    NANO_FILE_ERR_TYPE = -2,
    NANO_FILE_ERR_TOO_LARGE = -3,
    NANO_FILE_ERR_MEMORY = -4,
    NANO_FILE_ERR_TEXT = -5,
    NANO_FILE_ERR_IO = -6,
};

int nano_file_load(const mini_memory_api_t *memory,
                   const mini_fs_api_t *fs,
                   const char *path,
                   nano_buffer_t *buffer);

int nano_file_save(const mini_fs_api_t *fs,
                   const char *path,
                   nano_buffer_t *buffer);

const char *nano_file_result_text(int result);
