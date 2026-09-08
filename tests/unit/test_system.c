#include "test_support.h"

bool test_system(void)
{
    fake_reset();
    minishell_services_port_t p = fake_minimal_port();
    minishell_services_configure(&p);
    const mini_api_t *api = mini_api_get();
    TEST_CHECK(api != NULL);
    TEST_EQ(api->api_version, MINISHELL_API_VERSION);
    TEST_CHECK(api->system != NULL);
    TEST_CHECK(api->system->write != NULL);
    api->system->write("hello");
    api->system->write("\nworld");
    TEST_CHECK(strcmp(g_fake.output, "hello\nworld") == 0);
    api->system->write(NULL);
    TEST_CHECK(strcmp(g_fake.output, "hello\nworld") == 0);
    return true;
}
