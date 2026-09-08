#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "minishell/api.h"

typedef mini_result_t (*linux_terminal_emit_fn)(void *ctx,
                                                const mini_key_event_t *event);

typedef struct {
    linux_terminal_emit_fn emit;
    void *emit_ctx;
    unsigned char pending[8];
    size_t pending_count;
    bool escape_pending;
} linux_terminal_parser_t;

void linux_terminal_parser_init(linux_terminal_parser_t *parser,
                                linux_terminal_emit_fn emit,
                                void *emit_ctx);
void linux_terminal_parser_reset(linux_terminal_parser_t *parser);
mini_result_t linux_terminal_parser_feed(linux_terminal_parser_t *parser,
                                         const unsigned char *bytes,
                                         size_t count,
                                         bool *out_emitted);
mini_result_t linux_terminal_parser_flush_escape(linux_terminal_parser_t *parser,
                                                 bool *out_emitted);
bool linux_terminal_parser_has_pending_escape(const linux_terminal_parser_t *parser);
