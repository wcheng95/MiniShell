#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "app_manager.h"
#include "platform_backend.h"
#include "shell.h"

#define SHELL_LINE_MAX 256
#define SHELL_ARG_MAX 16

static int split_args(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *save = NULL;
    char *token = strtok_r(line, " \t\r\n", &save);

    while (token != NULL && argc < max_args) {
        argv[argc++] = token;
        token = strtok_r(NULL, " \t\r\n", &save);
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
    int ret = minishell_app_run(name, argc, argv);

    if (ret == -ENOENT && command_lookup) {
        printf("%s: command not found\n", name);
    } else if (ret == -ENOENT) {
        printf("run: %s not found\n", name);
    } else if (ret != 0) {
        printf("app: %s returned %d\n", name, ret);
    }

    return ret;
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
        if (argc == 0) {
            continue;
        }

        if (strcmp(argv[0], "exit") == 0) {
            return 0;
        }

        if (strcmp(argv[0], "help") == 0) {
            cmd_help();
            continue;
        }

        if (strcmp(argv[0], "status") == 0) {
            cmd_status();
            continue;
        }

        if (strcmp(argv[0], "apps") == 0) {
            int ret = minishell_app_list(show_app, NULL);
            if (ret != 0) {
                printf("apps: failed (%d)\n", ret);
            }
            continue;
        }

        if (strcmp(argv[0], "run") == 0) {
            if (argc < 2) {
                puts("usage: run <app> [args...]");
            } else {
                (void)run_app(argv[1], argc - 1, &argv[1], 0);
            }
            continue;
        }

        (void)run_app(argv[0], argc, argv, 1);
    }
}
