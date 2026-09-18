#ifndef ADV_USB_CONSOLE_HANDOFF_H
#define ADV_USB_CONSOLE_HANDOFF_H

#include <stdbool.h>

/* Foreground-owned lease. A failed operation retains its cleanup obligation. */
typedef struct {
    bool suspended;
    bool uart_attempted;
} adv_usb_console_handoff_t;

typedef struct {
    int (*suspend)(void);
    int (*uart_begin)(void);
    int (*uart_end)(void);
    int (*resume)(void);
} adv_usb_console_ops_t;

static inline bool adv_usb_console_begin(adv_usb_console_handoff_t *state,
                                        const adv_usb_console_ops_t *ops)
{
    if (state->suspended || ops->suspend() != 0) return false;
    state->suspended = true;
    state->uart_attempted = true;
    return ops->uart_begin() == 0;
}

static inline bool adv_usb_console_end(adv_usb_console_handoff_t *state,
                                      const adv_usb_console_ops_t *ops, bool usb_busy)
{
    if (usb_busy) return false;
    if (!state->suspended) return true;
    if (state->uart_attempted) {
        if (ops->uart_end() != 0) return false;
        state->uart_attempted = false;
    }
    if (ops->resume() != 0) return false;
    state->suspended = false;
    return true;
}
#endif
