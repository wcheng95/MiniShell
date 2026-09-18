#include "test_support.h"

static unsigned opens, closes, transfers;
static uint32_t last_timeout;
static mini_result_t open_result, io_result, close_result;
static bool overreport;

static mini_result_t fake_open(void *ctx, const char *endpoint, minishell_backend_serial_t *out)
{
    (void)ctx; (void)endpoint;
    ++opens;
    *out = open_result == MINI_OK ? 77u : 0;
    return open_result;
}
static mini_result_t fake_read(void *ctx, minishell_backend_serial_t stream,
                               void *buffer, uint32_t size, uint32_t *out, uint32_t timeout)
{
    (void)ctx;
    if (stream != 77u) abort();
    ++transfers;
    last_timeout = timeout;
    memset(buffer, 42, size);
    *out = overreport ? size + 1 : size / 2;
    return io_result;
}
static mini_result_t fake_write(void *ctx, minishell_backend_serial_t stream,
                                const void *buffer, uint32_t size, uint32_t *out, uint32_t timeout)
{
    (void)ctx; (void)buffer;
    if (stream != 77u) abort();
    ++transfers;
    last_timeout = timeout;
    *out = overreport ? size + 1 : size / 2;
    return io_result;
}
static mini_result_t fake_close(void *ctx, minishell_backend_serial_t stream)
{
    (void)ctx;
    if (stream != 77u) abort();
    ++closes;
    return close_result;
}

bool test_serial(void)
{
    minishell_services_configure(NULL);
    TEST_CHECK(mini_api_get()->serial == NULL);
    minishell_services_port_t port = {.serial_capabilities = MINI_SERIAL_CAP_READ | MINI_SERIAL_CAP_WRITE,
        .serial_open = fake_open, .serial_read = fake_read, .serial_write = fake_write,
        .serial_close = fake_close};
    minishell_services_configure(&port);
    const mini_serial_api_t *api = mini_api_get()->serial;
    TEST_CHECK(api && api->capabilities == (MINI_SERIAL_CAP_READ | MINI_SERIAL_CAP_WRITE));
    mini_serial_t stream = 99, other = 99;
    char buffer[4]; uint32_t n = 99;
    TEST_EQ(api->open("x", NULL), MINI_ERR_INVALID);
    TEST_EQ(api->open(NULL, &stream), MINI_ERR_INVALID);
    TEST_EQ(stream, MINI_SERIAL_INVALID);
    TEST_EQ(api->open("", &stream), MINI_ERR_INVALID);
    TEST_EQ(opens, 0);
    open_result = MINI_ERR_IO;
    TEST_EQ(api->open("x", &stream), MINI_ERR_IO);
    TEST_EQ(stream, MINI_SERIAL_INVALID);
    open_result = MINI_OK;
    TEST_EQ(api->open("x", &stream), MINI_OK);
    TEST_CHECK(stream != 0);
    TEST_EQ(api->open("x", &other), MINI_ERR_TOO_MANY_OPEN);
    TEST_EQ(other, MINI_SERIAL_INVALID);
    TEST_EQ(api->read(0, buffer, 4, &n, 0), MINI_ERR_BAD_HANDLE);
    TEST_EQ(n, 0);
    TEST_EQ(api->write(0, buffer, 4, &n, 0), MINI_ERR_BAD_HANDLE);
    TEST_EQ(api->close(0), MINI_ERR_BAD_HANDLE);
    TEST_EQ(api->read(stream, buffer, 4, NULL, 0), MINI_ERR_INVALID);
    TEST_EQ(api->write(stream, buffer, 4, NULL, 0), MINI_ERR_INVALID);
    TEST_EQ(api->read(stream, NULL, 4, &n, 0), MINI_ERR_INVALID);
    TEST_EQ(api->write(stream, NULL, 4, &n, 0), MINI_ERR_INVALID);
    TEST_EQ(api->read(stream, NULL, 0, &n, 0), MINI_OK);
    TEST_EQ(api->write(stream, NULL, 0, &n, 0), MINI_OK);
    TEST_EQ(transfers, 0);
    TEST_EQ(api->read(stream, buffer, 4, &n, 123), MINI_OK);
    TEST_EQ(n, 2); TEST_EQ(last_timeout, 123); TEST_EQ(buffer[0], 42);
    TEST_EQ(api->write(stream, buffer, 4, &n, MINI_WAIT_FOREVER), MINI_OK);
    TEST_EQ(n, 2); TEST_EQ(last_timeout, MINI_WAIT_FOREVER);
    io_result = MINI_ERR_TIMEOUT;
    TEST_EQ(api->write(stream, buffer, 4, &n, 19), MINI_ERR_TIMEOUT);
    TEST_EQ(n, 0); TEST_EQ(last_timeout, 19);
    io_result = MINI_OK; overreport = true;
    TEST_EQ(api->read(stream, buffer, 4, &n, 0), MINI_ERR_IO);
    TEST_EQ(n, 0);
    TEST_EQ(api->write(stream, buffer, 4, &n, 0), MINI_ERR_IO);
    overreport = false;
    close_result = MINI_ERR_IO;
    TEST_EQ(api->close(stream), MINI_ERR_IO);
    TEST_EQ(api->close(stream), MINI_ERR_BAD_HANDLE);
    close_result = MINI_OK;
    TEST_EQ(api->open("x", &other), MINI_OK);
    TEST_CHECK(other != stream);
    TEST_EQ(api->write(stream, buffer, 4, &n, 0), MINI_ERR_BAD_HANDLE);
    minishell_services_app_end();
    TEST_EQ(closes, 2);
    TEST_EQ(api->read(other, buffer, 4, &n, 0), MINI_ERR_BAD_HANDLE);
    minishell_services_app_end();
    TEST_EQ(closes, 2);
    TEST_EQ(api->open("x", &stream), MINI_OK);
    minishell_services_configure(NULL); /* Cleanup uses the old provider. */
    TEST_EQ(closes, 3);
    TEST_CHECK(mini_api_get()->serial == NULL);
    TEST_EQ(api->open("x", &stream), MINI_ERR_UNSUPPORTED);
    port.serial_read = NULL;
    minishell_services_configure(&port);
    api = mini_api_get()->serial;
    TEST_EQ(api->capabilities, MINI_SERIAL_CAP_WRITE);
    TEST_EQ(api->open("x", &stream), MINI_OK);
    TEST_EQ(api->read(stream, buffer, 4, &n, 0), MINI_ERR_UNSUPPORTED);
    minishell_services_configure(NULL);
    return true;
}
