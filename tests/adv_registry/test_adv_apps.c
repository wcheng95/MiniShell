#include <assert.h>
#include <string.h>

#include "platform_backend.h"

static int s_seen_hello;
static int s_seen_probe;
static int s_seen_a3probe;
static int s_seen_date;
static int s_seen_free;
static int s_seen_ls;
static int s_seen_cat;
static int s_seen_ft8;

static void capture_app(const char *name, void *ctx)
{
    (void)ctx;
    if (name == NULL) return;
    if (strcmp(name, "hello") == 0) ++s_seen_hello;
    else if (strcmp(name, "probe") == 0) ++s_seen_probe;
    else if (strcmp(name, "a3probe") == 0) ++s_seen_a3probe;
    else if (strcmp(name, "date") == 0) ++s_seen_date;
    else if (strcmp(name, "free") == 0) ++s_seen_free;
    else if (strcmp(name, "ls") == 0) ++s_seen_ls;
    else if (strcmp(name, "cat") == 0) ++s_seen_cat;
    else if (strcmp(name, "ft8") == 0) ++s_seen_ft8;
}

int minishell_app_hello_main(int argc, char **argv)
{
    assert(argc == 2);
    assert(argv != NULL);
    assert(strcmp(argv[0], "hello") == 0);
    assert(strcmp(argv[1], "probe") == 0);
    return 23;
}

int minishell_app_a2_probe_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

int minishell_app_a3_probe_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

int minishell_app_date_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

int minishell_app_free_main(int argc, char **argv)
{
    assert(argc == 1);
    assert(argv != NULL);
    assert(strcmp(argv[0], "free") == 0);
    return 29;
}

int minishell_app_ls_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

int minishell_app_cat_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

int minishell_app_ft8_main(int argc, char **argv)
{
    assert(argc == 1);
    assert(argv != NULL);
    assert(strcmp(argv[0], "ft8") == 0);
    return 31;
}

int main(void)
{
    assert(minishell_platform_apps_list(NULL, NULL) == MINISHELL_PLATFORM_ERR_INVALID);

    s_seen_hello = 0;
    s_seen_probe = 0;
    s_seen_a3probe = 0;
    s_seen_date = 0;
    s_seen_free = 0;
    s_seen_ls = 0;
    s_seen_cat = 0;
    s_seen_ft8 = 0;
    assert(minishell_platform_apps_list(capture_app, NULL) == MINISHELL_PLATFORM_OK);
    assert(s_seen_hello == 1);
    assert(s_seen_probe == 1);
    assert(s_seen_a3probe == 1);
    assert(s_seen_date == 1);
    assert(s_seen_free == 1);
    assert(s_seen_ls == 1);
    assert(s_seen_cat == 1);
    assert(s_seen_ft8 == 1);

    char *hello_argv[] = {(char *)"hello", (char *)"probe"};
    int app_result = 0;
    assert(minishell_platform_app_run("hello", 2, hello_argv, &app_result) ==
           MINISHELL_PLATFORM_OK);
    assert(app_result == 23);

    char *free_argv[] = {(char *)"free"};
    assert(minishell_platform_app_run("free", 1, free_argv, &app_result) ==
           MINISHELL_PLATFORM_OK);
    assert(app_result == 29);

    char *ft8_argv[] = {(char *)"ft8"};
    assert(minishell_platform_app_run("ft8", 1, ft8_argv, &app_result) ==
           MINISHELL_PLATFORM_OK);
    assert(app_result == 31);

    assert(minishell_platform_app_run("missing", 0, NULL, &app_result) ==
           MINISHELL_PLATFORM_ERR_NOT_FOUND);
    assert(minishell_platform_app_run("bad/name", 0, NULL, &app_result) ==
           MINISHELL_PLATFORM_ERR_INVALID);
    assert(minishell_platform_app_run(NULL, 0, NULL, &app_result) ==
           MINISHELL_PLATFORM_ERR_INVALID);
    assert(minishell_platform_app_run("hello", 0, NULL, NULL) ==
           MINISHELL_PLATFORM_ERR_INVALID);

    return 0;
}
