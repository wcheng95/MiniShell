#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool battery_percent_valid;
    uint8_t battery_percent;
    bool charging_valid;
    bool charging;
} minishell_power_status_t;

typedef struct {
    void *ctx;
    int (*get_status)(void *ctx, minishell_power_status_t *out_status);
    int (*suspend)(void *ctx);
    int (*poweroff)(void *ctx);
} minishell_power_port_t;

void minishell_power_configure(const minishell_power_port_t *port);
int minishell_power_get_status(minishell_power_status_t *out_status);
int minishell_power_suspend(void);
int minishell_power_poweroff(void);

#ifdef __cplusplus
}
#endif
