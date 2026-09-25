#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "app_manager.h"
#include "alias.h"
#include "platform_backend.h"
#include "shell.h"

#define SHELL_LINE_MAX 256
#define SHELL_ARG_MAX 16
#define SHELL_OUTPUT_MAX 512

static void shell_write(const char *text)
{
    minishell_platform_console_write(text);
}

static void shell_printf(const char *format, ...)
{
    char buffer[SHELL_OUTPUT_MAX];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (written < 0) return;
    buffer[sizeof(buffer) - 1u] = '\0';
    shell_write(buffer);
}

static int is_shell_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static bool is_builtin(const char *name, size_t length)
{
    static const char *const names[] = {"exit", "help", "status", "apps", "run"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strlen(names[i]) == length && memcmp(names[i], name, length) == 0) return true;
    return false;
}

/* Read the whole stream to honor last-definition-wins; never retain a table. */
static int lookup_alias(const char *name, size_t length, char replacement[SHELL_LINE_MAX])
{
    const mini_api_t *api = mini_api_get();
    const mini_fs_api_t *fs = api ? api->fs : NULL;
    if (!fs || !fs->open || !fs->read || !fs->close) return 0;
    mini_file_t file = MINI_FILE_INVALID;
    mini_result_t result = fs->open("/flash/minishell/alias.txt", MINI_FS_READ, &file);
    if (result == MINI_ERR_NOT_FOUND || result == MINI_ERR_UNSUPPORTED) return 0;
    if (result != MINI_OK) return -1;

    char record[SHELL_LINE_MAX], chunk[128];
    size_t used = 0;
    bool skip = false, found = false, ok = true;
    for (;;) {
        uint32_t count = 0;
        result = fs->read(file, chunk, sizeof(chunk), &count);
        if (result != MINI_OK || count > sizeof(chunk)) { ok = false; break; }
        for (uint32_t i = 0; i < count; ++i) {
            if (chunk[i] == '\n') {
                if (!skip) {
                    record[used] = '\0';
                    const char *rhs = minishell_alias_match(record, name, length);
                    if (rhs) { strcpy(replacement, rhs); found = true; }
                }
                used = 0;
                skip = false;
            } else if (!skip) {
                if (chunk[i] == '\0' || used == sizeof(record) - 1) skip = true;
                else record[used++] = chunk[i];
            }
        }
        if (count == 0) break;
    }
    if (ok && !skip && used) {
        record[used] = '\0';
        const char *rhs = minishell_alias_match(record, name, length);
        if (rhs) { strcpy(replacement, rhs); found = true; }
    }
    if (fs->close(file) != MINI_OK) ok = false;
    return ok ? (found ? 1 : 0) : -1;
}

static int expand_alias(const char *line, char expanded[SHELL_LINE_MAX])
{
    const char *name = line;
    while (is_shell_space(*name)) ++name;
    const char *end = name;
    while (*end && !is_shell_space(*end)) ++end;
    size_t length = (size_t)(end - name);
    if (!length || is_builtin(name, length)) return 0;
    char replacement[SHELL_LINE_MAX];
    int found = lookup_alias(name, length, replacement);
    if (found < 0) { shell_write("alias: cannot read alias file\n"); return 0; }
    if (!found) return 0;
    while (is_shell_space(*end)) ++end;
    if (!minishell_alias_expand(replacement, end, expanded, SHELL_LINE_MAX)) {
        shell_write("alias: expansion too long\n");
        return -1;
    }
    return 1;
}

static int split_args(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *p = line;

    while (*p != '\0' && argc < max_args) {
        while (is_shell_space(*p)) ++p;
        if (*p == '\0') break;

        argv[argc++] = p;
        while (*p != '\0' && !is_shell_space(*p)) ++p;
        if (*p != '\0') *p++ = '\0';
    }

    return argc;
}

static void show_app(const char *name, void *ctx)
{
    (void)ctx;
    shell_write(name);
    shell_write("\n");
}

static void cmd_help(void)
{
    shell_write(
        "help              show this help\n"
        "status            show minishell platform/service status\n"
        "apps              list installed applications\n"
        "run <app> [...]   run an application\n"
        "<app> [...]       run an application directly\n"
        "exit              leave minishell\n"
        "aliases           /flash/minishell/alias.txt\n");
}

static int api_has_audio(const mini_api_t *api)
{
    if (api == NULL) return 0;
    const size_t end = offsetof(mini_api_t, audio) + sizeof(api->audio);
    return api->struct_size >= end && api->audio != NULL;
}

static void cmd_status(void)
{
    const mini_api_t *api = mini_api_get();
    shell_printf("platform : %s\n", minishell_platform_name());
    shell_printf("system   : %s\n", api != NULL && api->system != NULL ? "ready" : "unavailable");
    shell_printf("console  : %s\n", api != NULL && api->console != NULL ? "ready" : "unavailable");
    shell_printf("memory   : %s\n", api != NULL && api->memory != NULL ? "ready" : "unavailable");
    shell_printf("fs       : %s\n", api != NULL && api->fs != NULL ? "ready" : "unavailable");
    shell_printf("time     : %s\n", api != NULL && api->time_location != NULL ? "ready" : "unavailable");
    shell_printf("display  : %s\n", api != NULL && api->display != NULL ? "ready" : "unavailable");
    shell_printf("input    : %s\n", api != NULL && api->input != NULL ? "ready" : "unavailable");
    shell_printf("audio    : %s\n", api_has_audio(api) ? "ready" : "unavailable");
}

static int run_app(const char *name, int argc, char **argv, int command_lookup)
{
    int app_result = 0;
    minishell_platform_result_t launch =
        minishell_app_run(name, argc, argv, &app_result);

    if (launch == MINISHELL_PLATFORM_ERR_NOT_FOUND && command_lookup) {
        shell_printf("%s: command not found\n", name);
        return (int)launch;
    }
    if (launch == MINISHELL_PLATFORM_ERR_NOT_FOUND) {
        shell_printf("run: %s not found\n", name);
        return (int)launch;
    }
    if (launch != MINISHELL_PLATFORM_OK) {
        shell_printf("app: %s launch failed (%d)\n", name, (int)launch);
        return (int)launch;
    }
    if (app_result != 0) {
        shell_printf("app: %s returned %d\n", name, app_result);
    }
    return app_result;
}

/* True is an exit request, honored only by the interactive prompt loop. */
static bool execute_line(char *line)
{
    char expanded[SHELL_LINE_MAX];
    char *argv[SHELL_ARG_MAX];
    if (strlen(line) >= SHELL_LINE_MAX) {
        shell_write("shell: command too long\n");
        return false;
    }
    int expansion = expand_alias(line, expanded);
    if (expansion < 0) return false;
    int argc = split_args(expansion ? expanded : line, argv, SHELL_ARG_MAX);
    if (argc == 0) return false;

    if (strcmp(argv[0], "exit") == 0) return true;
    if (strcmp(argv[0], "help") == 0) {
        cmd_help();
        return false;
    }
    if (strcmp(argv[0], "status") == 0) {
        cmd_status();
        return false;
    }
    if (strcmp(argv[0], "apps") == 0) {
        minishell_platform_result_t result = minishell_app_list(show_app, NULL);
        if (result != MINISHELL_PLATFORM_OK) {
            shell_printf("apps: failed (%d)\n", (int)result);
        }
        return false;
    }
    if (strcmp(argv[0], "run") == 0) {
        if (argc < 2) shell_write("usage: run <app> [args...]\n");
        else (void)run_app(argv[1], argc - 1, &argv[1], 0);
        return false;
    }

    (void)run_app(argv[0], argc, argv, 1);
    return false;
}

void minishell_shell_startup(char *commands)
{
    char *segment = commands;
    while (segment && *segment) {
        char *next = strchr(segment, ';');
        if (next) *next++ = '\0';
        (void)execute_line(segment);
        segment = next;
    }
}

int minishell_shell_run(void)
{
    char line[SHELL_LINE_MAX];
    for (;;) {
        shell_write("M$> ");
        if (minishell_platform_console_read_line(line, sizeof(line)) <= 0) {
            shell_write("\n");
            return 0;
        }
        if (execute_line(line)) return 0;
    }
}
