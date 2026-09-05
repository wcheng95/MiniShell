#include "test_support.h"

#include <stddef.h>

#include "minishell_transfer.h"

#define TRANSFER_BUFFER 8192u

typedef struct {
    uint8_t input[TRANSFER_BUFFER];
    size_t input_len;
    size_t input_pos;
    uint8_t output[TRANSFER_BUFFER];
    size_t output_len;
    bool put_ready;
} fake_transfer_t;

static fake_transfer_t s_transfer;

static uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void store_le64(uint8_t *p, uint64_t value)
{
    for (unsigned i = 0u; i < 8u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static void store_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static int transfer_read(void *ctx, uint8_t *buffer, size_t size, uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    if (!s_transfer.put_ready || s_transfer.input_pos >= s_transfer.input_len) return 0;
    size_t available = s_transfer.input_len - s_transfer.input_pos;
    if (size > available) size = available;
    memcpy(buffer, s_transfer.input + s_transfer.input_pos, size);
    s_transfer.input_pos += size;
    return (int)size;
}

static int transfer_write(void *ctx, const uint8_t *buffer, size_t size, uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    if (s_transfer.output_len + size > sizeof(s_transfer.output)) return -1;
    memcpy(s_transfer.output + s_transfer.output_len, buffer, size);
    s_transfer.output_len += size;

    static const char ready[] = "MFT1 PUT READY\n";
    if (s_transfer.output_len >= sizeof(ready) - 1u &&
        memcmp(s_transfer.output + s_transfer.output_len - (sizeof(ready) - 1u),
               ready, sizeof(ready) - 1u) == 0) {
        s_transfer.put_ready = true;
    }
    return (int)size;
}

static int find_node(const char *path)
{
    for (int i = 0; i < 16; ++i) {
        if (g_fake.fs_nodes[i].exists && strcmp(g_fake.fs_nodes[i].path, path) == 0) return i;
    }
    return -1;
}

static int replace_file(void *ctx, const char *temporary_path, const char *destination_path)
{
    (void)ctx;
    int temporary = find_node(temporary_path);
    if (temporary < 0) return -1;
    int destination = find_node(destination_path);
    if (destination >= 0 && destination != temporary) g_fake.fs_nodes[destination].exists = false;
    snprintf(g_fake.fs_nodes[temporary].path, sizeof(g_fake.fs_nodes[temporary].path),
             "%s", destination_path);
    return 0;
}

static void remove_file(void *ctx, const char *path)
{
    (void)ctx;
    int node = find_node(path);
    if (node >= 0) g_fake.fs_nodes[node].exists = false;
}

static void configure_transfer(void)
{
    memset(&s_transfer, 0, sizeof(s_transfer));
    minishell_transfer_port_t port = {
        .ctx = NULL,
        .read = transfer_read,
        .write = transfer_write,
        .replace_file = replace_file,
        .remove_file = remove_file,
    };
    minishell_transfer_configure(&port);
}

static void prepare_put_input(const uint8_t *payload, size_t size, uint32_t crc)
{
    uint8_t *p = s_transfer.input;
    p[0] = 'M'; p[1] = 'F'; p[2] = 'T'; p[3] = '1';
    store_le64(&p[4], size);
    store_le32(&p[12], crc);
    memcpy(&p[16], payload, size);
    s_transfer.input_len = 16u + size;
}

bool test_transfer(void)
{
    static const uint8_t payload[] = {0x00u, 0x01u, 0x7fu, 0x80u, 0xffu, 'M', 'F', 'T', '1'};
    const uint32_t crc = crc32_bytes(payload, sizeof(payload));

    fake_reset();
    minishell_services_port_t services = fake_full_port();
    minishell_services_configure(&services);
    fake_fs_add_file("/sd/new.bin", "old");
    configure_transfer();
    prepare_put_input(payload, sizeof(payload), crc);

    TEST_EQ(minishell_transfer_put("/sd/new.bin"), 0);
    int node = find_node("/sd/new.bin");
    TEST_CHECK(node >= 0);
    TEST_EQ(g_fake.fs_nodes[node].size, (uint32_t)sizeof(payload));
    TEST_CHECK(memcmp(g_fake.fs_nodes[node].data, payload, sizeof(payload)) == 0);
    TEST_CHECK(find_node("/sd/new.bin.mft.part") < 0);
    TEST_CHECK(s_transfer.output_len >= 8u);
    TEST_CHECK(memmem(s_transfer.output, s_transfer.output_len, "MFT1 OK ", 8u) != NULL);

    configure_transfer();
    s_transfer.put_ready = true; /* get has no incoming binary phase */
    TEST_EQ(minishell_transfer_get("/sd/new.bin"), 0);

    uint8_t *line_end = memchr(s_transfer.output, '\n', s_transfer.output_len);
    TEST_CHECK(line_end != NULL);
    size_t header_len = (size_t)(line_end - s_transfer.output) + 1u;
    TEST_CHECK(header_len + sizeof(payload) <= s_transfer.output_len);
    TEST_CHECK(memcmp(s_transfer.output + header_len, payload, sizeof(payload)) == 0);
    static const char trailer[] = "\nMFT1 OK\n";
    TEST_CHECK(s_transfer.output_len >= sizeof(trailer) - 1u);
    TEST_CHECK(memcmp(s_transfer.output + s_transfer.output_len - (sizeof(trailer) - 1u),
                      trailer, sizeof(trailer) - 1u) == 0);

    fake_reset();
    services = fake_full_port();
    minishell_services_configure(&services);
    configure_transfer();
    prepare_put_input(payload, sizeof(payload), crc ^ 1u);
    TEST_CHECK(minishell_transfer_put("/sd/bad.bin") != 0);
    TEST_CHECK(find_node("/sd/bad.bin") < 0);
    TEST_CHECK(find_node("/sd/bad.bin.mft.part") < 0);

    minishell_transfer_configure(NULL);
    return true;
}
