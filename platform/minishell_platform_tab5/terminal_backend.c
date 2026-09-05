#include <stdio.h>

#include "minishell_services.h"
#include "terminal_backend.h"

#define TERMINAL_COLUMNS 80u
#define TERMINAL_ROWS    24u
#define ESC_FOLLOW_MS    20u

static minishell_terminal_read_byte_fn s_read_byte;
static int s_pending_byte = -1;

static int terminal_read(uint32_t timeout_ms)
{
    if (s_pending_byte >= 0) {
        int ch = s_pending_byte;
        s_pending_byte = -1;
        return ch;
    }
    if (s_read_byte == NULL) return -1;
    return s_read_byte(timeout_ms);
}

static mini_result_t submit_char(uint32_t codepoint, uint32_t modifiers)
{
    const mini_key_event_t event = {
        .struct_size = sizeof(mini_key_event_t),
        .type = MINI_KEY_EVENT_CHAR,
        .codepoint = codepoint,
        .key = 0u,
        .modifiers = modifiers,
    };
    return minishell_services_input_submit(&event);
}

static mini_result_t submit_special(uint32_t key)
{
    const mini_key_event_t event = {
        .struct_size = sizeof(mini_key_event_t),
        .type = MINI_KEY_EVENT_SPECIAL,
        .codepoint = 0u,
        .key = key,
        .modifiers = 0u,
    };
    return minishell_services_input_submit(&event);
}

void minishell_terminal_backend_init(minishell_terminal_read_byte_fn read_byte)
{
    s_read_byte = read_byte;
    s_pending_byte = -1;
}

void minishell_terminal_input_flush(void)
{
    s_pending_byte = -1;
    if (s_read_byte == NULL) return;
    while (s_read_byte(MINI_WAIT_NONE) >= 0) {
    }
}

mini_result_t minishell_terminal_display_get_info(void *ctx,
                                                  uint32_t *out_columns,
                                                  uint32_t *out_rows)
{
    (void)ctx;
    if (out_columns == NULL || out_rows == NULL) return MINI_ERR_INVALID;
    *out_columns = TERMINAL_COLUMNS;
    *out_rows = TERMINAL_ROWS;
    return MINI_OK;
}

mini_result_t minishell_terminal_display_clear(void *ctx)
{
    (void)ctx;
    return fputs("\x1b[2J\x1b[H", stdout) == EOF ? MINI_ERR_IO : MINI_OK;
}

mini_result_t minishell_terminal_display_clear_at(void *ctx,
                                                  uint32_t row,
                                                  uint32_t column,
                                                  uint32_t rows,
                                                  uint32_t columns)
{
    (void)ctx;
    for (uint32_t r = 0; r < rows; ++r) {
        if (fprintf(stdout, "\x1b[%u;%uH", (unsigned)(row + r + 1u),
                    (unsigned)(column + 1u)) < 0) {
            return MINI_ERR_IO;
        }
        for (uint32_t c = 0; c < columns; ++c) {
            if (fputc(' ', stdout) == EOF) return MINI_ERR_IO;
        }
    }
    return MINI_OK;
}

mini_result_t minishell_terminal_display_write_at(void *ctx,
                                                  uint32_t row,
                                                  uint32_t column,
                                                  const char *text,
                                                  uint32_t byte_count)
{
    (void)ctx;
    if (fprintf(stdout, "\x1b[%u;%uH", (unsigned)(row + 1u),
                (unsigned)(column + 1u)) < 0) {
        return MINI_ERR_IO;
    }
    if (byte_count > 0u && fwrite(text, 1u, byte_count, stdout) != byte_count) {
        return MINI_ERR_IO;
    }
    return MINI_OK;
}

mini_result_t minishell_terminal_display_present(void *ctx)
{
    (void)ctx;
    return fflush(stdout) == 0 ? MINI_OK : MINI_ERR_IO;
}

static mini_result_t parse_escape_sequence(void)
{
    int second = terminal_read(ESC_FOLLOW_MS);
    if (second < 0) return submit_special(MINI_KEY_ESCAPE);

    if (second != '[') {
        s_pending_byte = second;
        return submit_special(MINI_KEY_ESCAPE);
    }

    int third = terminal_read(ESC_FOLLOW_MS);
    if (third < 0) return submit_special(MINI_KEY_ESCAPE);

    switch (third) {
    case 'A': return submit_special(MINI_KEY_UP);
    case 'B': return submit_special(MINI_KEY_DOWN);
    case 'C': return submit_special(MINI_KEY_RIGHT);
    case 'D': return submit_special(MINI_KEY_LEFT);
    case 'H': return submit_special(MINI_KEY_HOME);
    case 'F': return submit_special(MINI_KEY_END);
    default: break;
    }

    if (third >= '0' && third <= '9') {
        int fourth = terminal_read(ESC_FOLLOW_MS);
        if (fourth == '~') {
            switch (third) {
            case '2': return submit_special(MINI_KEY_INSERT);
            case '3': return submit_special(MINI_KEY_DELETE);
            case '5': return submit_special(MINI_KEY_PAGE_UP);
            case '6': return submit_special(MINI_KEY_PAGE_DOWN);
            default: break;
            }
        }
    }

    return MINI_OK;
}

mini_result_t minishell_terminal_input_wait(void *ctx, uint32_t timeout_ms)
{
    (void)ctx;
    int ch = terminal_read(timeout_ms);
    if (ch < 0) {
        if (timeout_ms == MINI_WAIT_NONE) return MINI_ERR_NOT_READY;
        if (timeout_ms == MINI_WAIT_FOREVER) return MINI_ERR_NOT_READY;
        return MINI_ERR_TIMEOUT;
    }

    if (ch == '\r') {
        int next = terminal_read(MINI_WAIT_NONE);
        if (next >= 0 && next != '\n') s_pending_byte = next;
        return submit_special(MINI_KEY_ENTER);
    }
    if (ch == '\n') return submit_special(MINI_KEY_ENTER);
    if (ch == '\t') return submit_special(MINI_KEY_TAB);
    if (ch == '\b' || ch == 0x7f) return submit_special(MINI_KEY_BACKSPACE);
    if (ch == 0x1b) return parse_escape_sequence();

    if (ch >= 1 && ch <= 26) {
        return submit_char((uint32_t)('a' + ch - 1), MINI_MOD_CTRL);
    }
    if (ch >= 0x20 && ch <= 0x7e) {
        return submit_char((uint32_t)ch, 0u);
    }

    /* V0 terminal backend guarantees ASCII. Ignore other byte sequences. */
    return MINI_OK;
}
