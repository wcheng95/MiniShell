#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "app_manager.h"
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
        "exit              leave minishell\n");
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

int minishell_shell_run(void)
{
    char line[SHELL_LINE_MAX];
    char *argv[SHELL_ARG_MAX];

    for (;;) {
        shell_write("M$> ");

        if (minishell_platform_console_read_line(line, sizeof(line)) <= 0) {
            shell_write("\n");
            return 0;
        }

        int argc = split_args(line, argv, SHELL_ARG_MAX);
        if (argc == 0) continue;

        if (strcmp(argv[0], "exit") == 0) return 0;
        if (strcmp(argv[0], "help") == 0) {
            cmd_help();
            continue;
        }
        if (strcmp(argv[0], "status") == 0) {
            cmd_status();
            continue;
        }
        if (strcmp(argv[0], "apps") == 0) {
            minishell_platform_result_t result = minishell_app_list(show_app, NULL);
            if (result != MINISHELL_PLATFORM_OK) {
                shell_printf("apps: failed (%d)\n", (int)result);
            }
            continue;
        }
        if (strcmp(argv[0], "run") == 0) {
            if (argc < 2) shell_write("usage: run <app> [args...]\n");
            else (void)run_app(argv[1], argc - 1, &argv[1], 0);
            continue;
        }

        (void)run_app(argv[0], argc, argv, 1);
    }
}
