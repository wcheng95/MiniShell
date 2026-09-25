#include "test_support.h"

/* Exercise the real portable command with the service and fake backend. */
#define main test_mv_main
#include "../../apps/mv/main/mv.c"
#undef main

static uint32_t rejected_renames;

static mini_result_t cross_volume_rename(void *ctx, const char *source, const char *dest)
{
    (void)ctx;
    if (strcmp(source, "/sd/source") != 0 || strcmp(dest, "/flash/source") != 0)
        return MINI_ERR_INVALID;
    ++rejected_renames;
    return MINI_ERR_UNSUPPORTED;
}

static int move_file(char *source, char *dest)
{
    char *argv[] = {"mv", source, dest};
    return test_mv_main(3, argv);
}

bool test_mv(void)
{
    fake_reset();
    fake_fs_add_dir("/flash");
    fake_fs_add_dir("/sd/archive");
    fake_fs_add_file("/sd/source", "new");
    fake_fs_add_file("/sd/archive/source", "old");
    minishell_services_port_t port = fake_full_port();
    port.console_write = port.system_write;
    minishell_services_configure(&port);
    minishell_services_app_begin();
    const mini_fs_api_t *fs = mini_api_get()->fs;
    TEST_EQ(minishell_filesystem_cwd_set("/sd"), MINI_OK);
    TEST_EQ(move_file("source/.", "archive/."), 0);
    mini_fs_stat_t st = {.struct_size = sizeof(st)};
    TEST_EQ(fs->stat("source", &st), MINI_ERR_NOT_FOUND);
    mini_file_t file;
    char data[4] = {0};
    uint32_t count;
    TEST_EQ(fs->open("archive/source", MINI_FS_READ, &file), MINI_OK);
    TEST_EQ(fs->read(file, data, 3u, &count), MINI_OK);
    TEST_EQ(count, 3u);
    TEST_CHECK(strcmp(data, "new") == 0);
    TEST_EQ(fs->close(file), MINI_OK);
    TEST_EQ(move_file("archive/source", "."), 0);
    TEST_EQ(move_file("source", "."), 0); /* Same path retains rename semantics. */
    TEST_EQ(move_file("archive", "."), 4);
    TEST_EQ(fs->mkdir("archive/source"), MINI_OK);
    TEST_CHECK(move_file("source", "archive") != 0);
    TEST_EQ(fs->stat("source", &st), MINI_OK);
    minishell_services_app_end();

    /* Force the backend's ADV-style cross-volume failure and ensure the command
     * neither opens/copies content nor removes either original file. */
    fake_fs_add_file("/flash/source", "old");
    port.fs_rename = cross_volume_rename;
    minishell_services_configure(&port);
    minishell_services_app_begin();
    rejected_renames = 0u;
    uint32_t opens = g_fake.fs_open_calls;
    TEST_EQ(move_file("/sd/source", "/flash"), 9);
    TEST_EQ(rejected_renames, 1u);
    TEST_EQ(g_fake.fs_open_calls, opens);
    TEST_EQ(fs->stat("/sd/source", &st), MINI_OK);
    TEST_EQ(fs->open("/flash/source", MINI_FS_READ, &file), MINI_OK);
    TEST_EQ(fs->read(file, data, 3u, &count), MINI_OK);
    TEST_CHECK(count == 3u && strcmp(data, "old") == 0);
    TEST_EQ(fs->close(file), MINI_OK);
    minishell_services_app_end();
    return true;
}
