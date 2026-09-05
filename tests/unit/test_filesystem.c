#include "test_support.h"

bool test_filesystem(void)
{
    fake_reset();
    fake_fs_add_file("/sd/read.txt", "abcdef");
    minishell_services_port_t p = fake_full_port();
    minishell_services_configure(&p);
    minishell_services_app_begin();

    const mini_fs_api_t *fs = mini_api_get()->fs;
    TEST_CHECK(fs != NULL);

    mini_file_t f = MINI_FILE_INVALID;
    TEST_EQ(fs->open("relative", MINI_FS_READ, &f), MINI_ERR_INVALID);
    TEST_EQ(fs->open("/sd/read.txt", 0, &f), MINI_ERR_INVALID);
    TEST_EQ(fs->open("/sd/read.txt", MINI_FS_CREATE | MINI_FS_READ, &f), MINI_ERR_INVALID);
    TEST_EQ(fs->open("/sd/read.txt", MINI_FS_EXCL | MINI_FS_WRITE, &f), MINI_ERR_INVALID);

    TEST_EQ(fs->open("/sd//./read.txt", MINI_FS_READ, &f), MINI_OK);
    TEST_CHECK(strcmp(g_fake.fs_last_path, "/sd/read.txt") == 0);
    TEST_CHECK(f != MINI_FILE_INVALID);

    g_fake.fs_max_read = 2u;
    char buf[16] = {0};
    uint32_t n = 99u;
    TEST_EQ(fs->read(f, buf, 6, &n), MINI_OK);
    TEST_EQ(n, 2u);
    TEST_CHECK(memcmp(buf, "ab", 2) == 0);

    uint64_t pos = 0;
    TEST_EQ(fs->seek(f, 1, MINI_FS_SEEK_CUR, &pos), MINI_OK);
    TEST_EQ(pos, 3u);
    g_fake.fs_fail_seek = true;
    TEST_EQ(fs->seek(f, 2, MINI_FS_SEEK_SET, &pos), MINI_ERR_IO);
    g_fake.fs_fail_seek = false;
    g_fake.fs_max_read = 0u;
    memset(buf, 0, sizeof(buf));
    TEST_EQ(fs->read(f, buf, 3, &n), MINI_OK);
    TEST_CHECK(memcmp(buf, "def", 3) == 0);
    TEST_EQ(fs->read(f, buf, 3, &n), MINI_OK);
    TEST_EQ(n, 0u);
    TEST_EQ(fs->read(f, NULL, 0, &n), MINI_OK);
    TEST_EQ(fs->close(f), MINI_OK);
    TEST_EQ(fs->close(f), MINI_ERR_BAD_HANDLE);

    mini_file_t w = MINI_FILE_INVALID;
    g_fake.fs_max_write = 3u;
    TEST_EQ(fs->open("/sd/new.txt", MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC, &w), MINI_OK);
    uint32_t nw = 0;
    TEST_EQ(fs->write(w, "abcdef", 6, &nw), MINI_OK);
    TEST_EQ(nw, 3u);
    TEST_EQ(fs->write(w, NULL, 0, &nw), MINI_OK);
    TEST_EQ(fs->sync(w), MINI_OK);
    TEST_EQ(g_fake.fs_sync_calls, 1u);
    TEST_EQ(fs->close(w), MINI_OK);
    g_fake.fs_max_write = 0u;

    mini_file_t a = MINI_FILE_INVALID;
    TEST_EQ(fs->open("/sd/new.txt", MINI_FS_WRITE | MINI_FS_APPEND, &a), MINI_OK);
    TEST_EQ(fs->seek(a, 0, MINI_FS_SEEK_SET, &pos), MINI_OK);
    TEST_EQ(fs->write(a, "Z", 1, &nw), MINI_OK);
    TEST_EQ(fs->close(a), MINI_OK);

    mini_file_t r = MINI_FILE_INVALID;
    TEST_EQ(fs->open("/sd/new.txt", MINI_FS_READ, &r), MINI_OK);
    memset(buf, 0, sizeof(buf));
    TEST_EQ(fs->read(r, buf, sizeof(buf), &n), MINI_OK);
    TEST_EQ(n, 4u);
    TEST_CHECK(memcmp(buf, "abcZ", 4) == 0);
    TEST_EQ(fs->close(r), MINI_OK);

    mini_fs_stat_t st = {.struct_size = sizeof(st)};
    TEST_EQ(fs->stat("/sd/new.txt", &st), MINI_OK);
    TEST_EQ(st.type, MINI_FS_TYPE_FILE);
    TEST_EQ(st.size, 4u);
    st.struct_size = sizeof(st);
    TEST_EQ(fs->stat("/sd", &st), MINI_OK);
    TEST_EQ(st.type, MINI_FS_TYPE_DIRECTORY);
    TEST_EQ(fs->stat("/../../bad", &st), MINI_ERR_INVALID);

    TEST_CHECK(fs->rename != NULL);
    TEST_CHECK(fs->remove_file != NULL);
    TEST_CHECK(fs->mkdir != NULL);
    TEST_CHECK(fs->rmdir != NULL);

    fake_fs_add_file("/sd/move.txt", "move-me");
    TEST_EQ(fs->rename("/sd/./move.txt", "/sd/moved.txt"), MINI_OK);
    st.struct_size = sizeof(st);
    TEST_EQ(fs->stat("/sd/move.txt", &st), MINI_ERR_NOT_FOUND);
    st.struct_size = sizeof(st);
    TEST_EQ(fs->stat("/sd/moved.txt", &st), MINI_OK);
    TEST_EQ(st.type, MINI_FS_TYPE_FILE);
    TEST_EQ(st.size, 7u);
    TEST_EQ(fs->rename("/sd/moved.txt", "/sd/./moved.txt"), MINI_OK);

    fake_fs_add_file("/sd/existing.txt", "keep");
    TEST_EQ(fs->rename("/sd/moved.txt", "/sd/existing.txt"), MINI_ERR_EXISTS);
    st.struct_size = sizeof(st);
    TEST_EQ(fs->stat("/sd/moved.txt", &st), MINI_OK);
    TEST_EQ(fs->rename("/", "/sd/root"), MINI_ERR_ACCESS);
    TEST_EQ(fs->rename("/sd/moved.txt", "/"), MINI_ERR_ACCESS);

    TEST_EQ(fs->remove_file("/sd/moved.txt"), MINI_OK);
    st.struct_size = sizeof(st);
    TEST_EQ(fs->stat("/sd/moved.txt", &st), MINI_ERR_NOT_FOUND);
    TEST_EQ(fs->remove_file("/sd"), MINI_ERR_IS_DIR);
    TEST_EQ(fs->remove_file("/"), MINI_ERR_IS_DIR);

    TEST_EQ(fs->mkdir("/sd/newdir"), MINI_OK);
    st.struct_size = sizeof(st);
    TEST_EQ(fs->stat("/sd/newdir", &st), MINI_OK);
    TEST_EQ(st.type, MINI_FS_TYPE_DIRECTORY);
    TEST_EQ(fs->mkdir("/sd/newdir"), MINI_ERR_EXISTS);
    TEST_EQ(fs->mkdir("/missing/child"), MINI_ERR_NOT_FOUND);

    fake_fs_add_file("/sd/newdir/child.txt", "x");
    TEST_EQ(fs->rmdir("/sd/newdir"), MINI_ERR_NOT_EMPTY);
    TEST_EQ(fs->remove_file("/sd/newdir/child.txt"), MINI_OK);
    TEST_EQ(fs->rmdir("/sd/newdir"), MINI_OK);
    st.struct_size = sizeof(st);
    TEST_EQ(fs->stat("/sd/newdir", &st), MINI_ERR_NOT_FOUND);
    TEST_EQ(fs->rmdir("/sd/read.txt"), MINI_ERR_NOT_DIR);
    TEST_EQ(fs->rmdir("/"), MINI_ERR_ACCESS);

    mini_file_t handles[32];
    for (uint32_t i = 0; i < 32u; ++i) {
        TEST_EQ(fs->open("/sd/read.txt", MINI_FS_READ, &handles[i]), MINI_OK);
    }
    mini_file_t extra = MINI_FILE_INVALID;
    TEST_EQ(fs->open("/sd/read.txt", MINI_FS_READ, &extra), MINI_ERR_TOO_MANY_OPEN);
    uint32_t closes_before = g_fake.fs_close_calls;
    mini_file_t stale = handles[0];
    minishell_services_app_end();
    TEST_EQ(g_fake.fs_close_calls, closes_before + 32u);
    minishell_services_app_begin();
    TEST_EQ(fs->read(stale, buf, 1, &n), MINI_ERR_BAD_HANDLE);

    mini_fs_stat_t small = {.struct_size = sizeof(uint32_t)};
    TEST_EQ(fs->stat("/sd", &small), MINI_ERR_INVALID);

    minishell_services_port_t old_port = fake_full_port();
    old_port.fs_rename = NULL;
    old_port.fs_remove_file = NULL;
    old_port.fs_mkdir = NULL;
    old_port.fs_rmdir = NULL;
    minishell_services_configure(&old_port);
    minishell_services_app_begin();
    fs = mini_api_get()->fs;
    TEST_CHECK(fs != NULL);
    TEST_EQ(fs->rename("/sd/read.txt", "/sd/x.txt"), MINI_ERR_UNSUPPORTED);
    TEST_EQ(fs->remove_file("/sd/read.txt"), MINI_ERR_UNSUPPORTED);
    TEST_EQ(fs->mkdir("/sd/x"), MINI_ERR_UNSUPPORTED);
    TEST_EQ(fs->rmdir("/sd"), MINI_ERR_UNSUPPORTED);

    return true;
}
