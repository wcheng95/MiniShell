#include "test_support.h"

#include "cp_copy.h"

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
    TEST_EQ(cp_copy_file(fs, "/sd/source.bin", "/sd/destination-dir"), CP_COPY_ERR_DEST_IS_DIR);

    minishell_services_app_end();
    return true;
}
