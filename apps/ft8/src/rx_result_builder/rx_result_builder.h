#ifndef RX_RESULT_BUILDER_H
#define RX_RESULT_BUILDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ft8_message_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RX_RESULT_CALL_CAP FT8_PROTOCOL_CALL_CAP
#define RX_RESULT_EXTRA_CAP FT8_PROTOCOL_EXTRA_CAP
#define RX_RESULT_TEXT_CAP FT8_PROTOCOL_TEXT_CAP
#define RX_RESULT_FD_EXCHANGE_CAP 12u
#define RX_RESULT_REPORT_UNKNOWN (-99)

typedef enum {
    RX_RESULT_OK = 0,
    RX_RESULT_ERR_INVALID = -1,
    RX_RESULT_ERR_NOT_INITIALIZED = -2,
    RX_RESULT_ERR_OUTPUT_FULL = -3
} RxResultStatus;

/* Factual FT8 QSO message stage. This is RX classification, not TX policy. */
typedef uint8_t RxQsoMessageKind;
enum {
    RX_QSO_MSG_NONE = 0u,
    RX_QSO_MSG_TX1,
    RX_QSO_MSG_TX2,
    RX_QSO_MSG_TX3,
    RX_QSO_MSG_TX4,
    RX_QSO_MSG_TX5
};

typedef struct {
    char local_callsign[RX_RESULT_CALL_CAP];
} RxResultBuilderConfig;

typedef struct {
    int initialized;
    RxResultBuilderConfig config;
} RxResultBuilder;

typedef struct {
    uint8_t payload[FT8_PAYLOAD_BYTES];
    Ft8ProtocolType protocol_type;
    Ft8ProtocolParseStatus parse_status;
    bool has_unresolved_hash;

    /* Factual application classification only; no reply/TX policy. */
    bool is_cq;
    bool is_to_me;
    bool is_fd;
    RxQsoMessageKind qso_kind;
    int8_t report_db;

    char canonical_text[RX_RESULT_TEXT_CAP];

    /* Type-dependent factual fields when a protocol family provides them. */
    char call_to[RX_RESULT_CALL_CAP];
    char call_de[RX_RESULT_CALL_CAP];
    char extra[RX_RESULT_EXTRA_CAP];
    char fd_exchange[RX_RESULT_FD_EXCHANGE_CAP];

    /* Factual RX measurements used later by AutoSeq/QSO policy. */
    int16_t offset_hz;
    int8_t snr_db;

    Ft8Candidate candidate;
    int ldpc_errors;
    uint16_t crc_extracted;
    uint16_t crc_calculated;
} RxMessage;

typedef struct {
    int64_t slot_id;
    Ft8ProtocolSlotStatus protocol_status;
    RxMessage *messages;
    size_t capacity;
    size_t message_count;
} RxBatch;

RxResultBuilderConfig rx_result_builder_default_config(void);
RxResultStatus rx_result_builder_init(RxResultBuilder *builder,
                                      const RxResultBuilderConfig *config);
void rx_result_builder_destroy(RxResultBuilder *builder);

/*
 * Convert one typed protocol slot into factual MiniFT8 application results.
 * The caller owns output storage. No AutoSeq, reply selection, TX state, UI
 * ordering, IgnoreList, or other policy is applied here.
 */
RxResultStatus rx_result_builder_build(const RxResultBuilder *builder,
                                       const Ft8ProtocolSlot *slot,
                                       RxMessage *message_storage,
                                       size_t message_capacity,
                                       RxBatch *out_batch);

#ifdef __cplusplus
}
#endif

#endif
