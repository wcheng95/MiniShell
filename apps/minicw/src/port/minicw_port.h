#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Pinned Mini-CW sdkconfig uses 100 Hz. Keep its rounding and wrap arithmetic. */
static inline uint32_t minicw_ticks_from_ms(uint32_t ms) { return ms / 10U; }
uint32_t minicw_port_now_ms(void);
uint32_t minicw_port_ticks(void);
bool minicw_port_inputs_ready(void);
bool minicw_port_outputs_ready(void);
uint32_t minicw_port_read(uint32_t line);
void minicw_port_write(uint32_t line, uint32_t level);
void minicw_port_present(const char rows[7][21]);
