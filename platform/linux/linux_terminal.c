#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "linux_internal.h"

#define INPUT_READ_MAX 64u

typedef struct {
    struct termios saved_termios;
    bool app_mode_active;
} terminal_state_t;

static terminal_state_t s_terminal;

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

static mini_result_t submit_special(uint32_t key, uint32_t modifiers)
{
    mini_key_event_t event = {
        .struct_size = sizeof(event),
        .type = MINI_KEY_EVENT_SPECIAL,
        .codepoint = 0u,
        .key = key,
        .modifiers = modifiers,
    };
    return minishell_services_input_submit(&event);
}

static mini_result_t submit_char(uint32_t codepoint, uint32_t modifiers)
{
    mini_key_event_t event = {
        .struct_size = sizeof(event),
        .type = MINI_KEY_EVENT_CHAR,
        .codepoint = codepoint,
        .key = 0u,
        .modifiers = modifiers,
    };
    return minishell_services_input_submit(&event);
}

static size_t decode_utf8(const unsigned char *bytes, size_t available,
                          uint32_t *out_codepoint)
{
    if (available == 0u || out_codepoint == NULL) return 0u;
    unsigned char first = bytes[0];
    if (first < 0x80u) {
        *out_codepoint = first;
        return 1u;
    }

    size_t needed;
    uint32_t codepoint;
    if ((first & 0xE0u) == 0xC0u) {
        needed = 2u;
        codepoint = first & 0x1Fu;
    } else if ((first & 0xF0u) == 0xE0u) {
        needed = 3u;
        codepoint = first & 0x0Fu;
    } else if ((first & 0xF8u) == 0xF0u) {
        needed = 4u;
        codepoint = first & 0x07u;
    } else {
        return 0u;
    }
    if (available < needed) return 0u;

    for (size_t i = 1u; i < needed; ++i) {
        if ((bytes[i] & 0xC0u) != 0x80u) return 0u;
        codepoint = (codepoint << 6) | (uint32_t)(bytes[i] & 0x3Fu);
    }

    if ((needed == 2u && codepoint < 0x80u) ||
        (needed == 3u && codepoint < 0x800u) ||
        (needed == 4u && codepoint < 0x10000u) ||
        codepoint > 0x10FFFFu ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
        return 0u;
    }

    *out_codepoint = codepoint;
    return needed;
}

/* Deliberately preserves the current stateless parser behavior.  H5 evaluates
 * carrying incomplete ANSI/CSI/UTF-8 sequences across read boundaries later. */
static bool submit_input_bytes(const unsigned char *bytes, size_t count)
{
    bool submitted = false;
    size_t i = 0u;
    while (i < count) {
        unsigned char ch = bytes[i];

        if (ch == 0x1bu) {
            if (i + 2u < count && bytes[i + 1u] == '[') {
                unsigned char code = bytes[i + 2u];
                uint32_t key = 0u;
                if (code == 'A') key = MINI_KEY_UP;
                else if (code == 'B') key = MINI_KEY_DOWN;
                else if (code == 'C') key = MINI_KEY_RIGHT;
                else if (code == 'D') key = MINI_KEY_LEFT;
                else if (code == 'H') key = MINI_KEY_HOME;
                else if (code == 'F') key = MINI_KEY_END;
                if (key != 0u) {
                    (void)submit_special(key, 0u);
                    submitted = true;
                    i += 3u;
                    continue;
                }

                if (i + 3u < count && bytes[i + 3u] == '~') {
                    if (code == '2') key = MINI_KEY_INSERT;
                    else if (code == '3') key = MINI_KEY_DELETE;
                    else if (code == '5') key = MINI_KEY_PAGE_UP;
                    else if (code == '6') key = MINI_KEY_PAGE_DOWN;
                    if (key != 0u) {
                        (void)submit_special(key, 0u);
                        submitted = true;
                        i += 4u;
                        continue;
                    }
                }
            }
            (void)submit_special(MINI_KEY_ESCAPE, 0u);
            submitted = true;
            ++i;
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            (void)submit_special(MINI_KEY_ENTER, 0u);
            submitted = true;
            ++i;
            continue;
        }
        if (ch == '\t') {
            (void)submit_special(MINI_KEY_TAB, 0u);
            submitted = true;
            ++i;
            continue;
        }
        if (ch == 0x08u || ch == 0x7fu) {
            (void)submit_special(MINI_KEY_BACKSPACE, 0u);
            submitted = true;
            ++i;
            continue;
        }
        if (ch >= 1u && ch <= 26u) {
            (void)submit_char((uint32_t)('a' + ch - 1u), MINI_MOD_CTRL);
            submitted = true;
            ++i;
            continue;
        }
        if (ch >= 0x20u && ch < 0x7fu) {
            (void)submit_char(ch, 0u);
            submitted = true;
            ++i;
            continue;
        }
        if (ch >= 0x80u) {
            uint32_t codepoint = 0u;
            size_t used = decode_utf8(&bytes[i], count - i, &codepoint);
            if (used != 0u) {
                (void)submit_char(codepoint, 0u);
                submitted = true;
                i += used;
                continue;
            }
        }
        ++i;
    }
    return submitted;
}

static void input_lock(void *ctx) { (void)ctx; }
static void input_unlock(void *ctx) { (void)ctx; }
static void input_wake(void *ctx) { (void)ctx; }

static mini_result_t input_wait(void *ctx, uint32_t timeout_ms)
{
    (void)ctx;
    struct pollfd descriptor = {
        .fd = STDIN_FILENO,
        .events = POLLIN,
        .revents = 0,
    };

    int timeout;
    if (timeout_ms == MINI_WAIT_FOREVER) timeout = -1;
    else if (timeout_ms > (uint32_t)INT_MAX) timeout = INT_MAX;
    else timeout = (int)timeout_ms;

    int ready;
    do {
        ready = poll(&descriptor, 1u, timeout);
    } while (ready < 0 && errno == EINTR);

    if (ready < 0) return linux_result_from_errno(errno);
    if (ready == 0) return timeout_ms == MINI_WAIT_NONE ? MINI_ERR_NOT_READY : MINI_ERR_TIMEOUT;
    if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) return MINI_ERR_IO;
    if ((descriptor.revents & (POLLIN | POLLHUP)) == 0) return MINI_ERR_NOT_READY;

    unsigned char buffer[INPUT_READ_MAX];
    ssize_t count;
    do {
        count = read(STDIN_FILENO, buffer, sizeof(buffer));
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return MINI_ERR_NOT_READY;
        return linux_result_from_errno(errno);
    }
    if (count == 0) return MINI_ERR_NOT_READY;

    return submit_input_bytes(buffer, (size_t)count) ? MINI_OK : MINI_ERR_NOT_READY;
}

static void input_flush(void *ctx)
{
    (void)ctx;
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
