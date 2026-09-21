/* Private logical-input seam. ui_service keeps the Mini-CW event vocabulary;
 * the implementation consumes MiniShell key events, never raw keyboard state. */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MINICW_INPUT_EVENT_NONE = 0,
    MINICW_INPUT_EVENT_CHAR,
    MINICW_INPUT_EVENT_FN,
    MINICW_INPUT_EVENT_CTRL,
    MINICW_INPUT_EVENT_OPT,
    MINICW_INPUT_EVENT_ALT,
    MINICW_INPUT_EVENT_BACKSPACE_HOLD,
} minicw_input_event_type_t;

typedef struct {
    minicw_input_event_type_t type;
    char ch;
    bool fn;
    bool shift;
    bool ctrl;
    bool opt;
    bool alt;
    bool caps_lock;
} minicw_input_event_t;

bool minicw_input_poll_input(minicw_input_event_t *out_event);
#ifdef __cplusplus
}
#endif
