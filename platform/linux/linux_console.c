#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "linux_internal.h"
#include "linux_terminal_parser.h"
#include "platform_backend.h"
#include "shell_completion.h"

static bool s_line_start = true;

void linux_console_line_unknown(void) { s_line_start = false; }

void linux_console_prepare(void)
{
    /* Prevent stdio from reading ahead across the handoff to application Input. */
    (void)setvbuf(stdin, NULL, _IONBF, 0);
}

void minishell_platform_console_write(const char *text)
{
    if (text == NULL) return;
    for (const char *p = text; *p; ++p) s_line_start = *p == '\n' || *p == '\r';
    fputs(text, stdout);
    fflush(stdout);
}

void minishell_platform_console_prompt(void)
{
    if (isatty(STDIN_FILENO) && !s_line_start) minishell_platform_console_write("\n");
    minishell_platform_console_write("M$> ");
}

static void redraw(const shell_editor_t *e)
{
    struct winsize size = {0};
    unsigned columns = 80u;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 5u)
        columns = size.ws_col;
    /* Keep one physical row, leaving a cursor cell to avoid terminal autowrap.
     * Long commands pan horizontally; the full 255-byte line remains editable. */
    size_t visible = columns - 5u;
    size_t start = e->cursor > visible ? e->cursor - visible : 0u;
    size_t count = e->length - start;
    if (count > visible) count = visible;
    fprintf(stdout, "\r\033[4C\033[K%.*s\r\033[%zuC", (int)count,
            e->line + start, 4u + e->cursor - start);
    fflush(stdout);
}

static uint64_t completion_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0u;
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

typedef struct {
    shell_editor_t *editor;
    shell_completion_pending_t completion;
    bool finished;
    int result;
} shell_input_t;

static mini_result_t shell_event(void *ctx, const mini_key_event_t *event)
{
    shell_input_t *input = ctx;
    shell_editor_t *e = input->editor;
    if (input->finished) return MINI_OK;
    shell_edit_action_t action;
    if (event->type == MINI_KEY_EVENT_CHAR) {
        if (event->modifiers & MINI_MOD_CTRL) {
            if (event->codepoint == 'd' && e->length == 0u) {
                input->finished = true;
                input->result = 0;
            } else if (event->codepoint == 'c') {
                shell_editor_begin(e);
                redraw(e);
                input->finished = true;
            }
            return MINI_OK;
        }
        action = SHELL_EDIT_CHAR;
    } else if (event->type == MINI_KEY_EVENT_SPECIAL) {
        switch (event->key) {
        case MINI_KEY_ENTER: input->finished = true; return MINI_OK;
        case MINI_KEY_UP: action = SHELL_EDIT_PREVIOUS; break;
        case MINI_KEY_DOWN: action = SHELL_EDIT_NEXT; break;
        case MINI_KEY_LEFT: action = SHELL_EDIT_LEFT; break;
        case MINI_KEY_RIGHT: action = SHELL_EDIT_RIGHT; break;
        case MINI_KEY_HOME: action = SHELL_EDIT_HOME; break;
        case MINI_KEY_END: action = SHELL_EDIT_END; break;
        case MINI_KEY_BACKSPACE: action = SHELL_EDIT_BACKSPACE; break;
        case MINI_KEY_DELETE: action = SHELL_EDIT_DELETE; break;
        default: return MINI_OK; /* PageUp/Down are not command history. */
        }
    } else return MINI_OK;
    if (shell_editor_edit(e, action, event->codepoint)) {
        if (action == SHELL_EDIT_CHAR)
            shell_completion_defer(&input->completion, completion_now());
        redraw(e);
    }
    return ferror(stdout) ? MINI_ERR_IO : MINI_OK;
}

int minishell_platform_console_read_line(shell_editor_t *editor)
{
    if (!isatty(STDIN_FILENO)) {
        if (fgets(editor->line, sizeof(editor->line), stdin) != NULL) return 1;
        return ferror(stdin) ? -1 : 0;
    }
    if (linux_terminal_shell_begin() != 0) return -1;
    shell_input_t input = {.editor = editor, .result = 2};
    linux_terminal_parser_t parser;
    linux_terminal_parser_init(&parser, shell_event, &input);
    while (!input.finished) {
        struct pollfd fd = {.fd = STDIN_FILENO, .events = POLLIN};
        int timeout = linux_terminal_parser_has_pending_escape(&parser) ? 30 : -1;
        int completion_timeout = shell_completion_timeout_ms(&input.completion, completion_now());
        if (completion_timeout >= 0 && (timeout < 0 || completion_timeout < timeout))
            timeout = completion_timeout;
        int ready = poll(&fd, 1u, timeout);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0 || (fd.revents & (POLLERR | POLLNVAL))) { input.result = -1; break; }
        mini_result_t result;
        if (ready == 0) {
            result = linux_terminal_parser_flush_escape(&parser, NULL);
            if (shell_completion_poll(&input.completion, editor, completion_now())) redraw(editor);
            if (ferror(stdout)) result = MINI_ERR_IO;
        } else {
            /* One byte at a time avoids consuming the next app/prompt's input. */
            unsigned char byte;
            ssize_t count = read(STDIN_FILENO, &byte, 1u);
            if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (count <= 0) { input.result = count == 0 ? 0 : -1; break; }
            /* Cancel even for a partial escape or ignored control byte. */
            input.completion.pending = false;
            result = linux_terminal_parser_feed(&parser, &byte, 1u, NULL);
        }
        if (result != MINI_OK) { input.result = -1; break; }
    }
    linux_terminal_app_end();
    if (input.result == 2) {
        /* Commit a full command to the host scrollback, including panned text. */
        fprintf(stdout, "\r\033[4C\033[K%s\n", editor->line);
        fflush(stdout);
        s_line_start = true;
    }
    return input.result;
}
