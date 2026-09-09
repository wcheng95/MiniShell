#include "test_support.h"

static char s_console_output[128];
static uint32_t s_console_len;

static void capture_console(void *ctx, const char *text)
{
    (void)ctx;
    if (text == NULL) return;
    size_t length = strlen(text);
    if (length > sizeof(s_console_output) - s_console_len - 1u) {
        length = sizeof(s_console_output) - s_console_len - 1u;
    }
    memcpy(s_console_output + s_console_len, text, length);
    s_console_len += (uint32_t)length;
    s_console_output[s_console_len] = '\0';
}

bool test_console(void)
{
    fake_reset();
    memset(s_console_output, 0, sizeof(s_console_output));
    s_console_len = 0u;

    minishell_services_port_t p = fake_minimal_port();
    p.console_write = capture_console;
    minishell_services_configure(&p);

    const mini_api_t *api = mini_api_get();
    TEST_CHECK(api != NULL);
    TEST_EQ(api->api_version, MINISHELL_API_VERSION);
    TEST_CHECK(api->console != NULL);
    TEST_CHECK(api->console->write != NULL);

    api->console->write("hello");
    api->console->write("\nworld");
    TEST_CHECK(strcmp(s_console_output, "hello\nworld") == 0);
    TEST_CHECK(g_fake.output[0] == '\0');

    api->console->write(NULL);
    TEST_CHECK(strcmp(s_console_output, "hello\nworld") == 0);

    p.console_write = NULL;
    minishell_services_configure(&p);
    api = mini_api_get();
    TEST_CHECK(api->console == NULL);
    TEST_CHECK(api->system != NULL);
    return true;
}
