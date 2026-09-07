#ifndef MINIFT8_MINISHELL_UI_ADAPTER_H
#define MINIFT8_MINISHELL_UI_ADAPTER_H

#include <stdbool.h>

#include "minishell/api.h"
#include "minift8/app_types.h"

typedef struct {
    const mini_display_api_t *display;
    const mini_text_display_api_t *text;
    const mini_key_input_api_t *key;
} MiniFt8UiAdapter;

bool minift8_ui_adapter_init(MiniFt8UiAdapter *adapter, const mini_api_t *api);
bool minift8_ui_adapter_render(const MiniFt8UiAdapter *adapter, const UiFrame *frame);
bool minift8_ui_adapter_read_input(const MiniFt8UiAdapter *adapter, UiInput *out_input);
void minift8_ui_adapter_shutdown(const MiniFt8UiAdapter *adapter);

#endif
