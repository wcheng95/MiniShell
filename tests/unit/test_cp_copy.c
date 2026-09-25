#include "test_support.h"

#include "cp_copy.h"
#include "../../apps/common/file_destination.h"

static mini_result_t destination_stat_result;
static uint32_t destination_type;

static mini_result_t destination_stat(const char *path, mini_fs_stat_t *st)
{
    (void)path;
    st->type = destination_type;
    return destination_stat_result;
}

static bool test_destination_helper(void)
{
    mini_fs_api_t fs = {.stat = destination_stat};
    char joined[FILE_DESTINATION_CAP];
    const char *effective = NULL;
    destination_stat_result = MINI_OK;
    destination_type = MINI_FS_TYPE_DIRECTORY;
    const char *sources[] = {"/flash/a.txt", "a.txt", "./a.txt", "/flash//a.txt",
        "dir/../a.txt", "a.txt/", "a.txt/.", "a.txt//./", "a.txt/child/.."};
    const char *directories[] = {"/dir", "/dir/", "/dir/.", ".", "../dir", "/"};
    const char *expected[] = {"/dir/a.txt", "/dir/a.txt", "/dir/./a.txt",
        "./a.txt", "../dir/a.txt", "/a.txt"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        for (size_t j = 0; j < sizeof(directories) / sizeof(directories[0]); ++j) {
            TEST_EQ(file_destination_resolve(&fs, sources[i], directories[j], joined,
                                             &effective), MINI_OK);
            TEST_CHECK(strcmp(effective, expected[j]) == 0);
        }
    }
    const char *unusable[] = {"", "/", ".", "..", "a/..", "./../"};
    for (size_t i = 0; i < sizeof(unusable) / sizeof(unusable[0]); ++i) {
        TEST_EQ(file_destination_resolve(&fs, unusable[i], "/dir", joined,
                                         &effective), MINI_ERR_INVALID);
    }
    char directory[FILE_DESTINATION_CAP + 1u];
    memset(directory, 'd', sizeof(directory));
    directory[FILE_DESTINATION_CAP - 3u] = '\0';
    TEST_EQ(file_destination_resolve(&fs, "a", directory, joined, &effective), MINI_OK);
    TEST_EQ(strlen(effective), FILE_DESTINATION_CAP - 1u);
    directory[FILE_DESTINATION_CAP - 3u] = 'd';
    directory[FILE_DESTINATION_CAP - 2u] = '\0';
    TEST_EQ(file_destination_resolve(&fs, "a", directory, joined, &effective),
            MINI_ERR_NAME_TOO_LONG);
    directory[FILE_DESTINATION_CAP - 2u] = 'd';
    directory[FILE_DESTINATION_CAP] = '\0';
    TEST_EQ(file_destination_resolve(&fs, "a", directory, joined, &effective),
            MINI_ERR_NAME_TOO_LONG);
    char filename[FILE_DESTINATION_CAP];
    memset(filename, 'a', sizeof(filename) - 1u);
    filename[sizeof(filename) - 1u] = '\0';
    TEST_EQ(file_destination_resolve(&fs, filename, ".", joined, &effective),
            MINI_ERR_NAME_TOO_LONG);

    const char *literal = "../literal";
    destination_type = MINI_FS_TYPE_FILE;
    TEST_EQ(file_destination_resolve(&fs, "a", literal, joined, &effective), MINI_OK);
    TEST_CHECK(effective == literal);
    destination_stat_result = MINI_ERR_NOT_FOUND;
    TEST_EQ(file_destination_resolve(&fs, "a", literal, joined, &effective), MINI_OK);
    TEST_CHECK(effective == literal);
    destination_stat_result = MINI_ERR_ACCESS;
    TEST_EQ(file_destination_resolve(&fs, "a", literal, joined, &effective), MINI_ERR_ACCESS);
    return true;
}

static fake_fs_node_t *find_fake_node(const char *path)
{
    for (uint32_t i = 0u; i < 16u; ++i) {
        if (g_fake.fs_nodes[i].exists &&
            strcmp(g_fake.fs_nodes[i].path, path) == 0) {
            return &g_fake.fs_nodes[i];
        }
    }
    return NULL;
}

bool test_cp_copy(void)
{
    TEST_CHECK(test_destination_helper());
    fake_reset();
    fake_fs_add_file("/sd/source.bin", "seed");

    fake_fs_node_t *source = find_fake_node("/sd/source.bin");
    TEST_CHECK(source != NULL);
    source->size = 1500u;
    for (uint32_t i = 0u; i < source->size; ++i) {
        source->data[i] = (uint8_t)((i * 37u) & 0xffu);
    }
    source->data[0] = 0u;
    source->data[777] = 0u;
    source->data[1499] = 0xffu;

    minishell_services_port_t port = fake_full_port();
    minishell_services_configure(&port);
    minishell_services_app_begin();
    const mini_fs_api_t *fs = mini_api_get()->fs;
    TEST_CHECK(fs != NULL);

    g_fake.fs_max_read = 173u;
    g_fake.fs_max_write = 61u;
    TEST_EQ(cp_copy_file(fs, "/sd/source.bin", "/sd/copy.bin"), CP_COPY_OK);

    fake_fs_node_t *copy = find_fake_node("/sd/copy.bin");
    TEST_CHECK(copy != NULL);
    TEST_EQ(copy->size, source->size);
    TEST_CHECK(memcmp(copy->data, source->data, source->size) == 0);
    TEST_EQ(g_fake.fs_sync_calls, 1u);
    TEST_EQ(g_fake.fs_close_calls, 2u);

    source->size = 3u;
    source->data[0] = 'n';
    source->data[1] = 'e';
    source->data[2] = 'w';
    g_fake.fs_max_read = 0u;
    g_fake.fs_max_write = 0u;
    TEST_EQ(cp_copy_file(fs, "/sd/source.bin", "/sd/copy.bin"), CP_COPY_OK);
    TEST_EQ(copy->size, 3u);
    TEST_CHECK(memcmp(copy->data, "new", 3u) == 0);
    TEST_EQ(g_fake.fs_sync_calls, 2u);

    fake_fs_add_file("/sd/empty.bin", "");
    TEST_EQ(cp_copy_file(fs, "/sd/empty.bin", "/sd/empty-copy.bin"), CP_COPY_OK);
    fake_fs_node_t *empty_copy = find_fake_node("/sd/empty-copy.bin");
    TEST_CHECK(empty_copy != NULL);
    TEST_EQ(empty_copy->size, 0u);
    TEST_EQ(g_fake.fs_sync_calls, 3u);

    uint32_t opens_before = g_fake.fs_open_calls;
    TEST_EQ(cp_copy_file(fs, "/sd/source.bin", "/sd/source.bin"), CP_COPY_ERR_SAME_PATH);
    TEST_EQ(g_fake.fs_open_calls, opens_before);

    TEST_EQ(cp_copy_file(fs, "/sd/missing.bin", "/sd/x.bin"), CP_COPY_ERR_SOURCE_STAT);

    fake_fs_add_dir("/sd/source-dir");
    TEST_EQ(cp_copy_file(fs, "/sd/source-dir", "/sd/x.bin"), CP_COPY_ERR_SOURCE_IS_DIR);

    fake_fs_add_dir("/sd/destination-dir");
    TEST_EQ(cp_copy_file(fs, "/sd/source.bin", "/sd/destination-dir"), CP_COPY_OK);
    copy = find_fake_node("/sd/destination-dir/source.bin");
    TEST_CHECK(copy != NULL && copy->size == 3u && memcmp(copy->data, "new", 3u) == 0);
    source->data[0] = 'N';
    TEST_EQ(cp_copy_file(fs, "/sd/source.bin/.", "/sd/destination-dir/."), CP_COPY_OK);
    TEST_CHECK(copy->size == 3u && memcmp(copy->data, "New", 3u) == 0);

    fake_fs_add_dir("/sd/blocked");
    fake_fs_add_dir("/sd/blocked/source.bin");
    TEST_EQ(cp_copy_file(fs, "/sd/source.bin", "/sd/blocked"), CP_COPY_ERR_DEST_IS_DIR);
    TEST_EQ(minishell_filesystem_cwd_set("/sd"), MINI_OK);
    TEST_EQ(cp_copy_file(fs, "source.bin", "."), CP_COPY_ERR_OPEN_DEST);
    TEST_EQ(cp_copy_file(fs, "/sd/source.bin", "/sd"), CP_COPY_ERR_SAME_PATH);
    TEST_CHECK(source->size == 3u && memcmp(source->data, "New", 3u) == 0);
    TEST_EQ(cp_copy_file(fs, "source-dir", "destination-dir"), CP_COPY_ERR_SOURCE_IS_DIR);

    minishell_services_app_end();
    return true;
}
