#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "linux_internal.h"
#include "linux_terminal_parser.h"

#define INPUT_READ_MAX 64u
#define ESC_AMBIGUITY_US 30000u

typedef struct {
    struct termios saved_termios;
    bool app_mode_active;
    linux_terminal_parser_t parser;
    uint64_t escape_deadline_us;
} terminal_state_t;

static terminal_state_t s_terminal;

static uint64_t terminal_now_us(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0u;
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static uint32_t ceil_ms(uint64_t microseconds)
{
    uint64_t milliseconds = (microseconds + 999u) / 1000u;
    if (milliseconds > (uint64_t)INT_MAX) return (uint32_t)INT_MAX;
    return (uint32_t)milliseconds;
}

static mini_result_t display_get_info(void *ctx, uint32_t *out_columns,
                                      uint32_t *out_rows)
{
    (void)ctx;
    if (out_columns == NULL || out_rows == NULL) return MINI_ERR_INVALID;

    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 &&
        size.ws_col != 0u && size.ws_row != 0u) {
        *out_columns = size.ws_col;
        *out_rows = size.ws_row;
        return MINI_OK;
    }

    *out_columns = 80u;
    *out_rows = 24u;
    return MINI_OK;
}

static mini_result_t display_clear(void *ctx)
{
    (void)ctx;
    fputs("\033[2J\033[H", stdout);
    return ferror(stdout) ? MINI_ERR_IO : MINI_OK;
}

static mini_result_t display_clear_at(void *ctx, uint32_t row,
                                      uint32_t column, uint32_t rows,
                                      uint32_t columns)
{
    (void)ctx;
    for (uint32_t r = 0; r < rows; ++r) {
        if (fprintf(stdout, "\033[%u;%uH", (unsigned)(row + r + 1u),
                    (unsigned)(column + 1u)) < 0) {
            return MINI_ERR_IO;
        }
        for (uint32_t c = 0; c < columns; ++c) {
            if (fputc(' ', stdout) == EOF) return MINI_ERR_IO;
        }
    }
    return MINI_OK;
}

static mini_result_t display_write_at(void *ctx, uint32_t row,
                                      uint32_t column, const char *text,
                                      uint32_t byte_count)
{
    (void)ctx;
    if (fprintf(stdout, "\033[%u;%uH", (unsigned)(row + 1u),
                (unsigned)(column + 1u)) < 0) {
        return MINI_ERR_IO;
    }
    return fwrite(text, 1u, byte_count, stdout) == byte_count ? MINI_OK : MINI_ERR_IO;
}

static mini_result_t display_write_at_attr(void *ctx, uint32_t row,
                                           uint32_t column, const char *text,
                                           uint32_t byte_count,
                                           uint32_t attributes)
{
    if ((attributes & ~MINI_TEXT_ATTR_INVERSE) != 0u) return MINI_ERR_INVALID;
    if (attributes == MINI_TEXT_ATTR_NONE) {
        return display_write_at(ctx, row, column, text, byte_count);
    }

    (void)ctx;
    if (fprintf(stdout, "\033[%u;%uH\033[7m", (unsigned)(row + 1u),
                (unsigned)(column + 1u)) < 0) {
        return MINI_ERR_IO;
    }
    if (fwrite(text, 1u, byte_count, stdout) != byte_count) return MINI_ERR_IO;
    return fputs("\033[0m", stdout) == EOF ? MINI_ERR_IO : MINI_OK;
}

static mini_result_t display_present(void *ctx)
{
    (void)ctx;
    return fflush(stdout) == 0 ? MINI_OK : MINI_ERR_IO;
}

static mini_result_t parser_emit(void *ctx, const mini_key_event_t *event)
{
    (void)ctx;
    return minishell_services_input_submit(event);
}

static void update_escape_deadline(void)
{
    if (linux_terminal_parser_has_pending_escape(&s_terminal.parser)) {
        if (s_terminal.escape_deadline_us == 0u) {
            s_terminal.escape_deadline_us = terminal_now_us() + ESC_AMBIGUITY_US;
        }
    } else {
        s_terminal.escape_deadline_us = 0u;
    }
}

static mini_result_t flush_escape_if_due(uint64_t now, bool *out_emitted)
{
    if (!linux_terminal_parser_has_pending_escape(&s_terminal.parser) ||
        s_terminal.escape_deadline_us == 0u ||
        now < s_terminal.escape_deadline_us) {
        if (out_emitted != NULL) *out_emitted = false;
        return MINI_OK;
    }

    mini_result_t result = linux_terminal_parser_flush_escape(&s_terminal.parser,
                                                               out_emitted);
    s_terminal.escape_deadline_us = 0u;
    return result;
}

static void input_lock(void *ctx) { (void)ctx; }
static void input_unlock(void *ctx) { (void)ctx; }
static void input_wake(void *ctx) { (void)ctx; }

static mini_result_t input_wait(void *ctx, uint32_t timeout_ms)
{
    (void)ctx;

    uint64_t start = terminal_now_us();
    uint64_t call_deadline_us = 0u;
    if (timeout_ms != MINI_WAIT_FOREVER && timeout_ms != MINI_WAIT_NONE) {
        call_deadline_us = start + (uint64_t)timeout_ms * 1000u;
    }

    for (;;) {
        uint64_t now = terminal_now_us();
        bool emitted = false;
        mini_result_t flush_result = flush_escape_if_due(now, &emitted);
        if (flush_result != MINI_OK) return flush_result;
        if (emitted) return MINI_OK;

        if (timeout_ms != MINI_WAIT_FOREVER && timeout_ms != MINI_WAIT_NONE &&
            now >= call_deadline_us) {
            return MINI_ERR_TIMEOUT;
        }

        int poll_timeout;
        if (timeout_ms == MINI_WAIT_NONE) {
            poll_timeout = 0;
        } else if (timeout_ms == MINI_WAIT_FOREVER) {
            poll_timeout = -1;
        } else {
            poll_timeout = (int)ceil_ms(call_deadline_us - now);
        }

        if (linux_terminal_parser_has_pending_escape(&s_terminal.parser) &&
            s_terminal.escape_deadline_us > now) {
            int escape_timeout = (int)ceil_ms(s_terminal.escape_deadline_us - now);
            if (poll_timeout < 0 || escape_timeout < poll_timeout) {
                poll_timeout = escape_timeout;
            }
        }

        struct pollfd descriptor = {
            .fd = STDIN_FILENO,
            .events = POLLIN,
            .revents = 0,
        };

        int ready;
        do {
            ready = poll(&descriptor, 1u, poll_timeout);
        } while (ready < 0 && errno == EINTR);

        if (ready < 0) return linux_result_from_errno(errno);
        if (ready == 0) {
            now = terminal_now_us();
            flush_result = flush_escape_if_due(now, &emitted);
            if (flush_result != MINI_OK) return flush_result;
            if (emitted) return MINI_OK;

            if (timeout_ms == MINI_WAIT_NONE) return MINI_ERR_NOT_READY;
            if (timeout_ms != MINI_WAIT_FOREVER && now >= call_deadline_us) {
                return MINI_ERR_TIMEOUT;
            }
            continue;
        }

        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) return MINI_ERR_IO;
        if ((descriptor.revents & (POLLIN | POLLHUP)) == 0) {
            if (timeout_ms == MINI_WAIT_NONE) return MINI_ERR_NOT_READY;
            continue;
        }

        unsigned char buffer[INPUT_READ_MAX];
        ssize_t count;
        do {
            count = read(STDIN_FILENO, buffer, sizeof(buffer));
        } while (count < 0 && errno == EINTR);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (timeout_ms == MINI_WAIT_NONE) return MINI_ERR_NOT_READY;
                continue;
            }
            return linux_result_from_errno(errno);
        }
        if (count == 0) {
            if (timeout_ms == MINI_WAIT_NONE) return MINI_ERR_NOT_READY;
            continue;
        }

        mini_result_t parse_result = linux_terminal_parser_feed(&s_terminal.parser,
                                                                 buffer,
                                                                 (size_t)count,
                                                                 &emitted);
        update_escape_deadline();
        if (parse_result != MINI_OK) return parse_result;
        if (emitted) return MINI_OK;
        if (timeout_ms == MINI_WAIT_NONE) return MINI_ERR_NOT_READY;
    }
}

static void input_flush(void *ctx)
{
    (void)ctx;
    linux_terminal_parser_reset(&s_terminal.parser);
    s_terminal.escape_deadline_us = 0u;
    if (isatty(STDIN_FILENO)) (void)tcflush(STDIN_FILENO, TCIFLUSH);
}

int linux_terminal_app_begin(void)
{
    if (!isatty(STDIN_FILENO)) return 0;
    if (tcgetattr(STDIN_FILENO, &s_terminal.saved_termios) != 0) return -errno;

    struct termios mode = s_terminal.saved_termios;
    mode.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    mode.c_iflag &= (tcflag_t)~(IXON | IXOFF);
    mode.c_cc[VMIN] = 0;
    mode.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &mode) != 0) return -errno;
    s_terminal.app_mode_active = true;
    return 0;
}

void linux_terminal_app_end(void)
{
    if (!s_terminal.app_mode_active) return;
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &s_terminal.saved_termios);
    s_terminal.app_mode_active = false;
}

void linux_terminal_configure(minishell_services_port_t *port)
{
    if (port == NULL) return;

    linux_terminal_parser_init(&s_terminal.parser, parser_emit, NULL);
    s_terminal.escape_deadline_us = 0u;

    port->display_capabilities = MINI_DISPLAY_CAP_TEXT;
    port->display_text_get_info = display_get_info;
    port->display_text_clear = display_clear;
    port->display_text_clear_at = display_clear_at;
    port->display_text_write_at = display_write_at;
    port->display_text_write_at_attr = display_write_at_attr;
    port->display_present = display_present;

    port->input_capabilities = MINI_INPUT_CAP_KEY;
    port->input_lock = input_lock;
    port->input_unlock = input_unlock;
    port->input_wait = input_wait;
    port->input_wake = input_wake;
    port->input_flush = input_flush;
}
