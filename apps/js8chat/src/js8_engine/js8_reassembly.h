#ifndef JS8_REASSEMBLY_H
#define JS8_REASSEMBLY_H

#include "js8_directed.h"

#define JS8_RX_CONTEXTS 4
#define JS8_RX_MESSAGE_SIZE 1024

typedef enum { JS8_RX_FRAGMENT_DIRECTED, JS8_RX_FRAGMENT_DATA } Js8RxFragmentKind;
typedef struct {
    Js8RxFragmentKind kind;
    uint32_t slot_index;
    int32_t frequency_millihz;
    uint8_t tx_flags;
    char from[JS8_CALLSIGN28_SIZE], to[JS8_CALLSIGN28_SIZE];
    uint8_t command_code;
    const char *text;
    uint16_t text_len;
} Js8RxFragment;

typedef struct {
    char from[JS8_CALLSIGN28_SIZE], to[JS8_CALLSIGN28_SIZE];
    int32_t frequency_millihz;
    uint32_t first_slot, last_slot;
    char text[JS8_RX_MESSAGE_SIZE];
    uint16_t text_len;
} Js8RxMessage;

typedef struct {
    Js8RxMessage message;
    uint8_t active, gap, command_code;
} Js8RxContext;
typedef struct { Js8RxContext contexts[JS8_RX_CONTEXTS]; } Js8RxReassembly;

typedef enum {
    JS8_RX_ACCEPTED, JS8_RX_COMPLETE, JS8_RX_IGNORED, JS8_RX_ORPHAN,
    JS8_RX_DUPLICATE, JS8_RX_GAP, JS8_RX_OVERFLOW, JS8_RX_INVALID
} Js8RxStatus;

/* Context bit masks report independent cleanup even when this event completes
 * another stream. A new FIRST replaces its closest match, or evicts the oldest
 * last_slot if full. Ties always use the lowest context index.
 */
typedef struct { uint8_t expired, replaced, evicted; } Js8RxDrops;

/* Caller owns all storage. Zero initialization is also a valid initial state. */
void js8_rx_reassembly_init(Js8RxReassembly *state);

/* Normal semantic events only: DIRECTED means standard DIRECTED, DATA means
 * successfully decoded text from either codec. No wall clock or codec access.
 * All pointers required except text when text_len is zero. Objects must not
 * overlap; text must point to at least text_len readable bytes during the call.
 * Invalid arguments leave all outputs/state unchanged. message changes only on
 * COMPLETE; drops is reset on every valid call. GAP marks both a newly detected
 * gap and an incomplete LAST (the latter clears the context). Expiry uses >6
 * slots since the latest fragment, with no synthetic completion. Slot indices
 * are non-wrapping; DATA at/before last_slot is DUPLICATE with no append.
 */
Js8RxStatus js8_rx_reassembly_feed(Js8RxReassembly *state, const Js8RxFragment *event,
                                  Js8RxMessage *message, Js8RxDrops *drops);
#endif
