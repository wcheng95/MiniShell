#include <string.h>
#include "../common/tone_sim.h"

#include "linux_audio_wav.h"
#include "linux_audio_buffered.h"
#include "platform_backend.h"
#include "linux_internal.h"

void minishell_platform_services_prepare(minishell_services_port_t *out_port)
{
    if (out_port == NULL) return;

    memset(out_port, 0, sizeof(*out_port));
    const minishell_services_port_t *base = minishell_platform_services_port();
    if (base == NULL) return;

    *out_port = *base;
    linux_audio_wav_configure(out_port);
    linux_audio_buffered_configure(out_port);
    linux_serial_configure(out_port);
    tone_sim_configure(out_port);
}

void minishell_platform_services_started(void)
{
    /* Linux currently has no resident producer that needs post-service start. */
}

void minishell_platform_services_stopping(void)
{
    /* Linux currently has no resident producer that needs pre-service stop. */
}
