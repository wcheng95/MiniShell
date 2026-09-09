#ifndef FT8_MINISHELL_UI_ADAPTER_H
#define FT8_MINISHELL_UI_ADAPTER_H

#include <stdbool.h>

#include "minishell/api.h"
#include "ft8/app_types.h"
#include "presentation_profile.h"

typedef struct {
    const mini_display_api_t *display;
    const mini_text_display_api_t *text;
    const mini_key_input_api_t *key;
    ft8_presentation_profile_t presentation;
} ft8_ui_adapter_t;

bool ft8_ui_adapter_init(ft8_ui_adapter_t *adapter, const mini_api_t *api,
                         ft8_presentation_profile_t presentation);
bool ft8_ui_adapter_render(const ft8_ui_adapter_t *adapter, const UiFrame *frame);
bool ft8_ui_adapter_read_input(const ft8_ui_adapter_t *adapter, UiInput *out_input);
void ft8_ui_adapter_shutdown(const ft8_ui_adapter_t *adapter);

#endif
