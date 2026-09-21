#pragma once
#include "minishell/api.h"
#include "ui_shell.h"
typedef struct {
    const mini_display_api_t *display;
    const mini_key_input_api_t *input;
    ui_frame_t last;
    bool presented;
} ui_adapter_t;
mini_result_t ui_adapter_open(ui_adapter_t *adapter, const mini_api_t *api);
bool ui_adapter_read(ui_adapter_t *adapter, ui_input_t *event);
mini_result_t ui_adapter_present(ui_adapter_t *adapter, const ui_frame_t *frame);
void ui_adapter_close(ui_adapter_t *adapter);
