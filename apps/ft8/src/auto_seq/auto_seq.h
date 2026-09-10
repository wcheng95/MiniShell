#ifndef FT8_AUTO_SEQ_H
#define FT8_AUTO_SEQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUTO_SEQ_MAX_QUEUE 30u
#define AUTO_SEQ_CALL_CAP 14u
#define AUTO_SEQ_GRID_CAP 7u
#define AUTO_SEQ_FD_EXCHANGE_CAP 12u
#define AUTO_SEQ_DEFAULT_MAX_RETRY 5u
#define AUTO_SEQ_SNR_UNKNOWN (-99)

/* QsoContext flags. Scheduling position is represented by array zone, not a flag. */
#define AUTO_SEQ_FLAG_LOGGED              0x01u
#define AUTO_SEQ_FLAG_CABRILLO_LOGGED     0x02u
#define AUTO_SEQ_FLAG_FD                  0x04u
#define AUTO_SEQ_FLAG_PARK_AFTER_SIGNOFF  0x08u
#define AUTO_SEQ_FLAG_FREETEXT            0x10u

/* Normalized RX-event facts supplied by app_controller in later AS stages. */
#define AUTO_SEQ_RX_FLAG_CQ     0x01u
#define AUTO_SEQ_RX_FLAG_TO_ME  0x02u
#define AUTO_SEQ_RX_FLAG_FD     0x04u

typedef uint8_t AutoSeqState;
enum {
    AUTO_SEQ_STATE_CALLING = 0u,
    AUTO_SEQ_STATE_REPLYING,
    AUTO_SEQ_STATE_REPORT,
    AUTO_SEQ_STATE_ROGER_REPORT,
    AUTO_SEQ_STATE_ROGERS,
    AUTO_SEQ_STATE_SIGNOFF,
    AUTO_SEQ_STATE_IDLE
};

typedef uint8_t AutoSeqMessageKind;
enum {
    AUTO_SEQ_MSG_NONE = 0u,
    AUTO_SEQ_MSG_TX1,
    AUTO_SEQ_MSG_TX2,
    AUTO_SEQ_MSG_TX3,
    AUTO_SEQ_MSG_TX4,
    AUTO_SEQ_MSG_TX5,
    AUTO_SEQ_MSG_TX6
};

typedef enum {
    AUTO_SEQ_OK = 0,
    AUTO_SEQ_IGNORED = 1,
    AUTO_SEQ_ERR_INVALID = -1,
    AUTO_SEQ_ERR_FULL = -2
} AutoSeqResult;

typedef struct {
    int64_t inactive_since_ms;
    int16_t offset_hz;
    int8_t snr_tx;
    int8_t snr_rx;
    uint16_t retry_counter;
    uint16_t retry_limit;
    AutoSeqState state;
    AutoSeqMessageKind last_rx_kind;
    uint8_t tx_parity;
    uint8_t flags;
    char dxcall[AUTO_SEQ_CALL_CAP];
    char dxgrid[AUTO_SEQ_GRID_CAP];
    char fd_rx_exchange[AUTO_SEQ_FD_EXCHANGE_CAP];
} QsoContext;

typedef struct {
    char callsign[AUTO_SEQ_CALL_CAP];
    char grid[AUTO_SEQ_GRID_CAP];
    uint16_t max_retry;
    uint8_t skip_tx1;
} AutoSeqConfig;

typedef struct {
    int64_t rx_slot_id;
    int16_t offset_hz;
    int8_t snr_db;
    int8_t report_db;
    AutoSeqMessageKind kind;
    uint8_t flags;
    char dxcall[AUTO_SEQ_CALL_CAP];
    char dxgrid[AUTO_SEQ_GRID_CAP];
    char fd_exchange[AUTO_SEQ_FD_EXCHANGE_CAP];
} AutoSeqRxEvent;

typedef struct {
    char dxcall[AUTO_SEQ_CALL_CAP];
    AutoSeqState state;
    AutoSeqMessageKind next_tx;
    uint16_t retry_counter;
    uint16_t retry_limit;
    uint8_t tx_parity;
    uint8_t flags;
    uint8_t active;
} AutoSeqQsoView;

typedef struct {
    QsoContext queue[AUTO_SEQ_MAX_QUEUE];
    AutoSeqConfig config;
    uint8_t active_count;
    uint8_t inactive_start;
} AutoSeq;

AutoSeqConfig auto_seq_default_config(void);
bool auto_seq_init(AutoSeq *seq, const AutoSeqConfig *config);
void auto_seq_clear(AutoSeq *seq);

bool auto_seq_set_station(AutoSeq *seq, const char *callsign, const char *grid);
void auto_seq_set_skip_tx1(AutoSeq *seq, bool enabled);
bool auto_seq_get_skip_tx1(const AutoSeq *seq);
void auto_seq_set_max_retry(AutoSeq *seq, int value);
int auto_seq_get_max_retry(const AutoSeq *seq);

AutoSeqMessageKind auto_seq_next_tx_for_state(AutoSeqState state);

AutoSeqResult auto_seq_on_manual_rx(AutoSeq *seq, const AutoSeqRxEvent *event);
AutoSeqResult auto_seq_on_addressed_rx(AutoSeq *seq, const AutoSeqRxEvent *event);

bool auto_seq_tick(AutoSeq *seq, int64_t now_ms);
bool auto_seq_drop_index(AutoSeq *seq, size_t index, int64_t now_ms);
bool auto_seq_rotate_same_parity(AutoSeq *seq);

size_t auto_seq_active_count(const AutoSeq *seq);
size_t auto_seq_inactive_count(const AutoSeq *seq);
size_t auto_seq_total_count(const AutoSeq *seq);
bool auto_seq_has_active_qso(const AutoSeq *seq);
bool auto_seq_get_active_context(const AutoSeq *seq, size_t index, QsoContext *out);
bool auto_seq_get_inactive_context(const AutoSeq *seq, size_t index, QsoContext *out);
size_t auto_seq_snapshot_active(const AutoSeq *seq, AutoSeqQsoView *out, size_t capacity);
size_t auto_seq_snapshot_inactive(const AutoSeq *seq, AutoSeqQsoView *out, size_t capacity);

#endif
