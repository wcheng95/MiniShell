#include "test_support.h"

/* Two distinct native directories, each with one entry, for public-API lifetime tests. */
static minishell_backend_dir_t s_test_dir;
static bool s_test_dir_read;
static uint32_t s_test_dir_closes;

static mini_result_t lifetime_dir_open(void *ctx, const char *path,
                                       minishell_backend_dir_t *out_dir)
{
    (void)ctx;
    if (s_test_dir != MINISHELL_BACKEND_DIR_INVALID) return MINI_ERR_TOO_MANY_OPEN;
    if (strcmp(path, "/sd/first") == 0) *out_dir = 1u;
    else if (strcmp(path, "/sd/second") == 0) *out_dir = 2u;
    else return MINI_ERR_NOT_FOUND;
    s_test_dir = *out_dir;
    s_test_dir_read = false;
    return MINI_OK;
}

static mini_result_t lifetime_dir_read(void *ctx, minishell_backend_dir_t dir,
                                       char *name, uint32_t capacity,
                                       uint32_t *type, uint32_t *has_entry)
{
    (void)ctx;
    if (dir == MINISHELL_BACKEND_DIR_INVALID || dir != s_test_dir)
        return MINI_ERR_BAD_HANDLE;
    *has_entry = s_test_dir_read ? 0u : 1u;
    if (*has_entry != 0u) {
        const char *entry = dir == 1u ? "first.txt" : "second.txt";
        if (strlen(entry) + 1u > capacity) return MINI_ERR_NAME_TOO_LONG;
        strcpy(name, entry);
        *type = MINI_FS_TYPE_FILE;
        s_test_dir_read = true;
    }
    return MINI_OK;
}

static mini_result_t lifetime_dir_close(void *ctx, minishell_backend_dir_t dir)
{
    (void)ctx;
    if (dir == MINISHELL_BACKEND_DIR_INVALID || dir != s_test_dir)
        return MINI_ERR_BAD_HANDLE;
    s_test_dir = MINISHELL_BACKEND_DIR_INVALID;
    ++s_test_dir_closes;
    return MINI_OK;
}

static bool stale_file_rejected(const mini_fs_api_t *fs, mini_file_t stale)
{
    char byte;
    uint32_t count;
    uint64_t position;
    uint32_t closes = g_fake.fs_close_calls;
    uint32_t syncs = g_fake.fs_sync_calls;
    TEST_EQ(fs->read(stale, &byte, 1u, &count), MINI_ERR_BAD_HANDLE);
    TEST_EQ(fs->write(stale, "!", 1u, &count), MINI_ERR_BAD_HANDLE);
    TEST_EQ(fs->seek(stale, 1, MINI_FS_SEEK_SET, &position), MINI_ERR_BAD_HANDLE);
    TEST_EQ(fs->sync(stale), MINI_ERR_BAD_HANDLE);
    TEST_EQ(fs->close(stale), MINI_ERR_BAD_HANDLE);
    TEST_EQ(g_fake.fs_close_calls, closes);
    TEST_EQ(g_fake.fs_sync_calls, syncs);
    return true;
}

static bool stale_dir_rejected(const mini_fs_api_t *fs, mini_dir_t stale)
{
    mini_fs_dir_entry_t entry = {.struct_size = sizeof(entry)};
    uint32_t has_entry;
    uint32_t closes = s_test_dir_closes;
    TEST_EQ(fs->dir_read(stale, &entry, &has_entry), MINI_ERR_BAD_HANDLE);
    TEST_EQ(fs->dir_close(stale), MINI_ERR_BAD_HANDLE);
    TEST_EQ(s_test_dir_closes, closes);
    return true;
}

static bool test_handle_reuse(void)
{
    fake_reset();
    fake_fs_add_file("/sd/first.txt", "A");
    fake_fs_add_file("/sd/second.txt", "B");
    s_test_dir = MINISHELL_BACKEND_DIR_INVALID;
    s_test_dir_closes = 0u;
    minishell_services_port_t port = fake_full_port();
    port.fs_dir_open = lifetime_dir_open;
    port.fs_dir_read = lifetime_dir_read;
    port.fs_dir_close = lifetime_dir_close;
    minishell_services_configure(&port);
    minishell_services_app_begin();
    const mini_fs_api_t *fs = mini_api_get()->fs;

    mini_file_t files[64];
    mini_dir_t dirs[64];
    for (uint32_t i = 0u; i < 64u; ++i) {
        const char expected = (i % 2u == 0u) ? 'A' : 'B';
        const char *path = (i % 2u == 0u) ? "/sd/first.txt" : "/sd/second.txt";
        TEST_EQ(fs->open(path, MINI_FS_READ | MINI_FS_WRITE, &files[i]), MINI_OK);
        TEST_CHECK(files[i] != MINI_FILE_INVALID);
        for (uint32_t j = 0u; j < i; ++j) {
            TEST_CHECK(files[i] != files[j]);
            TEST_CHECK(stale_file_rejected(fs, files[j]));
        }
        char byte;
        uint32_t count;
        uint64_t position;
        TEST_EQ(fs->read(files[i], &byte, 1u, &count), MINI_OK);
        TEST_EQ(count, 1u);
        TEST_EQ(byte, expected);
        TEST_EQ(fs->seek(files[i], 0, MINI_FS_SEEK_SET, &position), MINI_OK);
        TEST_EQ(position, 0u);
        TEST_EQ(fs->write(files[i], &expected, 1u, &count), MINI_OK);
        TEST_EQ(count, 1u);
        TEST_EQ(fs->sync(files[i]), MINI_OK);
        TEST_EQ(fs->close(files[i]), MINI_OK);
        TEST_CHECK(stale_file_rejected(fs, files[i]));

        path = (i % 2u == 0u) ? "/sd/first" : "/sd/second";
        TEST_EQ(fs->dir_open(path, &dirs[i]), MINI_OK);
        TEST_CHECK(dirs[i] != MINI_DIR_INVALID);
        for (uint32_t j = 0u; j < i; ++j) {
            TEST_CHECK(dirs[i] != dirs[j]);
            TEST_CHECK(stale_dir_rejected(fs, dirs[j]));
        }
        mini_fs_dir_entry_t entry = {.struct_size = sizeof(entry)};
        TEST_EQ(fs->dir_read(dirs[i], &entry, &count), MINI_OK);
        TEST_EQ(count, 1u);
        TEST_EQ(entry.type, MINI_FS_TYPE_FILE);
        TEST_CHECK(strcmp(entry.name, (i % 2u == 0u) ? "first.txt" : "second.txt") == 0);
        TEST_EQ(fs->dir_read(dirs[i], &entry, &count), MINI_OK);
        TEST_EQ(count, 0u);
        TEST_EQ(fs->dir_close(dirs[i]), MINI_OK);
        TEST_CHECK(stale_dir_rejected(fs, dirs[i]));
    }

    /* Reclaimed lifetimes must stay invalid after both app and port transitions. */
    for (uint32_t reconfigure = 0u; reconfigure < 2u; ++reconfigure) {
        mini_file_t old_file, new_file;
        mini_dir_t old_dir, new_dir;
        TEST_EQ(fs->open("/sd/first.txt", MINI_FS_READ | MINI_FS_WRITE, &old_file), MINI_OK);
        TEST_EQ(fs->dir_open("/sd/first", &old_dir), MINI_OK);
        uint32_t file_closes = g_fake.fs_close_calls;
        uint32_t dir_closes = s_test_dir_closes;
        if (reconfigure != 0u) minishell_services_configure(&port);
        else minishell_services_app_end();
        TEST_EQ(g_fake.fs_close_calls, file_closes + 1u);
        TEST_EQ(s_test_dir_closes, dir_closes + 1u);
        minishell_services_app_begin();
        TEST_EQ(fs->open("/sd/second.txt", MINI_FS_READ | MINI_FS_WRITE, &new_file), MINI_OK);
        TEST_EQ(fs->dir_open("/sd/second", &new_dir), MINI_OK);
        TEST_CHECK(new_file != old_file);
        TEST_CHECK(new_dir != old_dir);
        TEST_CHECK(stale_file_rejected(fs, old_file));
        TEST_CHECK(stale_dir_rejected(fs, old_dir));
        char byte;
        uint32_t count;
        mini_fs_dir_entry_t entry = {.struct_size = sizeof(entry)};
        TEST_EQ(fs->read(new_file, &byte, 1u, &count), MINI_OK);
        TEST_EQ(count, 1u);
        TEST_EQ(byte, 'B');
        TEST_EQ(fs->dir_read(new_dir, &entry, &count), MINI_OK);
        TEST_EQ(count, 1u);
        TEST_CHECK(strcmp(entry.name, "second.txt") == 0);
        TEST_EQ(fs->close(new_file), MINI_OK);
        TEST_EQ(fs->dir_close(new_dir), MINI_OK);
    }
    minishell_services_app_end();
    return true;
}

/* Minimal two-volume tree for quota scans and physical-space dispatch. */
static unsigned space_calls, space_positions[3];
static uint64_t space_file_size = 10;
static mini_result_t space_stat_error, space_backend_error;
static bool space_sd_available, space_bad_capacity;
static char space_last_path[512];
static mini_result_t space_stat(void *ctx, const char *path, uint32_t *type, uint64_t *size)
{
    (void)ctx;
    if (space_stat_error != MINI_OK) return space_stat_error;
    if (!space_sd_available && strncmp(path, "/sd", 3) == 0) return MINI_ERR_NOT_FOUND;
    *size = 0;
    *type = MINI_FS_TYPE_DIRECTORY;
    if (!strcmp(path,"/") || !strcmp(path,"/flash") || !strcmp(path,"/sd") ||
        !strcmp(path,"/flash/ft8") || !strcmp(path,"/sd/foo/bar")) return MINI_OK;
    *type = MINI_FS_TYPE_FILE;
    if (!strcmp(path,"/flash/data")) { *size=space_file_size; return MINI_OK; }
    if (!strcmp(path,"/sd/data")) { *size=20; return MINI_OK; }
    return MINI_ERR_NOT_FOUND;
}
static mini_result_t space_dir_open(void *ctx, const char *path, minishell_backend_dir_t *dir)
{
    (void)ctx;
    *dir = !strcmp(path,"/") ? 1 : !strcmp(path,"/flash") ? 2 : 3;
    space_positions[*dir-1]=0;
    return MINI_OK;
}
static mini_result_t space_dir_read(void *ctx, minishell_backend_dir_t dir, char *name,
                                    uint32_t capacity, uint32_t *type, uint32_t *has)
{
    (void)ctx; (void)capacity;
    unsigned i=space_positions[dir-1]++;
    *has = i < (dir == 1 ? 2u : 1u);
    if (*has) {
        strcpy(name, dir == 1 ? (i ? "sd" : "flash") : "data");
        *type = dir == 1 ? MINI_FS_TYPE_DIRECTORY : MINI_FS_TYPE_FILE;
    }
    return MINI_OK;
}
static mini_result_t space_dir_close(void *ctx, minishell_backend_dir_t dir)
{
    (void)ctx; (void)dir; return MINI_OK;
}
static mini_result_t space_backend(void *ctx, const char *path, uint64_t *total, uint64_t *free_bytes)
{
    (void)ctx; ++space_calls;
    strcpy(space_last_path,path);
    *total = !strncmp(path,"/flash",6) ? 1000 : 10000000000ULL;
    *free_bytes = space_bad_capacity ? *total+1 : *total-200;
    return space_backend_error;
}
static bool test_space_modes(void)
{
    fake_reset();
    minishell_services_port_t port=fake_full_port();
    port.fs_stat=space_stat; port.fs_space=space_backend;
    port.fs_dir_open=space_dir_open; port.fs_dir_read=space_dir_read; port.fs_dir_close=space_dir_close;
    space_calls=0; space_file_size=10; space_sd_available=true;
    space_stat_error=space_backend_error=MINI_OK;
    minishell_resource_limits_t limits={.storage_bytes=100};
    minishell_services_set_resource_limits(&limits);
    minishell_services_configure(&port); minishell_services_app_begin();
    const mini_fs_api_t *fs=mini_api_get()->fs;
    mini_fs_space_t result={.struct_size=sizeof(result)};
    TEST_EQ(fs->space("/flash/./ft8", &result), MINI_OK);
    TEST_EQ(result.total_bytes,100u); TEST_EQ(result.used_bytes,30u); TEST_EQ(result.free_bytes,70u);
    TEST_EQ(minishell_filesystem_cwd_set("/flash"), MINI_OK);
    TEST_EQ(fs->space("./ft8", &result), MINI_OK);
    TEST_EQ(result.used_bytes,30u); TEST_EQ(result.free_bytes,70u);
    TEST_EQ(fs->space("/sd/foo/bar", &result), MINI_OK);
    TEST_EQ(result.used_bytes,30u); TEST_EQ(space_calls,0u);
    space_file_size=120;
    TEST_EQ(fs->space("/flash", &result), MINI_OK);
    TEST_EQ(result.used_bytes,140u); TEST_EQ(result.free_bytes,0u); TEST_EQ(space_calls,0u);
    space_stat_error=MINI_ERR_IO;
    TEST_EQ(fs->space("/flash", &result), MINI_ERR_IO);
    space_stat_error=MINI_OK;

    limits.storage_bytes=0; minishell_services_set_resource_limits(&limits);
    const char *paths[]={"/flash", "/sd", "/flash//x/../ft8", "/sd/foo/./bar"};
    const char *normalized[]={"/flash", "/sd", "/flash/ft8", "/sd/foo/bar"};
    for (unsigned i=0;i<4;++i) {
        TEST_EQ(fs->space(paths[i], &result), MINI_OK);
        TEST_CHECK(!strcmp(space_last_path,normalized[i]));
        TEST_EQ(result.total_bytes,i%2 ? 10000000000ULL : 1000u);
        TEST_EQ(result.used_bytes,200u); TEST_EQ(result.free_bytes,result.total_bytes-200);
        TEST_EQ(result.reserved0,0u);
    }
    TEST_EQ(space_calls,4u);
    TEST_EQ(fs->space("../sd/foo/./bar", &result), MINI_OK);
    TEST_CHECK(!strcmp(space_last_path,"/sd/foo/bar"));
    TEST_EQ(result.used_bytes,200u);
    TEST_EQ(fs->space("relative",&result),MINI_ERR_NOT_FOUND);
    TEST_EQ(fs->space("/missing",&result),MINI_ERR_NOT_FOUND);
    space_sd_available=false;
    TEST_EQ(fs->space("/sd/foo/bar",&result),MINI_ERR_NOT_FOUND);
    TEST_EQ(space_calls,5u);
    space_stat_error=MINI_ERR_NOT_READY;
    TEST_EQ(fs->space("/flash",&result),MINI_ERR_NOT_READY);
    TEST_EQ(space_calls,5u); space_stat_error=MINI_OK;
    const mini_result_t errors[]={MINI_ERR_IO,MINI_ERR_NOT_READY,MINI_ERR_NOT_FOUND,MINI_ERR_UNSUPPORTED};
    for (unsigned i=0;i<4;++i) {
        space_backend_error=errors[i];
        TEST_EQ(fs->space("/flash",&result),errors[i]);
    }
    space_backend_error=MINI_OK; space_bad_capacity=true;
    TEST_EQ(fs->space("/flash",&result),MINI_ERR_IO);
    space_bad_capacity=false;
    port.fs_space=NULL;
    minishell_services_configure(&port); minishell_services_app_begin();
    TEST_EQ(fs->space("/flash",&result),MINI_ERR_UNSUPPORTED);
    TEST_EQ(fs->space("/missing",&result),MINI_ERR_NOT_FOUND);
    space_sd_available=true; limits.storage_bytes=100;
    minishell_services_set_resource_limits(&limits);
    TEST_EQ(fs->space("/flash",&result),MINI_OK);
    TEST_EQ(result.total_bytes,100u); TEST_EQ(result.used_bytes,140u); TEST_EQ(result.free_bytes,0u);
    result.struct_size=sizeof(uint32_t);
    TEST_EQ(fs->space("/flash",&result),MINI_ERR_INVALID);
    minishell_services_app_end(); minishell_services_set_resource_limits(NULL);
    return true;
}

static bool cwd_is(const char *expected)
{
    char cwd[MINISHELL_FILESYSTEM_PATH_CAP];
    TEST_EQ(minishell_filesystem_cwd_get(cwd, sizeof(cwd)), MINI_OK);
    TEST_CHECK(!strcmp(cwd, expected));
    return true;
}

static char cwd_boundary_path[MINISHELL_FILESYSTEM_PATH_CAP];
static mini_result_t cwd_boundary_stat(void *ctx, const char *path, uint32_t *type, uint64_t *size)
{
    (void)ctx;
    if (path[0] != '/' || strlen(path) >= sizeof(cwd_boundary_path)) return MINI_ERR_INVALID;
    strcpy(cwd_boundary_path, path);
    *type = MINI_FS_TYPE_DIRECTORY; *size = 0;
    return MINI_OK;
}

static bool test_cwd(void)
{
    fake_reset();
    fake_fs_add_dir("/sd/first");
    fake_fs_add_dir("/sd/second");
    fake_fs_add_file("/sd/first/read.txt", "data");
    minishell_services_port_t port = fake_full_port();
    port.fs_dir_open = lifetime_dir_open;
    port.fs_dir_read = lifetime_dir_read;
    port.fs_dir_close = lifetime_dir_close;
    s_test_dir = MINISHELL_BACKEND_DIR_INVALID;
    minishell_services_configure(&port);
    const mini_fs_api_t *fs = mini_api_get()->fs;
    TEST_CHECK(cwd_is("/"));
    char tiny[2] = "!";
    TEST_EQ(minishell_filesystem_cwd_get(NULL, 0), MINI_ERR_INVALID);
    TEST_EQ(minishell_filesystem_cwd_get(tiny, 0), MINI_ERR_INVALID);
    TEST_EQ(minishell_filesystem_cwd_get(tiny, 1), MINI_ERR_NAME_TOO_LONG);
    TEST_CHECK(!strcmp(tiny, "!"));
    TEST_EQ(minishell_filesystem_cwd_get(tiny, 2), MINI_OK);
    TEST_EQ(minishell_filesystem_cwd_set("/sd//./first"), MINI_OK);
    TEST_CHECK(cwd_is("/sd/first"));
    TEST_EQ(minishell_filesystem_cwd_set("."), MINI_OK);
    TEST_EQ(minishell_filesystem_cwd_set(".."), MINI_OK);
    TEST_CHECK(cwd_is("/sd"));
    TEST_EQ(minishell_filesystem_cwd_set("first"), MINI_OK);
    TEST_EQ(minishell_filesystem_cwd_set("missing"), MINI_ERR_NOT_FOUND);
    TEST_EQ(minishell_filesystem_cwd_set("read.txt"), MINI_ERR_NOT_DIR);
    TEST_EQ(minishell_filesystem_cwd_set("../../.."), MINI_ERR_INVALID);
    TEST_EQ(minishell_filesystem_cwd_set(""), MINI_ERR_INVALID);
    TEST_EQ(minishell_filesystem_cwd_set(NULL), MINI_ERR_INVALID);
    TEST_CHECK(cwd_is("/sd/first"));
    minishell_services_app_begin();
    TEST_CHECK(cwd_is("/sd/first"));
    mini_file_t file, writer;
    TEST_EQ(fs->open("./read.txt", MINI_FS_READ, &file), MINI_OK);
    TEST_CHECK(!strcmp(g_fake.fs_last_path, "/sd/first/read.txt"));
    TEST_EQ(fs->open("/sd/first/read.txt", MINI_FS_WRITE, &writer), MINI_ERR_ACCESS);
    TEST_EQ(fs->close(file), MINI_OK);
    TEST_EQ(fs->open("/sd/first/read.txt", MINI_FS_READ, &file), MINI_OK);
    TEST_EQ(fs->open("read.txt", MINI_FS_WRITE, &writer), MINI_ERR_ACCESS);
    TEST_EQ(fs->close(file), MINI_OK);
    TEST_EQ(fs->open("read.txt", MINI_FS_WRITE, &writer), MINI_OK);
    TEST_EQ(fs->rename("./read.txt", "../second/moved.txt"), MINI_ERR_ACCESS);
    TEST_EQ(fs->close(writer), MINI_OK);
    mini_fs_stat_t info = {.struct_size=sizeof(info)};
    TEST_EQ(fs->stat("../first/read.txt", &info), MINI_OK);
    TEST_CHECK(!strcmp(g_fake.fs_last_path, "/sd/first/read.txt"));
    TEST_EQ(fs->rename("read.txt", "../second/moved.txt"), MINI_OK);
    TEST_EQ(fs->stat("/sd/second/moved.txt", &info), MINI_OK);
    TEST_EQ(fs->remove_file("../second/moved.txt"), MINI_OK);
    TEST_EQ(fs->stat("/sd/second/moved.txt", &info), MINI_ERR_NOT_FOUND);
    TEST_EQ(fs->mkdir("tmp"), MINI_OK);
    TEST_EQ(fs->stat("/sd/first/tmp", &info), MINI_OK);
    TEST_EQ(fs->rmdir("./tmp"), MINI_OK);
    mini_dir_t dir;
    TEST_EQ(fs->dir_open(".", &dir), MINI_OK);
    TEST_EQ(fs->dir_close(dir), MINI_OK);
    TEST_EQ(fs->remove_file("../.."), MINI_ERR_IS_DIR);
    TEST_EQ(fs->mkdir("../.."), MINI_ERR_EXISTS);
    TEST_EQ(fs->rmdir("../.."), MINI_ERR_ACCESS);
    TEST_EQ(fs->rename("../..", "anything"), MINI_ERR_ACCESS);
    TEST_EQ(fs->rename("anything", "../.."), MINI_ERR_ACCESS);
    TEST_EQ(fs->stat("../../..", &info), MINI_ERR_INVALID);
    TEST_EQ(fs->open("", MINI_FS_READ, &file), MINI_ERR_INVALID);
    TEST_EQ(fs->dir_open(".", &dir), MINI_OK);
    minishell_services_app_end();
    minishell_services_app_begin();
    TEST_CHECK(cwd_is("/sd/first"));
    TEST_EQ(fs->dir_close(dir), MINI_ERR_BAD_HANDLE);
    minishell_services_configure(&port);
    TEST_CHECK(cwd_is("/"));

    /* Exercise the service's real 511-byte path bound without the small fake
     * filesystem's 128-byte node-name storage truncating the test fixture. */
    port.fs_stat = cwd_boundary_stat;
    minishell_services_configure(&port);
    char path[MINISHELL_FILESYSTEM_PATH_CAP + 1];
    memset(path, 'a', sizeof(path)); path[0] = '/'; path[511] = 0;
    TEST_EQ(minishell_filesystem_cwd_set(path), MINI_OK);
    TEST_CHECK(cwd_is(path));
    TEST_EQ(fs->stat(".", &info), MINI_OK);
    TEST_CHECK(!strcmp(cwd_boundary_path, path));
    TEST_EQ(fs->stat("x", &info), MINI_ERR_NAME_TOO_LONG);
    TEST_EQ(minishell_filesystem_cwd_set("x"), MINI_ERR_NAME_TOO_LONG);
    TEST_CHECK(cwd_is(path));
    TEST_EQ(minishell_filesystem_cwd_set("../sd"), MINI_OK);
    TEST_CHECK(cwd_is("/sd"));
    memset(path, 'b', sizeof(path)); path[507] = 0;
    TEST_EQ(fs->stat(path, &info), MINI_OK); /* /sd/ + 507 = 511 */
    TEST_EQ(strlen(cwd_boundary_path), 511u);
    path[507] = 'b'; path[508] = 0;
    TEST_EQ(fs->stat(path, &info), MINI_ERR_NAME_TOO_LONG);
    path[0] = '/'; path[511] = 'b'; path[512] = 0;
    /* Fill all bytes again so an earlier terminator cannot shorten the path. */
    memset(path+1, 'b', 511); path[512] = 0;
    TEST_EQ(fs->stat(path, &info), MINI_ERR_NAME_TOO_LONG);
    memset(path, '/', sizeof(path)); path[512] = 0;
    TEST_EQ(fs->stat(path, &info), MINI_OK); /* Raw length is not normalized length. */
    TEST_CHECK(!strcmp(cwd_boundary_path, "/"));
    minishell_services_configure(NULL);
    TEST_CHECK(cwd_is("/"));
    TEST_EQ(minishell_filesystem_cwd_set("/sd"), MINI_ERR_UNSUPPORTED);
    return true;
}

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
    TEST_EQ(fs->open("relative", MINI_FS_READ, &f), MINI_ERR_NOT_FOUND);
    TEST_EQ(fs->open("/sd/read.txt", 0, &f), MINI_ERR_INVALID);
    TEST_EQ(fs->open("/sd/read.txt", MINI_FS_CREATE | MINI_FS_READ, &f), MINI_ERR_INVALID);
    TEST_EQ(fs->open("/sd/read.txt", MINI_FS_EXCL | MINI_FS_WRITE, &f), MINI_ERR_INVALID);

    TEST_EQ(fs->open("/sd//./read.txt", MINI_FS_READ, &f), MINI_OK);
    TEST_CHECK(strcmp(g_fake.fs_last_path, "/sd/read.txt") == 0);
    TEST_CHECK(f != MINI_FILE_INVALID);

    /* Readers may coexist, but a writer cannot acquire the same normalized
     * logical path while any reader already owns it. */
    mini_file_t reader2 = MINI_FILE_INVALID;
    TEST_EQ(fs->open("/sd/read.txt", MINI_FS_READ, &reader2), MINI_OK);
    uint32_t opens_before_alias = g_fake.fs_open_calls;
    mini_file_t alias_writer = MINI_FILE_INVALID;
    TEST_EQ(fs->open("/sd/./read.txt", MINI_FS_WRITE | MINI_FS_TRUNC,
                     &alias_writer), MINI_ERR_ACCESS);
    TEST_EQ(alias_writer, MINI_FILE_INVALID);
    TEST_EQ(g_fake.fs_open_calls, opens_before_alias);
    TEST_EQ(fs->close(reader2), MINI_OK);

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

    /* Once the final reader releases the path, a writer may acquire an alias. */
    alias_writer = MINI_FILE_INVALID;
    TEST_EQ(fs->open("/sd/./read.txt", MINI_FS_WRITE, &alias_writer), MINI_OK);
    TEST_EQ(fs->close(alias_writer), MINI_OK);

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

    fake_fs_add_dir("/sd/existing-dir");
    TEST_EQ(fs->rename("/sd/moved.txt", "/sd/existing-dir"), MINI_ERR_IS_DIR);

    mini_file_t held = MINI_FILE_INVALID;
    TEST_EQ(fs->open("/sd/moved.txt", MINI_FS_WRITE, &held), MINI_OK);
    TEST_EQ(fs->rename("/sd/moved.txt", "/sd/held-move.txt"), MINI_ERR_ACCESS);
    TEST_EQ(fs->close(held), MINI_OK);

    fake_fs_add_file("/sd/existing.txt", "keep");
    TEST_EQ(fs->rename("/sd/moved.txt", "/sd/existing.txt"), MINI_OK);
    st.struct_size = sizeof(st);
    TEST_EQ(fs->stat("/sd/moved.txt", &st), MINI_ERR_NOT_FOUND);
    st.struct_size = sizeof(st);
    TEST_EQ(fs->stat("/sd/existing.txt", &st), MINI_OK);
    TEST_EQ(st.type, MINI_FS_TYPE_FILE);
    TEST_EQ(st.size, 7u);

    r = MINI_FILE_INVALID;
    TEST_EQ(fs->open("/sd/existing.txt", MINI_FS_READ, &r), MINI_OK);
    memset(buf, 0, sizeof(buf));
    TEST_EQ(fs->read(r, buf, sizeof(buf), &n), MINI_OK);
    TEST_EQ(n, 7u);
    TEST_CHECK(memcmp(buf, "move-me", 7) == 0);
    TEST_EQ(fs->close(r), MINI_OK);

    TEST_EQ(fs->rename("/", "/sd/root"), MINI_ERR_ACCESS);
    TEST_EQ(fs->rename("/sd/existing.txt", "/"), MINI_ERR_ACCESS);

    TEST_EQ(fs->remove_file("/sd/existing.txt"), MINI_OK);
    st.struct_size = sizeof(st);
    TEST_EQ(fs->stat("/sd/existing.txt", &st), MINI_ERR_NOT_FOUND);
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

    TEST_CHECK(test_handle_reuse());
    TEST_CHECK(test_space_modes());
    TEST_CHECK(test_cwd());
    return true;
}
