#include "storage_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static struct {
    const char *text;
    size_t offset;
    uint32_t chunk;
    mini_result_t open_result;
    mini_result_t read_result;
    mini_result_t close_result;
    unsigned fail_read;
    unsigned opens;
    unsigned reads;
    unsigned closes;
} fake;

static mini_result_t fake_open(const char *path, uint32_t flags, mini_file_t *file)
{
    CHECK(strcmp(path, "/flash/ft8/station.txt") == 0);
    CHECK(flags == MINI_FS_READ);
    ++fake.opens;
    if (fake.open_result == MINI_OK) *file = 42u;
    return fake.open_result;
}

static mini_result_t fake_read(mini_file_t file, void *buffer, uint32_t size,
                               uint32_t *out_read)
{
    CHECK(file == 42u && fake.closes == 0u);
    ++fake.reads;
    *out_read = 0u;
    if (fake.reads == fake.fail_read) return fake.read_result;
    size_t count = strlen(fake.text) - fake.offset;
    if (count > size) count = size;
    if (count > fake.chunk) count = fake.chunk;
    memcpy(buffer, fake.text + fake.offset, count);
    fake.offset += count;
    *out_read = (uint32_t)count;
    return MINI_OK;
}

static mini_result_t fake_close(mini_file_t file)
{
    CHECK(file == 42u && fake.closes == 0u);
    ++fake.closes;
    return fake.close_result;
}

static void run_case(const char *name, const char *text, uint32_t chunk,
                     mini_result_t opened, unsigned fail_read,
                     mini_result_t read_error, mini_result_t closed,
                     StorageReadResult expected, unsigned reads)
{
    /* Only read-path operations are available; writes cannot pass unnoticed. */
    const mini_fs_api_t fs = {
        .struct_size = sizeof(fs), .open = fake_open,
        .read = fake_read, .close = fake_close
    };
    StorageService storage = {.fs = &fs};
    char out[8];
    memset(out, 0x7f, sizeof(out));
    memset(&fake, 0, sizeof(fake));
    fake.text = text;
    fake.chunk = chunk;
    fake.open_result = opened;
    fake.fail_read = fail_read;
    fake.read_result = read_error;
    fake.close_result = closed;
    CHECK(storage_service_read_text(&storage, "/flash/ft8/station.txt",
                                    out, sizeof(out)) == expected);
    CHECK(fake.opens == 1u);
    CHECK(fake.reads == reads);
    CHECK(fake.closes == (opened == MINI_OK ? 1u : 0u));
    if (expected == STORAGE_READ_FOUND) {
        CHECK(memcmp(out, text, strlen(text) + 1u) == 0);
    }
    printf("PASS: %s\n", name);
}

int main(void)
{
    run_case("small text", "abc\n", 99, MINI_OK, 0, MINI_OK, MINI_OK,
             STORAGE_READ_FOUND, 2);
    run_case("empty text", "", 99, MINI_OK, 0, MINI_OK, MINI_OK,
             STORAGE_READ_FOUND, 1);
    run_case("short reads", "abc\n", 2, MINI_OK, 0, MINI_OK, MINI_OK,
             STORAGE_READ_FOUND, 3);
    run_case("exact fit plus NUL", "1234567", 99, MINI_OK, 0, MINI_OK, MINI_OK,
             STORAGE_READ_FOUND, 2);
    run_case("missing", "", 99, MINI_ERR_NOT_FOUND, 0, MINI_OK, MINI_OK,
             STORAGE_READ_NOT_FOUND, 0);
    const mini_result_t errors[] = {MINI_ERR_ACCESS, MINI_ERR_IO, MINI_ERR_IS_DIR};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        run_case("open failure", "", 99, errors[i], 0, MINI_OK, MINI_OK,
                 STORAGE_READ_ERROR, 0);
    }
    run_case("read failure", "abc", 99, MINI_OK, 1, MINI_ERR_IO, MINI_OK,
             STORAGE_READ_ERROR, 1);
    run_case("read failure after progress", "abc", 2, MINI_OK, 2,
             MINI_ERR_NOT_FOUND, MINI_OK, STORAGE_READ_ERROR, 2);
    run_case("oversized", "12345678", 99, MINI_OK, 0, MINI_OK, MINI_OK,
             STORAGE_READ_ERROR, 2);
    run_case("EOF probe failure", "1234567", 99, MINI_OK, 2, MINI_ERR_IO,
             MINI_OK, STORAGE_READ_ERROR, 2);
    run_case("close failure", "abc", 99, MINI_OK, 0, MINI_OK, MINI_ERR_IO,
             STORAGE_READ_ERROR, 2);
    run_case("read and close failure", "abc", 99, MINI_OK, 1, MINI_ERR_IO,
             MINI_ERR_IO, STORAGE_READ_ERROR, 1);
    return 0;
}
