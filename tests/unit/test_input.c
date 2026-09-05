#include "test_support.h"

static mini_key_event_t char_event(uint32_t cp)
{
    mini_key_event_t e = {.struct_size = sizeof(mini_key_event_t), .type = MINI_KEY_EVENT_CHAR,
                          .codepoint = cp, .key = 0u, .modifiers = 0u};
    return e;
}

static mini_key_event_t special_event(uint32_t key)
{
    mini_key_event_t e = {.struct_size = sizeof(mini_key_event_t), .type = MINI_KEY_EVENT_SPECIAL,
                          .codepoint = 0u, .key = key, .modifiers = 0u};
    return e;
}

bool test_input(void)
{
    fake_reset();
    minishell_services_port_t p = fake_full_port();
    minishell_services_configure(&p);
    minishell_services_app_begin();

    const mini_input_api_t *input = mini_api_get()->input;
    TEST_CHECK(input != NULL);
    TEST_CHECK((input->capabilities & MINI_INPUT_CAP_KEY) != 0u);

    mini_key_event_t a = char_event('A');
    mini_key_event_t up = special_event(MINI_KEY_UP);
    TEST_EQ(minishell_services_input_submit(&a), MINI_OK);
    TEST_EQ(minishell_services_input_submit(&up), MINI_OK);
    TEST_EQ(g_fake.input_wake_calls, 2u);

    mini_key_event_t out = {.struct_size = sizeof(out)};
    TEST_EQ(input->key->read(&out, MINI_WAIT_NONE), MINI_OK);
    TEST_EQ(out.type, MINI_KEY_EVENT_CHAR);
    TEST_EQ(out.codepoint, (uint32_t)'A');
    out.struct_size = sizeof(out);
    TEST_EQ(input->key->read(&out, MINI_WAIT_NONE), MINI_OK);
    TEST_EQ(out.type, MINI_KEY_EVENT_SPECIAL);
    TEST_EQ(out.key, MINI_KEY_UP);

    out.struct_size = sizeof(out);
    TEST_EQ(input->key->read(&out, MINI_WAIT_NONE), MINI_ERR_NOT_READY);

    /* A pull-based terminal backend may discover an already-buffered byte only
     * when polled. MINI_WAIT_NONE must give the backend one zero-time poll. */
    g_fake.input_injected_event = char_event('p');
    g_fake.input_inject_on_wait = true;
    out.struct_size = sizeof(out);
    TEST_EQ(input->key->read(&out, MINI_WAIT_NONE), MINI_OK);
    TEST_EQ(out.codepoint, (uint32_t)'p');

    g_fake.mono_us = 1000u;
    out.struct_size = sizeof(out);
    TEST_EQ(input->key->read(&out, 10u), MINI_ERR_TIMEOUT);
    TEST_EQ(g_fake.mono_us, 11000u);

    g_fake.input_injected_event = char_event(0x4E2Du);
    g_fake.input_inject_on_wait = true;
    out.struct_size = sizeof(out);
    TEST_EQ(input->key->read(&out, MINI_WAIT_FOREVER), MINI_OK);
    TEST_EQ(out.codepoint, 0x4E2Du);

    mini_key_event_t invalid = char_event(0xD800u);
    TEST_EQ(minishell_services_input_submit(&invalid), MINI_ERR_INVALID);
    invalid = char_event(0x110000u);
    TEST_EQ(minishell_services_input_submit(&invalid), MINI_ERR_INVALID);
    invalid = char_event('x');
    invalid.key = MINI_KEY_UP;
    TEST_EQ(minishell_services_input_submit(&invalid), MINI_ERR_INVALID);
    invalid = special_event(0u);
    TEST_EQ(minishell_services_input_submit(&invalid), MINI_ERR_INVALID);

    mini_key_event_t small = {.struct_size = sizeof(uint32_t)};
    TEST_EQ(input->key->read(&small, MINI_WAIT_NONE), MINI_ERR_INVALID);

    minishell_services_input_flush();
    for (uint32_t i = 0; i < 64u; ++i) {
        mini_key_event_t e = char_event('a' + (i % 26u));
        TEST_EQ(minishell_services_input_submit(&e), MINI_OK);
    }
    mini_key_event_t extra = char_event('!');
    TEST_EQ(minishell_services_input_submit(&extra), MINI_ERR_NO_SPACE);

    minishell_services_app_end();
    minishell_services_app_begin();
    out.struct_size = sizeof(out);
    TEST_EQ(input->key->read(&out, MINI_WAIT_NONE), MINI_ERR_NOT_READY);

    fake_reset();
    minishell_services_port_t no_input = fake_full_port();
    no_input.input_wait = NULL;
    minishell_services_configure(&no_input);
    TEST_CHECK(mini_api_get()->input == NULL);
    return true;
}
