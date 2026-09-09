#include <assert.h>
#include <string.h>

#include "platform_backend.h"

static int s_seen_hello;

static void capture_app(const char *name, void *ctx)
{
    (void)ctx;
    if (name != NULL && strcmp(name, "hello") == 0) {
        ++s_seen_hello;
    }
}

int minishell_app_hello_main(int argc, char **argv)
{
    assert(argc == 2);
    assert(argv != NULL);
    assert(strcmp(argv[0], "hello") == 0);
    assert(strcmp(argv[1], "probe") == 0);
    return 23;
}

int main(void)
{
    assert(minishell_platform_apps_list(NULL, NULL) == MINISHELL_PLATFORM_ERR_INVALID);

    s_seen_hello = 0;
    assert(minishell_platform_apps_list(capture_app, NULL) == MINISHELL_PLATFORM_OK);
    assert(s_seen_hello == 1);

    char *argv[] = {(char *)"hello", (char *)"probe"};
    int app_result = 0;
    assert(minishell_platform_app_run("hello", 2, argv, &app_result) == MINISHELL_PLATFORM_OK);
    assert(app_result == 23);

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
