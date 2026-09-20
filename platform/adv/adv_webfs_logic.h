#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "minishell/api.h"

#define WEBFS_PATH_CAP 512u
#define WEBFS_QUERY_CAP (3u * WEBFS_PATH_CAP)
#define WEBFS_TRANSFER_CAP 2048u

typedef struct {
    char query[WEBFS_QUERY_CAP];
    char path[WEBFS_PATH_CAP];
    char child[WEBFS_PATH_CAP];
    char transfer[WEBFS_TRANSFER_CAP];
} webfs_buffers_t;

/* Returns zero for rejected RNG samples; accepted letters are unbiased. */
char webfs_password_letter(unsigned char sample);

/* Exactly one path= query parameter; percent-decode once, then validate. */
bool webfs_query_path(const char *query, char *path, size_t capacity);
bool webfs_valid_path(const char *path);
bool webfs_json_string(const char *text, char *out, size_t capacity);
/* Synchronous sink, also called with length zero to check cancellation. */
typedef bool (*webfs_emit_fn)(void *ctx, const char *data, size_t length);
mini_result_t webfs_list(const mini_fs_api_t *fs, webfs_buffers_t *buffers,
                         webfs_emit_fn emit, void *ctx);
mini_result_t webfs_file(const mini_fs_api_t *fs, webfs_buffers_t *buffers,
                         webfs_emit_fn emit, void *ctx);

#define WEBFS_TEMP_ATTEMPTS 16u
/* size == 0 is a cancellation probe; no body bytes may be consumed. */
typedef mini_result_t (*webfs_receive_fn)(void *ctx, void *data, uint32_t size, uint32_t *count);
typedef uint32_t (*webfs_random_fn)(void *ctx);
typedef enum { WEBFS_MKDIR, WEBFS_REMOVE_FILE, WEBFS_RMDIR, WEBFS_RENAME } webfs_mutation_t;
bool webfs_mutable_path(const char *path);
bool webfs_query_rename(const char *query, char *from, char *to, size_t capacity);
mini_result_t webfs_mutate(const mini_fs_api_t *fs, webfs_mutation_t operation,
                           const char *path, const char *to);
mini_result_t webfs_upload(const mini_fs_api_t *fs, webfs_buffers_t *buffers, size_t length,
                           webfs_receive_fn receive, webfs_random_fn random, void *ctx);
