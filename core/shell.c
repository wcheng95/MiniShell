#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "app_manager.h"
#include "platform_backend.h"
#include "shell.h"

#define SHELL_LINE_MAX 256
#define SHELL_ARG_MAX 16

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
    puts(name);
}

static void cmd_help(void)
{
    puts("help              show this help");
    puts("status            show MiniShell platform/service status");
    puts("apps              list installed applications");
    puts("run <app> [...]   run an application");
    puts("<app> [...]       run an application directly");
    puts("exit              leave MiniShell");
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
    printf("platform : %s\n", minishell_platform_name());
    printf("system   : %s\n", api != NULL && api->system != NULL ? "ready" : "unavailable");
    printf("memory   : %s\n", api != NULL && api->memory != NULL ? "ready" : "unavailable");
    printf("fs       : %s\n", api != NULL && api->fs != NULL ? "ready" : "unavailable");
    printf("time     : %s\n", api != NULL && api->time_location != NULL ? "ready" : "unavailable");
    printf("display  : %s\n", api != NULL && api->display != NULL ? "ready" : "unavailable");
    printf("input    : %s\n", api != NULL && api->input != NULL ? "ready" : "unavailable");
    printf("audio    : %s\n", api_has_audio(api) ? "ready" : "unavailable");
}

static int run_app(const char *name, int argc, char **argv, int command_lookup)
{
    int app_result = 0;
    minishell_platform_result_t launch =
        minishell_app_run(name, argc, argv, &app_result);

    if (launch == MINISHELL_PLATFORM_ERR_NOT_FOUND && command_lookup) {
        printf("%s: command not found\n", name);
        return (int)launch;
    }
    if (launch == MINISHELL_PLATFORM_ERR_NOT_FOUND) {
        printf("run: %s not found\n", name);
        return (int)launch;
    }
    if (launch != MINISHELL_PLATFORM_OK) {
        printf("app: %s launch failed (%d)\n", name, (int)launch);
        return (int)launch;
    }
    if (app_result != 0) {
        printf("app: %s returned %d\n", name, app_result);
    }
    return app_result;
}

int minishell_shell_run(void)
{
    char line[SHELL_LINE_MAX];
    char *argv[SHELL_ARG_MAX];

    for (;;) {
        printf("M$> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            putchar('\n');
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
                printf("apps: failed (%d)\n", (int)result);
            }
            continue;
        }
        if (strcmp(argv[0], "run") == 0) {
            if (argc < 2) puts("usage: run <app> [args...]");
            else (void)run_app(argv[1], argc - 1, &argv[1], 0);
            continue;
        }

        (void)run_app(argv[0], argc, argv, 1);
    }
}
