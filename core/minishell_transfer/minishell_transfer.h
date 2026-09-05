#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ctx;

    /* Raw byte-stream transport. Return byte count, 0 on timeout/no data, <0 on error. */
    int (*read)(void *ctx, uint8_t *buffer, size_t size, uint32_t timeout_ms);
    int (*write)(void *ctx, const uint8_t *buffer, size_t size, uint32_t timeout_ms);

    /* Platform-private publication helpers for a completed receive. */
    int (*replace_file)(void *ctx, const char *temporary_path, const char *destination_path);
    void (*remove_file)(void *ctx, const char *path);
} minishell_transfer_port_t;

void minishell_transfer_configure(const minishell_transfer_port_t *port);

/* Resident shell operations. They own the transport synchronously until return. */
int minishell_transfer_put(const char *destination_path);
int minishell_transfer_get(const char *source_path);

#ifdef __cplusplus
}
#endif
