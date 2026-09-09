#ifndef FT8_MESSAGE_CODEC_H
#define FT8_MESSAGE_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ft8_decoder.h"
#include "ft8_hash_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FT8_PROTOCOL_TEXT_CAP 64u
#define FT8_PROTOCOL_CALL_CAP 14u
#define FT8_PROTOCOL_EXTRA_CAP 8u
#define FT8_PROTOCOL_SECTION_CAP 4u
#define FT8_PROTOCOL_TELEMETRY_BYTES 9u
#define FT8_PROTOCOL_TELEMETRY_HEX_CAP 19u

typedef enum {
    FT8_PROTOCOL_FREE_TEXT = 0,
    FT8_PROTOCOL_DXPEDITION,
    FT8_PROTOCOL_EU_VHF,
    FT8_PROTOCOL_ARRL_FD,
    FT8_PROTOCOL_TELEMETRY,
    FT8_PROTOCOL_STANDARD,
    FT8_PROTOCOL_ARRL_RTTY,
    FT8_PROTOCOL_NONSTD_CALL,
    FT8_PROTOCOL_WWROF,
    FT8_PROTOCOL_UNKNOWN
} Ft8ProtocolType;

typedef enum {
    FT8_PROTOCOL_PARSE_OK = 0,
    FT8_PROTOCOL_PARSE_UNSUPPORTED = 1,
    FT8_PROTOCOL_PARSE_MALFORMED = 2
} Ft8ProtocolParseStatus;

typedef enum {
    FT8_PROTOCOL_CODEC_OK = 0,
    FT8_PROTOCOL_CODEC_UNSUPPORTED = 1,
    FT8_PROTOCOL_CODEC_MALFORMED = 2,
    FT8_PROTOCOL_CODEC_ERR_INVALID = -1
} Ft8ProtocolCodecStatus;

typedef enum {
    FT8_PROTOCOL_FIELD_NONE = 0,
    FT8_PROTOCOL_FIELD_TOKEN,
    FT8_PROTOCOL_FIELD_TOKEN_WITH_ARG,
    FT8_PROTOCOL_FIELD_CALL,
    FT8_PROTOCOL_FIELD_GRID,
    FT8_PROTOCOL_FIELD_REPORT
} Ft8ProtocolFieldKind;

typedef enum {
    FT8_PROTOCOL_TERMINAL_NONE = 0,
    FT8_PROTOCOL_TERMINAL_RRR,
    FT8_PROTOCOL_TERMINAL_RR73,
    FT8_PROTOCOL_TERMINAL_73
} Ft8ProtocolTerminal;

typedef struct {
    char call_to[FT8_PROTOCOL_CALL_CAP];
    Ft8ProtocolFieldKind call_to_kind;
    char call_de[FT8_PROTOCOL_CALL_CAP];
    char extra[FT8_PROTOCOL_EXTRA_CAP];
    Ft8ProtocolFieldKind extra_kind;
} Ft8ProtocolStandard;

typedef struct {
    bool is_cq;
    char call_to[FT8_PROTOCOL_CALL_CAP];
    char call_de[FT8_PROTOCOL_CALL_CAP];
    Ft8ProtocolTerminal terminal;
} Ft8ProtocolNonstandard;

typedef struct {
    char call_to[FT8_PROTOCOL_CALL_CAP];
    char call_de[FT8_PROTOCOL_CALL_CAP];
    bool has_r;
    uint8_t transmitter_count;
    char class_letter;
    char section[FT8_PROTOCOL_SECTION_CAP];
} Ft8ProtocolArrlFd;

typedef struct {
    char rr73_call[FT8_PROTOCOL_CALL_CAP];
    char report_call[FT8_PROTOCOL_CALL_CAP];
    char fox_call[FT8_PROTOCOL_CALL_CAP];
    int8_t report_db;
} Ft8ProtocolDxpedition;

typedef struct {
    char text[14];
} Ft8ProtocolFreeText;

typedef struct {
    uint8_t bytes[FT8_PROTOCOL_TELEMETRY_BYTES];
    char hex[FT8_PROTOCOL_TELEMETRY_HEX_CAP];
} Ft8ProtocolTelemetry;

typedef union {
    Ft8ProtocolStandard standard;
    Ft8ProtocolNonstandard nonstandard;
    Ft8ProtocolArrlFd arrl_fd;
    Ft8ProtocolDxpedition dxpedition;
    Ft8ProtocolFreeText free_text;
    Ft8ProtocolTelemetry telemetry;
} Ft8ProtocolData;

typedef struct {
    uint8_t payload[FT8_PAYLOAD_BYTES];
    Ft8ProtocolType type;
    Ft8ProtocolParseStatus parse_status;
    bool has_unresolved_hash;
    char canonical_text[FT8_PROTOCOL_TEXT_CAP];
    Ft8ProtocolData data;

    /* Factual decoder diagnostics copied from the RX-1D boundary. */
    Ft8Candidate candidate;
    int ldpc_errors;
    uint16_t crc_extracted;
    uint16_t crc_calculated;
} Ft8ProtocolMessage;

typedef enum {
    FT8_PROTOCOL_SLOT_EMPTY = 0,
    FT8_PROTOCOL_SLOT_OK,
    FT8_PROTOCOL_SLOT_FULL
} Ft8ProtocolSlotStatus;

typedef struct {
    int64_t slot_id;
    Ft8ProtocolSlotStatus status;
    Ft8ProtocolMessage *messages;
    size_t capacity;
    size_t message_count;
} Ft8ProtocolSlot;

typedef enum {
    FT8_PROTOCOL_SLOT_ADDED = 0,
    FT8_PROTOCOL_SLOT_DUPLICATE = 1,
    FT8_PROTOCOL_SLOT_ERR_INVALID = -1,
    FT8_PROTOCOL_SLOT_ERR_FULL = -2
} Ft8ProtocolSlotAddStatus;

uint8_t ft8_protocol_get_i3(const uint8_t payload[FT8_PAYLOAD_BYTES]);
uint8_t ft8_protocol_get_n3(const uint8_t payload[FT8_PAYLOAD_BYTES]);
Ft8ProtocolType ft8_protocol_get_type(const uint8_t payload[FT8_PAYLOAD_BYTES]);

/* FT8 callsign hash mathematics shared by RX protocol decode and later TX codec work. */
Ft8ProtocolCodecStatus ft8_protocol_callsign_hash22(const char *callsign,
                                                    uint32_t *out_hash22);

Ft8ProtocolCodecStatus ft8_protocol_decode(const Ft8DecodedPayload *decoded,
                                           Ft8HashStore *hash_store,
                                           Ft8ProtocolMessage *out_message);

void ft8_protocol_slot_init(Ft8ProtocolSlot *slot,
                            int64_t slot_id,
                            Ft8ProtocolMessage *message_storage,
                            size_t capacity);
Ft8ProtocolSlotAddStatus ft8_protocol_slot_add_unique(Ft8ProtocolSlot *slot,
                                                     const Ft8ProtocolMessage *message);
Ft8ProtocolSlotAddStatus ft8_protocol_slot_decode_add(Ft8ProtocolSlot *slot,
                                                     const Ft8DecodedPayload *decoded,
                                                     Ft8HashStore *hash_store,
                                                     Ft8ProtocolCodecStatus *out_codec_status);

#ifdef __cplusplus
}
#endif

#endif
