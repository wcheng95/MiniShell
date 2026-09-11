#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "adv_elf_loader.h"
#include "platform_backend.h"

static int s_seen_hello;
static int s_seen_probe;
static int s_seen_a3probe;
static int s_seen_date;
static int s_seen_free;
static int s_seen_ls;
static int s_seen_cat;
static int s_seen_cp;
static int s_seen_mv;
static int s_seen_rm;
static int s_seen_mkdir;
static int s_seen_rmdir;
static int s_seen_nano;
static int s_seen_ft8;
static int s_seen_usbmsc;
static int s_seen_elfhello;
static int s_seen_sdonly;

static bool s_flash_elfhello = true;
static int s_external_runs;
static const char *s_last_external_root;

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
    else if (strcmp(name, "cp") == 0) ++s_seen_cp;
    else if (strcmp(name, "mv") == 0) ++s_seen_mv;
    else if (strcmp(name, "rm") == 0) ++s_seen_rm;
    else if (strcmp(name, "mkdir") == 0) ++s_seen_mkdir;
    else if (strcmp(name, "rmdir") == 0) ++s_seen_rmdir;
    else if (strcmp(name, "nano") == 0) ++s_seen_nano;
    else if (strcmp(name, "ft8") == 0) ++s_seen_ft8;
    else if (strcmp(name, "usbmsc") == 0) ++s_seen_usbmsc;
    else if (strcmp(name, "elfhello") == 0) ++s_seen_elfhello;
    else if (strcmp(name, "sdonly") == 0) ++s_seen_sdonly;
}

bool adv_elf_loader_app_exists(const char *root, const char *name)
{
    if (root == NULL || name == NULL) return false;
    if (strcmp(root, "/flash") == 0) {
        if (strcmp(name, "hello") == 0) return true;
        if (strcmp(name, "elfhello") == 0) return s_flash_elfhello;
        return false;
    }
    if (strcmp(root, "/sd") == 0) {
        return strcmp(name, "elfhello") == 0 || strcmp(name, "sdonly") == 0;
    }
    return false;
}

minishell_platform_result_t adv_elf_loader_apps_list(const char *root,
                                                     minishell_app_emit_fn emit,
                                                     void *ctx)
{
    if (root == NULL || emit == NULL) return MINISHELL_PLATFORM_ERR_INVALID;

    if (strcmp(root, "/flash") == 0) {
        emit("hello", ctx); /* must be hidden by the compiled-in app */
        if (s_flash_elfhello) emit("elfhello", ctx);
        return MINISHELL_PLATFORM_OK;
    }
    if (strcmp(root, "/sd") == 0) {
        emit("elfhello", ctx);
        emit("sdonly", ctx);
        return MINISHELL_PLATFORM_OK;
    }
    return MINISHELL_PLATFORM_ERR_INVALID;
}

minishell_platform_result_t adv_elf_loader_app_run(const char *root,
                                                   const char *name,
                                                   int argc,
                                                   char **argv,
                                                   int *out_app_result)
{
    (void)argc;
    (void)argv;
    if (root == NULL || name == NULL || out_app_result == NULL) {
        return MINISHELL_PLATFORM_ERR_INVALID;
    }

    ++s_external_runs;
    s_last_external_root = root;
    *out_app_result = strcmp(root, "/flash") == 0 ? 41 : 42;
    return MINISHELL_PLATFORM_OK;
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

int minishell_app_cp_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

int minishell_app_mv_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

int minishell_app_rm_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

int minishell_app_mkdir_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

int minishell_app_rmdir_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

int minishell_app_nano_main(int argc, char **argv)
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

int minishell_app_usbmsc_main(int argc, char **argv)
{
    assert(argc == 1);
    assert(argv != NULL);
    assert(strcmp(argv[0], "usbmsc") == 0);
    return 37;
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
    s_seen_cp = 0;
    s_seen_mv = 0;
    s_seen_rm = 0;
    s_seen_mkdir = 0;
    s_seen_rmdir = 0;
    s_seen_nano = 0;
    s_seen_ft8 = 0;
    s_seen_usbmsc = 0;
    s_seen_elfhello = 0;
    s_seen_sdonly = 0;
    assert(minishell_platform_apps_list(capture_app, NULL) == MINISHELL_PLATFORM_OK);
    assert(s_seen_hello == 1);
    assert(s_seen_probe == 1);
    assert(s_seen_a3probe == 1);
    assert(s_seen_date == 1);
    assert(s_seen_free == 1);
    assert(s_seen_ls == 1);
    assert(s_seen_cat == 1);
    assert(s_seen_cp == 1);
    assert(s_seen_mv == 1);
    assert(s_seen_rm == 1);
    assert(s_seen_mkdir == 1);
    assert(s_seen_rmdir == 1);
    assert(s_seen_nano == 1);
    assert(s_seen_ft8 == 1);
    assert(s_seen_usbmsc == 1);
    assert(s_seen_elfhello == 1); /* flash shadows the same SD app */
    assert(s_seen_sdonly == 1);

    char *hello_argv[] = {(char *)"hello", (char *)"probe"};
    int app_result = 0;
    s_external_runs = 0;
    s_last_external_root = NULL;
    assert(minishell_platform_app_run("hello", 2, hello_argv, &app_result) ==
           MINISHELL_PLATFORM_OK);
    assert(app_result == 23);
    assert(s_external_runs == 0); /* compiled-in always wins */

    char *free_argv[] = {(char *)"free"};
    assert(minishell_platform_app_run("free", 1, free_argv, &app_result) ==
           MINISHELL_PLATFORM_OK);
    assert(app_result == 29);

    char *ft8_argv[] = {(char *)"ft8"};
    assert(minishell_platform_app_run("ft8", 1, ft8_argv, &app_result) ==
           MINISHELL_PLATFORM_OK);
    assert(app_result == 31);

    char *usbmsc_argv[] = {(char *)"usbmsc"};
    assert(minishell_platform_app_run("usbmsc", 1, usbmsc_argv, &app_result) ==
           MINISHELL_PLATFORM_OK);
    assert(app_result == 37);

    char *elfhello_argv[] = {(char *)"elfhello"};
    assert(minishell_platform_app_run("elfhello", 1, elfhello_argv, &app_result) ==
           MINISHELL_PLATFORM_OK);
    assert(app_result == 41);
    assert(s_external_runs == 1);
    assert(strcmp(s_last_external_root, "/flash") == 0);

    s_flash_elfhello = false;
    assert(minishell_platform_app_run("elfhello", 1, elfhello_argv, &app_result) ==
           MINISHELL_PLATFORM_OK);
    assert(app_result == 42);
    assert(s_external_runs == 2);
    assert(strcmp(s_last_external_root, "/sd") == 0);

    char *sdonly_argv[] = {(char *)"sdonly"};
    assert(minishell_platform_app_run("sdonly", 1, sdonly_argv, &app_result) ==
           MINISHELL_PLATFORM_OK);
    assert(app_result == 42);
    assert(strcmp(s_last_external_root, "/sd") == 0);

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
