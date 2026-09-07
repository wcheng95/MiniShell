#pragma once

#include "minishell_services.h"

void linux_audio_wav_configure(minishell_services_port_t *port, const char *root_dir);
void linux_audio_wav_shutdown(void);
