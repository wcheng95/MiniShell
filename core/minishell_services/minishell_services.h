#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "minishell/api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t minishell_backend_file_t;
#define MINISHELL_BACKEND_FILE_INVALID ((minishell_backend_file_t)0u)

typedef uintptr_t minishell_backend_dir_t;
#define MINISHELL_BACKEND_DIR_INVALID ((minishell_backend_dir_t)0u)

typedef uintptr_t minishell_backend_audio_t;
#define MINISHELL_BACKEND_AUDIO_INVALID ((minishell_backend_audio_t)0u)

typedef struct {
    uint64_t memory_bytes;
    uint64_t storage_bytes;
} minishell_resource_limits_t;

typedef struct {
    void *ctx;

    /* System diagnostics. */
    void (*system_write)(void *ctx, const char *text);

    /* User-facing, line-oriented application output. */
    void (*console_write)(void *ctx, const char *text);

    /* Memory */
    void *(*memory_alloc)(void *ctx, uint32_t size);
    void *(*memory_realloc)(void *ctx, void *ptr, uint32_t new_size);
    void (*memory_free)(void *ctx, void *ptr);
    bool (*memory_get_info)(void *ctx, uint64_t *free_bytes, uint64_t *largest_free_block);

    /* Filesystem. Paths are normalized MiniShell absolute paths. */
    mini_result_t (*fs_open)(void *ctx, const char *path, uint32_t flags,
                             minishell_backend_file_t *out_file);
    mini_result_t (*fs_close)(void *ctx, minishell_backend_file_t file);
    mini_result_t (*fs_read)(void *ctx, minishell_backend_file_t file,
                             void *buffer, uint32_t size, uint32_t *out_read);
    mini_result_t (*fs_write)(void *ctx, minishell_backend_file_t file,
                              const void *buffer, uint32_t size, uint32_t *out_written);
    mini_result_t (*fs_seek)(void *ctx, minishell_backend_file_t file,
                             int64_t offset, uint32_t origin, uint64_t *out_position);
    mini_result_t (*fs_sync)(void *ctx, minishell_backend_file_t file);
    mini_result_t (*fs_stat)(void *ctx, const char *path,
                             uint32_t *out_type, uint64_t *out_size);
    mini_result_t (*fs_rename)(void *ctx, const char *old_path, const char *new_path);
    mini_result_t (*fs_remove_file)(void *ctx, const char *path);
    mini_result_t (*fs_mkdir)(void *ctx, const char *path);
    mini_result_t (*fs_rmdir)(void *ctx, const char *path);
    mini_result_t (*fs_dir_open)(void *ctx, const char *path,
                                 minishell_backend_dir_t *out_dir);
    mini_result_t (*fs_dir_read)(void *ctx, minishell_backend_dir_t dir,
                                 char *out_name, uint32_t name_size,
                                 uint32_t *out_type, uint32_t *out_has_entry);
    mini_result_t (*fs_dir_close)(void *ctx, minishell_backend_dir_t dir);

    /* Time/location baseline. */
    uint64_t (*monotonic_us)(void *ctx);
    mini_result_t (*sleep_ms)(void *ctx, uint32_t milliseconds);
    uint64_t time_location_capabilities;

    /* Optional platform UTC/default-location storage. */
    mini_result_t (*utc_load)(void *ctx, int64_t *out_seconds, uint32_t *out_nanoseconds);
    mini_result_t (*utc_store)(void *ctx, int64_t seconds, uint32_t nanoseconds);
    mini_result_t (*default_location_load)(void *ctx, int32_t *out_latitude_e7,
                                           int32_t *out_longitude_e7);
    mini_result_t (*default_location_store)(void *ctx, int32_t latitude_e7,
                                            int32_t longitude_e7);
    mini_result_t (*default_location_clear)(void *ctx);

    /* Display. */
    uint64_t display_capabilities;
    mini_result_t (*display_text_get_info)(void *ctx, uint32_t *out_columns,
                                           uint32_t *out_rows);
    mini_result_t (*display_text_clear)(void *ctx);
    mini_result_t (*display_text_clear_at)(void *ctx, uint32_t row, uint32_t column,
                                           uint32_t rows, uint32_t columns);
    mini_result_t (*display_text_write_at)(void *ctx, uint32_t row, uint32_t column,
                                           const char *text, uint32_t byte_count);
    mini_result_t (*display_text_write_at_attr)(void *ctx, uint32_t row, uint32_t column,
                                                const char *text, uint32_t byte_count,
                                                uint32_t attributes);
    mini_result_t (*display_present)(void *ctx);

    /* Input. The service owns the logical queue; the port provides waiting. */
    uint64_t input_capabilities;
    void (*input_lock)(void *ctx);
    void (*input_unlock)(void *ctx);
    mini_result_t (*input_wait)(void *ctx, uint32_t timeout_ms);
    void (*input_wake)(void *ctx);
    void (*input_flush)(void *ctx);

    /* Audio. Frames are interleaved according to the requested channel count.
     * The backend converts native transport format but does not assign semantic
     * meaning such as stereo versus I/Q to channel 0/1. */
    uint64_t audio_capabilities;
    mini_result_t (*audio_rx_open)(void *ctx, const char *endpoint,
                                   uint32_t sample_rate_hz, uint32_t sample_format,
                                   uint32_t channels, minishell_backend_audio_t *out_audio);
    mini_result_t (*audio_rx_start)(void *ctx, minishell_backend_audio_t audio);
    mini_result_t (*audio_rx_read)(void *ctx, minishell_backend_audio_t audio,
                                   void *frames, uint32_t frame_capacity,
                                   uint32_t *out_frames, uint32_t timeout_ms);
    mini_result_t (*audio_rx_stop)(void *ctx, minishell_backend_audio_t audio);
    mini_result_t (*audio_rx_close)(void *ctx, minishell_backend_audio_t audio);

    mini_result_t (*audio_tx_open)(void *ctx, const char *endpoint,
                                   uint32_t sample_rate_hz, uint32_t sample_format,
                                   uint32_t channels, minishell_backend_audio_t *out_audio);
    mini_result_t (*audio_tx_start)(void *ctx, minishell_backend_audio_t audio);
    mini_result_t (*audio_tx_write)(void *ctx, minishell_backend_audio_t audio,
                                    const void *frames, uint32_t frame_count,
                                    uint32_t *out_frames, uint32_t timeout_ms);
    mini_result_t (*audio_tx_stop)(void *ctx, minishell_backend_audio_t audio);
    mini_result_t (*audio_tx_abort)(void *ctx, minishell_backend_audio_t audio);
    mini_result_t (*audio_tx_close)(void *ctx, minishell_backend_audio_t audio);
} minishell_services_port_t;

/* Configure the resident service layer. Safe to call again in host tests. */
void minishell_services_set_resource_limits(const minishell_resource_limits_t *limits);
void minishell_services_configure(const minishell_services_port_t *port);

/* Foreground application lifecycle hooks used by the app manager. */
void minishell_services_app_begin(void);
void minishell_services_app_end(void);

/* Internal producer/update hooks for resident MiniShell platform/services. */
mini_result_t minishell_services_utc_sync(int64_t seconds, uint32_t nanoseconds,
                                          bool persist);
mini_result_t minishell_services_live_location_update(int32_t latitude_e7,
                                                       int32_t longitude_e7);
void minishell_services_live_location_clear(void);
mini_result_t minishell_services_input_submit(const mini_key_event_t *event);
void minishell_services_input_flush(void);

#ifdef __cplusplus
}
#endif
