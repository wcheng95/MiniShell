#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINISHELL_ABI_VERSION 0x00000001u

#if defined(__GNUC__)
#define MINI_IMPORT __attribute__((visibility("default")))
#else
#define MINI_IMPORT
#endif

typedef int32_t mini_result_t;

#define MINI_OK                  ((mini_result_t)  0)
#define MINI_ERR_INVALID         ((mini_result_t) -1)
#define MINI_ERR_NOT_FOUND       ((mini_result_t) -2)
#define MINI_ERR_EXISTS          ((mini_result_t) -3)
#define MINI_ERR_BAD_HANDLE      ((mini_result_t) -4)
#define MINI_ERR_ACCESS          ((mini_result_t) -5)
#define MINI_ERR_IO              ((mini_result_t) -6)
#define MINI_ERR_NO_SPACE        ((mini_result_t) -7)
#define MINI_ERR_TOO_MANY_OPEN   ((mini_result_t) -8)
#define MINI_ERR_NAME_TOO_LONG   ((mini_result_t) -9)
#define MINI_ERR_UNSUPPORTED     ((mini_result_t)-10)
#define MINI_ERR_NOT_DIR         ((mini_result_t)-11)
#define MINI_ERR_IS_DIR          ((mini_result_t)-12)
#define MINI_ERR_NO_MEMORY       ((mini_result_t)-13)
#define MINI_ERR_NOT_READY       ((mini_result_t)-14)
#define MINI_ERR_TIMEOUT         ((mini_result_t)-15)
#define MINI_ERR_NOT_EMPTY       ((mini_result_t)-16)

typedef struct {
    uint32_t struct_size;
    void (*write)(const char *text);
} mini_system_api_t;

#define MINI_MEM_INFO_APP_USAGE      (1ull << 0)
#define MINI_MEM_INFO_FREE_BYTES     (1ull << 1)
#define MINI_MEM_INFO_LARGEST_BLOCK  (1ull << 2)

typedef struct {
    uint32_t struct_size;
    uint32_t reserved0;
    uint64_t valid_fields;
    uint64_t app_allocated_bytes;
    uint32_t app_allocation_count;
    uint32_t reserved1;
    uint64_t free_bytes;
    uint64_t largest_free_block;
} mini_memory_info_t;

typedef struct {
    uint32_t struct_size;
    mini_result_t (*alloc)(uint32_t size, void **out_ptr);
    mini_result_t (*realloc)(void *ptr, uint32_t new_size, void **out_ptr);
    mini_result_t (*free)(void *ptr);
    mini_result_t (*get_info)(mini_memory_info_t *out_info);
} mini_memory_api_t;

typedef uint32_t mini_file_t;
#define MINI_FILE_INVALID ((mini_file_t)0u)

#define MINI_FS_READ    (1u << 0)
#define MINI_FS_WRITE   (1u << 1)
#define MINI_FS_CREATE  (1u << 2)
#define MINI_FS_EXCL    (1u << 3)
#define MINI_FS_TRUNC   (1u << 4)
#define MINI_FS_APPEND  (1u << 5)

#define MINI_FS_SEEK_SET 0u
#define MINI_FS_SEEK_CUR 1u
#define MINI_FS_SEEK_END 2u

#define MINI_FS_TYPE_FILE       1u
#define MINI_FS_TYPE_DIRECTORY  2u

typedef struct {
    uint32_t struct_size;
    uint32_t type;
    uint64_t size;
} mini_fs_stat_t;

typedef struct {
    uint32_t struct_size;
    mini_result_t (*open)(const char *path, uint32_t flags, mini_file_t *out_file);
    mini_result_t (*close)(mini_file_t file);
    mini_result_t (*read)(mini_file_t file, void *buffer, uint32_t size, uint32_t *out_read);
    mini_result_t (*write)(mini_file_t file, const void *buffer, uint32_t size, uint32_t *out_written);
    mini_result_t (*seek)(mini_file_t file, int64_t offset, uint32_t origin, uint64_t *out_position);
    mini_result_t (*sync)(mini_file_t file);
    mini_result_t (*stat)(const char *path, mini_fs_stat_t *out_stat);
    mini_result_t (*rename)(const char *old_path, const char *new_path);
    mini_result_t (*remove_file)(const char *path);
    mini_result_t (*mkdir)(const char *path);
    mini_result_t (*rmdir)(const char *path);
} mini_fs_api_t;

#define MINI_TIMELOC_CAP_UTC                   (1ull << 0)
#define MINI_TIMELOC_CAP_LOCATION              (1ull << 1)
#define MINI_TIMELOC_CAP_SET_UTC               (1ull << 2)
#define MINI_TIMELOC_CAP_DEFAULT_LOCATION      (1ull << 3)
#define MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION  (1ull << 4)

#define MINI_LOCATION_SOURCE_DEFAULT  1u
#define MINI_LOCATION_SOURCE_LIVE     2u

#define MINI_TIMELOC_SNAPSHOT_UTC_VALID       (1ull << 0)
#define MINI_TIMELOC_SNAPSHOT_LOCATION_VALID  (1ull << 1)

typedef struct {
    uint32_t struct_size;
    int64_t unix_seconds;
    uint32_t nanoseconds;
} mini_utc_time_t;

typedef struct {
    uint32_t struct_size;
    int32_t latitude_e7;
    int32_t longitude_e7;
} mini_geo_point_t;

typedef struct {
    uint32_t struct_size;
    int32_t latitude_e7;
    int32_t longitude_e7;
    uint32_t source;
    uint32_t reserved0;
    uint64_t updated_monotonic_us;
} mini_location_t;

typedef struct {
    uint32_t struct_size;
    uint32_t reserved_header;
    uint64_t valid_fields;
    uint64_t monotonic_us;
    int64_t utc_unix_seconds;
    uint32_t utc_nanoseconds;
    uint32_t reserved0;
    int32_t latitude_e7;
    int32_t longitude_e7;
    uint32_t location_source;
    uint32_t reserved1;
    uint64_t location_updated_monotonic_us;
} mini_time_location_snapshot_t;

typedef struct {
    uint32_t struct_size;
    uint64_t capabilities;
    uint64_t (*monotonic_us)(void);
    mini_result_t (*sleep_ms)(uint32_t milliseconds);
    mini_result_t (*utc_get)(mini_utc_time_t *out_time);
    mini_result_t (*utc_set)(const mini_utc_time_t *time);
    mini_result_t (*location_get)(mini_location_t *out_location);
    mini_result_t (*location_default_get)(mini_geo_point_t *out_location);
    mini_result_t (*location_default_set)(const mini_geo_point_t *location);
    mini_result_t (*location_default_clear)(void);
    mini_result_t (*snapshot_get)(mini_time_location_snapshot_t *out_snapshot);
} mini_time_location_api_t;

#define MINI_DISPLAY_CAP_TEXT  (1ull << 0)

typedef struct {
    uint32_t struct_size;
    uint32_t columns;
    uint32_t rows;
} mini_text_display_info_t;

typedef struct {
    uint32_t struct_size;
    mini_result_t (*get_info)(mini_text_display_info_t *out_info);
    mini_result_t (*clear)(void);
    mini_result_t (*clear_at)(uint32_t row, uint32_t column, uint32_t rows, uint32_t columns);
    mini_result_t (*write_at)(uint32_t row, uint32_t column, const char *text, uint32_t byte_count);
} mini_text_display_api_t;

typedef struct {
    uint32_t struct_size;
    uint64_t capabilities;
    const mini_text_display_api_t *text;
    mini_result_t (*present)(void);
} mini_display_api_t;

#define MINI_INPUT_CAP_KEY  (1ull << 0)

#define MINI_KEY_EVENT_CHAR     1u
#define MINI_KEY_EVENT_SPECIAL  2u

#define MINI_MOD_SHIFT  (1u << 0)
#define MINI_MOD_CTRL   (1u << 1)
#define MINI_MOD_ALT    (1u << 2)

#define MINI_KEY_UP         1u
#define MINI_KEY_DOWN       2u
#define MINI_KEY_LEFT       3u
#define MINI_KEY_RIGHT      4u
#define MINI_KEY_ENTER      5u
#define MINI_KEY_BACKSPACE  6u
#define MINI_KEY_DELETE     7u
#define MINI_KEY_ESCAPE     8u
#define MINI_KEY_TAB        9u
#define MINI_KEY_HOME       10u
#define MINI_KEY_END        11u
#define MINI_KEY_PAGE_UP    12u
#define MINI_KEY_PAGE_DOWN  13u
#define MINI_KEY_INSERT     14u

#define MINI_WAIT_NONE     0u
#define MINI_WAIT_FOREVER  0xFFFFFFFFu

typedef struct {
    uint32_t struct_size;
    uint32_t type;
    uint32_t codepoint;
    uint32_t key;
    uint32_t modifiers;
} mini_key_event_t;

typedef struct {
    uint32_t struct_size;
    mini_result_t (*read)(mini_key_event_t *out_event, uint32_t timeout_ms);
} mini_key_input_api_t;

typedef struct {
    uint32_t struct_size;
    uint64_t capabilities;
    const mini_key_input_api_t *key;
} mini_input_api_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    const mini_system_api_t *system;
    const mini_memory_api_t *memory;
    const mini_fs_api_t *fs;
    const mini_time_location_api_t *time_location;
    const mini_display_api_t *display;
    const mini_input_api_t *input;
} mini_api_t;

MINI_IMPORT const mini_api_t *mini_api_get(void);

#ifdef __cplusplus
}
#endif
