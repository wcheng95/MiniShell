#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minishell_app.h"
#include "minishell_platform.h"
#include "minishell_power.h"
#include "minishell_shell.h"
#include "minishell_transfer.h"

#define SHELL_LINE_MAX 256
#define SHELL_ARG_MAX 16
#define SHELL_REPEAT_MAX 10000ul

static bool s_ignore_lf;

static bool read_line(char *line, size_t size)
{
    size_t len = 0;

    printf("M$> ");
    fflush(stdout);

    for (;;) {
        int ch = getchar();
        if (ch == EOF) {
            clearerr(stdin);
            continue;
        }

        if (s_ignore_lf && ch == '\n') {
            s_ignore_lf = false;
            continue;
        }
        s_ignore_lf = false;

        if (ch == '\r' || ch == '\n') {
            if (ch == '\r') {
                s_ignore_lf = true;
            }
            line[len] = '\0';
            putchar('\n');
            fflush(stdout);
            return true;
        }

        if (ch == 0x03) { /* Ctrl-C */
            line[0] = '\0';
            printf("^C\n");
            fflush(stdout);
            return true;
        }

        if (ch == '\b' || ch == 0x7f) {
            if (len > 0) {
                len--;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }

        if (isprint((unsigned char)ch)) {
            if (len + 1 < size) {
                line[len++] = (char)ch;
                putchar(ch);
                fflush(stdout);
            } else {
                putchar('\a');
                fflush(stdout);
            }
        }
    }
}

static int split_args(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *save = NULL;
    char *token = strtok_r(line, " \t", &save);

    while (token != NULL && argc < max_args) {
        argv[argc++] = token;
        token = strtok_r(NULL, " \t", &save);
    }

    return argc;
}

static void cmd_help(void)
{
    printf("help              show this help\n");
    printf("status            show platform and power status\n");
    printf("suspend           enter deep sleep; wake restarts MiniShell\n");
    printf("poweroff          power off the device\n");
    printf("ls [path]         list a directory\n");
    printf("put <path>        receive a file from host\n");
    printf("get <path>        send a file to host\n");
    printf("exec <app> [...]  run /sd/apps/<app>.elf\n");
    printf("repeat N <app>    run an app N times (stress/lifecycle test)\n");
    printf("<app> [...]       run an app as a shell command\n");
}

static void cmd_status(void)
{
    printf("platform : %s\n", minishell_platform_name());
    printf("console  : %s\n", minishell_platform_console_status());
    printf("sd       : %s\n", minishell_platform_sd_status());
    printf("app path : /sd/apps\n");

    minishell_power_status_t power;
    if (minishell_power_get_status(&power) != 0) {
        printf("battery  : unavailable\n");
        printf("charging : unavailable\n");
        return;
    }

    if (power.battery_percent_valid) {
        printf("battery  : %u%%\n", (unsigned)power.battery_percent);
    } else {
        printf("battery  : unavailable\n");
    }

    if (power.charging_valid) {
        printf("charging : %s\n", power.charging ? "yes" : "no");
    } else {
        printf("charging : unavailable\n");
    }
}

static void cmd_suspend(void)
{
    printf("suspend: entering deep sleep; wake restarts MiniShell\n");
    fflush(stdout);
    int result = minishell_power_suspend();
    printf("suspend: failed (%d)\n", result);
}

static void cmd_poweroff(void)
{
    printf("poweroff: shutting down\n");
    fflush(stdout);
    int result = minishell_power_poweroff();
    printf("poweroff: failed (%d)\n", result);
}

static int cmd_ls(const char *path)
{
    if (strcmp(path, "/") == 0) {
        printf("sd/\n");
        return 0;
    }

    if (strncmp(path, "/sd", 3) == 0 && !minishell_platform_sd_ready()) {
        printf("ls: SD is not mounted (%s)\n", minishell_platform_sd_status());
        return -1;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        printf("ls: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        printf("%s\n", entry->d_name);
    }

    closedir(dir);
    return 0;
}

static int run_app(const char *command, int argc, char **argv, bool command_lookup)
{
    if (!minishell_platform_sd_ready()) {
        printf("%s: SD is not mounted (%s)\n", command, minishell_platform_sd_status());
        return -ENODEV;
    }

    int ret = minishell_app_run(command, argc, argv);

    if (ret == -ENOENT && command_lookup) {
        printf("%s: command not found\n", command);
    } else if (ret == -ENOENT) {
        printf("exec: %s not found in /sd/apps\n", command);
    } else if (ret != 0) {
        printf("app: %s returned %d\n", command, ret);
    }

    return ret;
}

static void cmd_repeat(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: repeat <count> <app> [args...]\n");
        return;
    }

    char *end = NULL;
    errno = 0;
    unsigned long count = strtoul(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' ||
        count == 0ul || count > SHELL_REPEAT_MAX) {
        printf("repeat: count must be 1..%lu\n", SHELL_REPEAT_MAX);
        return;
    }

    if (!minishell_platform_sd_ready()) {
        printf("repeat: SD is not mounted (%s)\n", minishell_platform_sd_status());
        return;
    }

    const char *app = argv[2];
    for (unsigned long i = 1ul; i <= count; ++i) {
        int ret = minishell_app_run(app, argc - 2, &argv[2]);
        if (ret != 0) {
            printf("repeat: FAIL at %lu/%lu, %s returned %d\n", i, count, app, ret);
            return;
        }
    }

    printf("repeat: PASS %lu/%lu %s\n", count, count, app);
}

static void cmd_put(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: put <remote-path>\n");
        return;
    }
    (void)minishell_transfer_put(argv[1]);
}

static void cmd_get(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: get <remote-path>\n");
        return;
    }
    (void)minishell_transfer_get(argv[1]);
}

void minishell_shell_run(void)
{
    char line[SHELL_LINE_MAX];
    char *argv[SHELL_ARG_MAX];

    printf("type 'help' for commands\n");

    for (;;) {
        if (!read_line(line, sizeof(line))) {
            continue;
        }

        int argc = split_args(line, argv, SHELL_ARG_MAX);
        if (argc == 0) {
            continue;
        }

        if (strcmp(argv[0], "help") == 0) {
            cmd_help();
            continue;
        }

        if (strcmp(argv[0], "status") == 0) {
            cmd_status();
            continue;
        }

        if (strcmp(argv[0], "suspend") == 0) {
            if (argc != 1) printf("usage: suspend\n");
            else cmd_suspend();
            continue;
        }

        if (strcmp(argv[0], "poweroff") == 0) {
            if (argc != 1) printf("usage: poweroff\n");
            else cmd_poweroff();
            continue;
        }

        if (strcmp(argv[0], "ls") == 0) {
            if (argc > 2) {
                printf("usage: ls [path]\n");
            } else {
                (void)cmd_ls(argc == 2 ? argv[1] : "/");
            }
            continue;
        }

        if (strcmp(argv[0], "put") == 0) {
            cmd_put(argc, argv);
            continue;
        }

        if (strcmp(argv[0], "get") == 0) {
            cmd_get(argc, argv);
            continue;
        }

        if (strcmp(argv[0], "exec") == 0) {
            if (argc < 2) {
                printf("usage: exec <app> [args...]\n");
            } else {
                (void)run_app(argv[1], argc - 1, &argv[1], false);
            }
            continue;
        }

        if (strcmp(argv[0], "repeat") == 0) {
            cmd_repeat(argc, argv);
            continue;
        }

        (void)run_app(argv[0], argc, argv, true);
    }
}
