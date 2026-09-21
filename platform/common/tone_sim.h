#pragma once
#include "minishell_services.h"
/* Deterministic silent backend: advance the actual renderer against monotonic
 * time at API boundaries. No real-time thread or Linux PCM ownership needed. */
void tone_sim_configure(minishell_services_port_t *port);
