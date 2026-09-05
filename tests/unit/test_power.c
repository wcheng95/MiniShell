#include "test_support.h"

#include "minishell_power.h"

static uint32_t s_status_calls;
static uint32_t s_suspend_calls;
static uint32_t s_poweroff_calls;

static int fake_power_status(void *ctx, minishell_power_status_t *out_status)
{
    (void)ctx;
    ++s_status_calls;
    out_status->battery_percent_valid = true;
    out_status->battery_percent = 150u; /* core must clamp malformed backend data */
    out_status->charging_valid = true;
    out_status->charging = true;
    return 0;
}

static int fake_suspend(void *ctx)
{
    (void)ctx;
    ++s_suspend_calls;
    return 17;
}

static int fake_poweroff(void *ctx)
{
    (void)ctx;
    ++s_poweroff_calls;
    return 23;
}

bool test_power(void)
{
    minishell_power_status_t status = {
        .battery_percent_valid = true,
        .battery_percent = 42u,
        .charging_valid = true,
        .charging = true,
    };

    minishell_power_configure(NULL);
    TEST_CHECK(minishell_power_get_status(NULL) != 0);
    TEST_CHECK(minishell_power_get_status(&status) != 0);
    TEST_CHECK(!status.battery_percent_valid);
    TEST_CHECK(!status.charging_valid);
    TEST_CHECK(minishell_power_suspend() != 0);
    TEST_CHECK(minishell_power_poweroff() != 0);

    s_status_calls = 0u;
    s_suspend_calls = 0u;
    s_poweroff_calls = 0u;

    const minishell_power_port_t port = {
        .ctx = NULL,
        .get_status = fake_power_status,
        .suspend = fake_suspend,
        .poweroff = fake_poweroff,
    };
    minishell_power_configure(&port);

    TEST_EQ(minishell_power_get_status(&status), 0);
    TEST_EQ(s_status_calls, 1u);
    TEST_CHECK(status.battery_percent_valid);
    TEST_EQ(status.battery_percent, 100u);
    TEST_CHECK(status.charging_valid);
    TEST_CHECK(status.charging);

    TEST_EQ(minishell_power_suspend(), 17);
    TEST_EQ(s_suspend_calls, 1u);
    TEST_EQ(minishell_power_poweroff(), 23);
    TEST_EQ(s_poweroff_calls, 1u);

    minishell_power_configure(NULL);
    TEST_CHECK(minishell_power_get_status(&status) != 0);
    TEST_CHECK(!status.battery_percent_valid);
    TEST_CHECK(!status.charging_valid);
    return true;
}
