#ifndef JS8_FRONTEND_H
#define JS8_FRONTEND_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint8_t phase; } Js8Frontend;
/* Invalid/full output leaves phase and output unchanged. */
int js8_frontend_process(Js8Frontend *state, const int16_t *stereo, size_t frames,
                         float *out, size_t capacity, size_t *count);
/* Floor UTC slot/sample then backdate by produced count. Negative epochs are
 * represented here; live activity rejects negative/out-of-uint32 slot IDs. */
int js8_live_anchor(int64_t seconds, uint32_t ns, size_t produced,
                    int64_t *slot, uint32_t *offset);
/* Subtract bounded source latency before produced-sample backdating. */
int js8_live_delay(int64_t *seconds, uint32_t *ns, uint32_t delay_ms);
#endif
