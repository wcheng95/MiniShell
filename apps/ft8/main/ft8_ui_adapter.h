#ifndef FT8_MINISHELL_UI_ADAPTER_H
#define FT8_MINISHELL_UI_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#include "minishell/api.h"
#include "ft8/app_types.h"
#include "presentation_profile.h"

#define FT8_WATERFALL_WIDTH 240u
#define FT8_WATERFALL_HEIGHT 19u

typedef struct Ft8WaterfallHistory Ft8WaterfallHistory;

typedef struct {
    const mini_display_api_t *display;
    const mini_text_display_api_t *text;
    const mini_key_input_api_t *key;
    const mini_memory_api_t *memory;
    const mini_raster_display_api_t *raster;
    Ft8WaterfallHistory *waterfall;
    ft8_presentation_profile_t presentation;
} ft8_ui_adapter_t;

bool ft8_ui_adapter_init(ft8_ui_adapter_t *adapter, const mini_api_t *api,
                         ft8_presentation_profile_t presentation);
bool ft8_ui_adapter_waterfall_supported(const ft8_ui_adapter_t *adapter);
void ft8_ui_adapter_push_waterfall(ft8_ui_adapter_t *adapter, const uint8_t row[FT8_WATERFALL_WIDTH]);
bool ft8_ui_adapter_render(const ft8_ui_adapter_t *adapter, const UiFrame *frame);

/* Read one normalized MiniFT8 input event. Timeout/no-event is not an error. */
bool ft8_ui_adapter_read_input_timeout(const ft8_ui_adapter_t *adapter,
                                       uint32_t timeout_ms,
                                       UiInput *out_input,
                                       bool *out_has_input);

/* Compatibility blocking wrapper. */
bool ft8_ui_adapter_read_input(const ft8_ui_adapter_t *adapter, UiInput *out_input);
void ft8_ui_adapter_shutdown(const ft8_ui_adapter_t *adapter);

#endif
