#include "auto_seq.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

_Static_assert(AUTO_SEQ_MAX_QUEUE <= UINT8_MAX, "AutoSeq queue indices must fit in uint8_t");
_Static_assert(sizeof(QsoContext) <= 64u, "QsoContext exceeded AS-2 compactness budget");
_Static_assert(sizeof(AutoSeq) <= 2048u, "AutoSeq exceeded AS-2 embedded-storage budget");

static uint16_t clamp_retry(int value)
{
    if (value <= 0) return 0u;
    if ((unsigned)value > UINT16_MAX) return UINT16_MAX;
    return (uint16_t)value;
}

static bool copy_upper_checked(char *out, size_t out_size, const char *text)
{
    size_t len;
    size_t i;

    if (out == NULL || out_size == 0u || text == NULL) return false;
    len = strlen(text);
    if (len >= out_size) return false;
    for (i = 0u; i < len; ++i)
        out[i] = (char)toupper((unsigned char)text[i]);
    out[len] = '\0';
    return true;
}

static bool normalize_call(char out[AUTO_SEQ_CALL_CAP], const char *text)
{
    const char *begin;
    const char *end;
    size_t len;
    size_t i;

    if (out == NULL || text == NULL) return false;
    begin = text;
    end = text + strlen(text);
    if (begin < end && *begin == '<') ++begin;
    if (begin < end && end[-1] == '>') --end;
    len = (size_t)(end - begin);
    if (len >= AUTO_SEQ_CALL_CAP) return false;
    for (i = 0u; i < len; ++i)
        out[i] = (char)toupper((unsigned char)begin[i]);
    out[len] = '\0';
    return true;
}

static bool event_valid(const AutoSeqRxEvent *event)
{
    char normalized[AUTO_SEQ_CALL_CAP];

    if (event == NULL ||
        event->kind < AUTO_SEQ_MSG_TX1 || event->kind > AUTO_SEQ_MSG_TX5 ||
        !normalize_call(normalized, event->dxcall) || normalized[0] == '\0') {
        return false;
    }
    if (strlen(event->dxgrid) >= AUTO_SEQ_GRID_CAP ||
        strlen(event->fd_exchange) >= AUTO_SEQ_FD_EXCHANGE_CAP) {
        return false;
    }
    return true;
}

static void context_reset(QsoContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = AUTO_SEQ_STATE_IDLE;
    ctx->snr_tx = AUTO_SEQ_SNR_UNKNOWN;
    ctx->snr_rx = AUTO_SEQ_SNR_UNKNOWN;
    ctx->offset_hz = 1500;
}

static void set_state(QsoContext *ctx, AutoSeqState state, uint16_t retry_limit)
{
    ctx->state = state;
    ctx->retry_counter = 0u;
    ctx->retry_limit = retry_limit;
}

static bool context_is_freetext(const QsoContext *ctx)
{
    return (ctx->flags & AUTO_SEQ_FLAG_FREETEXT) != 0u;
}

static bool context_has_exchanged(const QsoContext *ctx)
{
    return ctx != NULL && ctx->dxcall[0] != '\0' &&
           strcmp(ctx->dxcall, "CQ") != 0 && ctx->state > AUTO_SEQ_STATE_REPLYING;
}

static void remove_active(AutoSeq *seq, size_t index)
{
    size_t i;
    if (seq == NULL || index >= seq->active_count) return;
    for (i = index; i + 1u < seq->active_count; ++i)
        seq->queue[i] = seq->queue[i + 1u];
    --seq->active_count;
}

static void evict_oldest_inactive(AutoSeq *seq)
{
    size_t oldest;
    int64_t oldest_time;
    size_t i;

    if (seq == NULL || seq->inactive_start >= AUTO_SEQ_MAX_QUEUE) return;

    oldest = seq->inactive_start;
    oldest_time = seq->queue[oldest].inactive_since_ms;
    for (i = (size_t)seq->inactive_start + 1u; i < AUTO_SEQ_MAX_QUEUE; ++i) {
        int64_t time = seq->queue[i].inactive_since_ms;
        if (time < oldest_time || (time == oldest_time && i > oldest)) {
            oldest = i;
            oldest_time = time;
        }
    }

    for (i = oldest; i > seq->inactive_start; --i)
        seq->queue[i] = seq->queue[i - 1u];
    ++seq->inactive_start;
}

static QsoContext *append_context(AutoSeq *seq)
{
    QsoContext *ctx;

    if (seq == NULL) return NULL;
    if (seq->inactive_start <= seq->active_count) {
        evict_oldest_inactive(seq);
        if (seq->inactive_start <= seq->active_count) return NULL;
    }

    ctx = &seq->queue[seq->active_count++];
    context_reset(ctx);
    return ctx;
}

static void move_to_inactive(AutoSeq *seq, size_t index, int64_t now_ms)
{
    QsoContext saved;

    if (seq == NULL || index >= seq->active_count) return;

    if (seq->inactive_start <= seq->active_count) {
        evict_oldest_inactive(seq);
        if (seq->inactive_start <= seq->active_count) {
            remove_active(seq, index);
            return;
        }
    }

    saved = seq->queue[index];
    saved.inactive_since_ms = now_ms;
    remove_active(seq, index);
    seq->queue[--seq->inactive_start] = saved;
}

static int find_active(const AutoSeq *seq, const char *dxcall)
{
    size_t i;
    for (i = 0u; i < seq->active_count; ++i) {
        if (strcmp(seq->queue[i].dxcall, dxcall) == 0) return (int)i;
    }
    return -1;
}

static int find_inactive(const AutoSeq *seq, const char *dxcall)
{
    size_t i;
    for (i = seq->inactive_start; i < AUTO_SEQ_MAX_QUEUE; ++i) {
        if (strcmp(seq->queue[i].dxcall, dxcall) == 0) return (int)i;
    }
    return -1;
}

static QsoContext *reactivate(AutoSeq *seq, size_t index)
{
    QsoContext saved;
    size_t i;

    if (seq == NULL || index < seq->inactive_start || index >= AUTO_SEQ_MAX_QUEUE)
        return NULL;

    saved = seq->queue[index];
    for (i = index; i > seq->inactive_start; --i)
        seq->queue[i] = seq->queue[i - 1u];
    ++seq->inactive_start;

    saved.retry_counter = 0u;
    saved.inactive_since_ms = 0;
    seq->queue[seq->active_count++] = saved;
    return &seq->queue[seq->active_count - 1u];
}

static bool comes_before(const QsoContext *left, const QsoContext *right)
{
    bool left_ft;
    bool right_ft;

    if (left->state == AUTO_SEQ_STATE_IDLE && right->state != AUTO_SEQ_STATE_IDLE)
        return true;
    if (right->state == AUTO_SEQ_STATE_IDLE && left->state != AUTO_SEQ_STATE_IDLE)
        return false;

    left_ft = context_is_freetext(left);
    right_ft = context_is_freetext(right);
    if (left_ft != right_ft) return left_ft;

    if (left->state == right->state)
        return left->retry_counter < right->retry_counter;

    return left->state > right->state;
}

static void sort_and_clean(AutoSeq *seq)
{
    size_t i;

    if (seq == NULL || seq->active_count == 0u) return;

    /* Stable insertion sort: V2 priority relation, fixed bounded queue. */
    for (i = 1u; i < seq->active_count; ++i) {
        QsoContext saved = seq->queue[i];
        size_t j = i;
        while (j > 0u && comes_before(&saved, &seq->queue[j - 1u])) {
            seq->queue[j] = seq->queue[j - 1u];
            --j;
        }
        seq->queue[j] = saved;
    }

    while (seq->active_count > 0u && seq->queue[0].state == AUTO_SEQ_STATE_IDLE)
        remove_active(seq, 0u);
}

static bool report_is_valid(int8_t report)
{
    return report >= -30 && report <= 30;
}

static void copy_event_facts(QsoContext *ctx, const AutoSeqRxEvent *event)
{
    if (ctx->dxgrid[0] == '\0' && event->dxgrid[0] != '\0')
        (void)copy_upper_checked(ctx->dxgrid, sizeof(ctx->dxgrid), event->dxgrid);

    if (event->fd_exchange[0] != '\0') {
        (void)copy_upper_checked(ctx->fd_rx_exchange,
                                 sizeof(ctx->fd_rx_exchange), event->fd_exchange);
        ctx->flags |= AUTO_SEQ_FLAG_FD;
    }
    if ((event->flags & AUTO_SEQ_RX_FLAG_FD) != 0u)
        ctx->flags |= AUTO_SEQ_FLAG_FD;

    if ((event->kind == AUTO_SEQ_MSG_TX2 || event->kind == AUTO_SEQ_MSG_TX3) &&
        report_is_valid(event->report_db)) {
        ctx->snr_rx = event->report_db;
    }
    if ((event->kind == AUTO_SEQ_MSG_TX1 || event->kind == AUTO_SEQ_MSG_TX2) &&
        ctx->snr_tx == AUTO_SEQ_SNR_UNKNOWN) {
        ctx->snr_tx = event->snr_db;
    }
    ctx->last_rx_kind = event->kind;
}

static void initial_state_from_received(QsoContext *ctx, AutoSeqMessageKind kind)
{
    switch (kind) {
    case AUTO_SEQ_MSG_TX1:
        set_state(ctx, AUTO_SEQ_STATE_CALLING, 0u);
        break;
    case AUTO_SEQ_MSG_TX2:
        set_state(ctx, AUTO_SEQ_STATE_REPLYING, 0u);
        break;
    case AUTO_SEQ_MSG_TX3:
        set_state(ctx, AUTO_SEQ_STATE_REPORT, 0u);
        break;
    case AUTO_SEQ_MSG_TX4:
        set_state(ctx, AUTO_SEQ_STATE_ROGER_REPORT, 0u);
        break;
    case AUTO_SEQ_MSG_TX5:
        set_state(ctx, AUTO_SEQ_STATE_ROGERS, 0u);
        break;
    default:
        set_state(ctx, AUTO_SEQ_STATE_IDLE, 0u);
        break;
    }
}

static bool apply_response(AutoSeq *seq, QsoContext *ctx,
                           const AutoSeqRxEvent *event, bool override)
{
    char dxcall[AUTO_SEQ_CALL_CAP];
    AutoSeqMessageKind received;

    if (!normalize_call(dxcall, event->dxcall)) return false;
    received = event->kind;
    copy_event_facts(ctx, event);

    if (override) {
        (void)memcpy(ctx->dxcall, dxcall, sizeof(ctx->dxcall));
        ctx->offset_hz = event->offset_hz;
        ctx->tx_parity = (uint8_t)(((uint64_t)event->rx_slot_id ^ 1u) & 1u);
        initial_state_from_received(ctx, received);
    }

    switch (ctx->state) {
    case AUTO_SEQ_STATE_CALLING:
        switch (received) {
        case AUTO_SEQ_MSG_TX1:
            set_state(ctx, AUTO_SEQ_STATE_REPORT, seq->config.max_retry);
            return true;
        case AUTO_SEQ_MSG_TX2:
            set_state(ctx, AUTO_SEQ_STATE_ROGER_REPORT, seq->config.max_retry);
            return true;
        case AUTO_SEQ_MSG_TX3:
            set_state(ctx, AUTO_SEQ_STATE_ROGERS, seq->config.max_retry);
            return true;
        case AUTO_SEQ_MSG_TX5:
            set_state(ctx, AUTO_SEQ_STATE_IDLE, 0u);
            return false;
        default:
            return false;
        }

    case AUTO_SEQ_STATE_REPLYING:
        switch (received) {
        case AUTO_SEQ_MSG_TX2:
            set_state(ctx, AUTO_SEQ_STATE_ROGER_REPORT, seq->config.max_retry);
            return true;
        case AUTO_SEQ_MSG_TX3:
            set_state(ctx, AUTO_SEQ_STATE_ROGERS, seq->config.max_retry);
            return true;
        case AUTO_SEQ_MSG_TX4:
            set_state(ctx, AUTO_SEQ_STATE_SIGNOFF, 0u);
            ctx->flags |= AUTO_SEQ_FLAG_PARK_AFTER_SIGNOFF;
            return true;
        case AUTO_SEQ_MSG_TX5:
            set_state(ctx, AUTO_SEQ_STATE_IDLE, 0u);
            return false;
        default:
            return false;
        }

    case AUTO_SEQ_STATE_REPORT:
        switch (received) {
        case AUTO_SEQ_MSG_TX2:
            set_state(ctx, AUTO_SEQ_STATE_ROGER_REPORT, seq->config.max_retry);
            return true;
        case AUTO_SEQ_MSG_TX3:
            set_state(ctx, AUTO_SEQ_STATE_ROGERS, seq->config.max_retry);
            return true;
        case AUTO_SEQ_MSG_TX4:
            set_state(ctx, AUTO_SEQ_STATE_SIGNOFF, 0u);
            ctx->flags |= AUTO_SEQ_FLAG_PARK_AFTER_SIGNOFF;
            return true;
        case AUTO_SEQ_MSG_TX5:
            set_state(ctx, AUTO_SEQ_STATE_IDLE, 0u);
            return false;
        default:
            return false;
        }

    case AUTO_SEQ_STATE_ROGER_REPORT:
        switch (received) {
        case AUTO_SEQ_MSG_TX4:
            set_state(ctx, AUTO_SEQ_STATE_SIGNOFF, seq->config.max_retry);
            ctx->flags |= AUTO_SEQ_FLAG_PARK_AFTER_SIGNOFF;
            return true;
        case AUTO_SEQ_MSG_TX5:
            set_state(ctx, AUTO_SEQ_STATE_IDLE, 0u);
            return false;
        default:
            return false;
        }

    case AUTO_SEQ_STATE_ROGERS:
        switch (received) {
        case AUTO_SEQ_MSG_TX3:
            set_state(ctx, AUTO_SEQ_STATE_ROGERS, seq->config.max_retry);
            return true;
        case AUTO_SEQ_MSG_TX4:
        case AUTO_SEQ_MSG_TX5:
            set_state(ctx, AUTO_SEQ_STATE_IDLE, 0u);
            return false;
        default:
            return false;
        }

    case AUTO_SEQ_STATE_SIGNOFF:
        switch (received) {
        case AUTO_SEQ_MSG_TX4:
            set_state(ctx, AUTO_SEQ_STATE_SIGNOFF, 0u);
            ctx->flags |= AUTO_SEQ_FLAG_PARK_AFTER_SIGNOFF;
            return true;
        case AUTO_SEQ_MSG_TX5:
            set_state(ctx, AUTO_SEQ_STATE_IDLE, 0u);
            ctx->flags &= (uint8_t)~AUTO_SEQ_FLAG_PARK_AFTER_SIGNOFF;
            return false;
        default:
            return false;
        }

    default:
        return false;
    }
}

AutoSeqConfig auto_seq_default_config(void)
{
    AutoSeqConfig config;
    memset(&config, 0, sizeof(config));
    config.max_retry = AUTO_SEQ_DEFAULT_MAX_RETRY;
    return config;
}

bool auto_seq_init(AutoSeq *seq, const AutoSeqConfig *config)
{
    AutoSeqConfig local;

    if (seq == NULL) return false;
    local = config != NULL ? *config : auto_seq_default_config();
    memset(seq, 0, sizeof(*seq));
    seq->inactive_start = AUTO_SEQ_MAX_QUEUE;
    seq->config.max_retry = local.max_retry;
    seq->config.skip_tx1 = local.skip_tx1 ? 1u : 0u;
    if (!auto_seq_set_station(seq, local.callsign, local.grid)) {
        memset(seq, 0, sizeof(*seq));
        return false;
    }
    return true;
}

void auto_seq_clear(AutoSeq *seq)
{
    AutoSeqConfig config;
    if (seq == NULL) return;
    config = seq->config;
    memset(seq->queue, 0, sizeof(seq->queue));
    seq->active_count = 0u;
    seq->inactive_start = AUTO_SEQ_MAX_QUEUE;
    seq->config = config;
}

bool auto_seq_set_station(AutoSeq *seq, const char *callsign, const char *grid)
{
    char normalized_call[AUTO_SEQ_CALL_CAP];
    char normalized_grid[AUTO_SEQ_GRID_CAP];

    if (seq == NULL || callsign == NULL || grid == NULL ||
        !normalize_call(normalized_call, callsign) ||
        !copy_upper_checked(normalized_grid, sizeof(normalized_grid), grid)) {
        return false;
    }
    memcpy(seq->config.callsign, normalized_call, sizeof(seq->config.callsign));
    memcpy(seq->config.grid, normalized_grid, sizeof(seq->config.grid));
    return true;
}

void auto_seq_set_skip_tx1(AutoSeq *seq, bool enabled)
{
    if (seq != NULL) seq->config.skip_tx1 = enabled ? 1u : 0u;
}

bool auto_seq_get_skip_tx1(const AutoSeq *seq)
{
    return seq != NULL && seq->config.skip_tx1 != 0u;
}

void auto_seq_set_max_retry(AutoSeq *seq, int value)
{
    uint16_t retry;
    size_t i;

    if (seq == NULL) return;
    retry = clamp_retry(value);
    seq->config.max_retry = retry;
    for (i = 0u; i < seq->active_count; ++i) {
        if (seq->queue[i].retry_limit > 0u) {
            seq->queue[i].retry_limit = retry;
            if (seq->queue[i].retry_counter > retry)
                seq->queue[i].retry_counter = retry;
        }
    }
}

int auto_seq_get_max_retry(const AutoSeq *seq)
{
    return seq != NULL ? (int)seq->config.max_retry : 0;
}

AutoSeqMessageKind auto_seq_next_tx_for_state(AutoSeqState state)
{
    switch (state) {
    case AUTO_SEQ_STATE_REPLYING: return AUTO_SEQ_MSG_TX1;
    case AUTO_SEQ_STATE_REPORT: return AUTO_SEQ_MSG_TX2;
    case AUTO_SEQ_STATE_ROGER_REPORT: return AUTO_SEQ_MSG_TX3;
    case AUTO_SEQ_STATE_ROGERS: return AUTO_SEQ_MSG_TX4;
    case AUTO_SEQ_STATE_SIGNOFF: return AUTO_SEQ_MSG_TX5;
    default: return AUTO_SEQ_MSG_NONE;
    }
}

AutoSeqResult auto_seq_on_manual_rx(AutoSeq *seq, const AutoSeqRxEvent *event)
{
    QsoContext *ctx;
    char dxcall[AUTO_SEQ_CALL_CAP];

    if (seq == NULL || !event_valid(event) || !normalize_call(dxcall, event->dxcall))
        return AUTO_SEQ_ERR_INVALID;

    ctx = append_context(seq);
    if (ctx == NULL) return AUTO_SEQ_ERR_FULL;

    if ((event->flags & AUTO_SEQ_RX_FLAG_TO_ME) != 0u) {
        (void)apply_response(seq, ctx, event, true);
    } else {
        memcpy(ctx->dxcall, dxcall, sizeof(ctx->dxcall));
        if (event->dxgrid[0] != '\0')
            (void)copy_upper_checked(ctx->dxgrid, sizeof(ctx->dxgrid), event->dxgrid);
        if (event->fd_exchange[0] != '\0')
            (void)copy_upper_checked(ctx->fd_rx_exchange,
                                     sizeof(ctx->fd_rx_exchange), event->fd_exchange);
        ctx->snr_tx = event->snr_db;
        ctx->offset_hz = event->offset_hz;
        ctx->tx_parity = (uint8_t)(((uint64_t)event->rx_slot_id ^ 1u) & 1u);
        ctx->last_rx_kind = event->kind;
        if ((event->flags & AUTO_SEQ_RX_FLAG_FD) != 0u)
            ctx->flags |= AUTO_SEQ_FLAG_FD;
        set_state(ctx,
                  seq->config.skip_tx1 ? AUTO_SEQ_STATE_REPORT : AUTO_SEQ_STATE_REPLYING,
                  seq->config.max_retry);
    }

    sort_and_clean(seq);
    return AUTO_SEQ_OK;
}

AutoSeqResult auto_seq_on_addressed_rx(AutoSeq *seq, const AutoSeqRxEvent *event)
{
    char dxcall[AUTO_SEQ_CALL_CAP];
    int index;
    QsoContext *ctx;

    if (seq == NULL || !event_valid(event) || !normalize_call(dxcall, event->dxcall))
        return AUTO_SEQ_ERR_INVALID;
    if ((event->flags & AUTO_SEQ_RX_FLAG_TO_ME) == 0u)
        return AUTO_SEQ_IGNORED;

    index = find_active(seq, dxcall);
    if (index >= 0) {
        ctx = &seq->queue[index];
        ctx->tx_parity = (uint8_t)(((uint64_t)event->rx_slot_id ^ 1u) & 1u);
        (void)apply_response(seq, ctx, event, false);
        sort_and_clean(seq);
        return AUTO_SEQ_OK;
    }

    index = find_inactive(seq, dxcall);
    if (index >= 0) {
        ctx = reactivate(seq, (size_t)index);
        if (ctx == NULL) return AUTO_SEQ_ERR_INVALID;
        ctx->tx_parity = (uint8_t)(((uint64_t)event->rx_slot_id ^ 1u) & 1u);
        (void)apply_response(seq, ctx, event, false);
        sort_and_clean(seq);
        return AUTO_SEQ_OK;
    }

    /* V2 reincarnation guards: unknown mid-QSO/signoff messages are ignored. */
    if (event->kind == AUTO_SEQ_MSG_TX3 ||
        event->kind == AUTO_SEQ_MSG_TX4 ||
        event->kind == AUTO_SEQ_MSG_TX5) {
        return AUTO_SEQ_IGNORED;
    }

    ctx = append_context(seq);
    if (ctx == NULL) return AUTO_SEQ_ERR_FULL;
    (void)apply_response(seq, ctx, event, true);
    sort_and_clean(seq);
    return AUTO_SEQ_OK;
}

bool auto_seq_tick(AutoSeq *seq, int64_t now_ms)
{
    QsoContext *ctx;

    if (seq == NULL || seq->active_count == 0u) return false;
    ctx = &seq->queue[0];

    switch (ctx->state) {
    case AUTO_SEQ_STATE_REPLYING:
    case AUTO_SEQ_STATE_REPORT:
    case AUTO_SEQ_STATE_ROGER_REPORT:
    case AUTO_SEQ_STATE_ROGERS:
        if (ctx->retry_counter < ctx->retry_limit) {
            ++ctx->retry_counter;
        } else if (context_has_exchanged(ctx)) {
            move_to_inactive(seq, 0u, now_ms);
        } else {
            remove_active(seq, 0u);
        }
        return true;

    case AUTO_SEQ_STATE_CALLING:
        remove_active(seq, 0u);
        return true;

    case AUTO_SEQ_STATE_SIGNOFF:
        if ((ctx->flags & AUTO_SEQ_FLAG_PARK_AFTER_SIGNOFF) != 0u)
            move_to_inactive(seq, 0u, now_ms);
        else
            remove_active(seq, 0u);
        return true;

    case AUTO_SEQ_STATE_IDLE:
        remove_active(seq, 0u);
        return true;

    default:
        return false;
    }
}

bool auto_seq_drop_index(AutoSeq *seq, size_t index, int64_t now_ms)
{
    if (seq == NULL || index >= seq->active_count) return false;
    if (seq->queue[index].state == AUTO_SEQ_STATE_CALLING ||
        strcmp(seq->queue[index].dxcall, "CQ") == 0) {
        remove_active(seq, index);
    } else {
        move_to_inactive(seq, index, now_ms);
    }
    return true;
}

bool auto_seq_rotate_same_parity(AutoSeq *seq)
{
    uint8_t parity;
    size_t last = 0u;
    size_t i;
    QsoContext head;

    if (seq == NULL || seq->active_count < 2u) return false;
    parity = seq->queue[0].tx_parity & 1u;
    for (i = 1u; i < seq->active_count; ++i) {
        if ((seq->queue[i].tx_parity & 1u) != parity) break;
        last = i;
    }
    if (last == 0u) return false;

    head = seq->queue[0];
    for (i = 0u; i < last; ++i)
        seq->queue[i] = seq->queue[i + 1u];
    seq->queue[last] = head;
    return true;
}

size_t auto_seq_active_count(const AutoSeq *seq)
{
    return seq != NULL ? seq->active_count : 0u;
}

size_t auto_seq_inactive_count(const AutoSeq *seq)
{
    return seq != NULL ? AUTO_SEQ_MAX_QUEUE - seq->inactive_start : 0u;
}

size_t auto_seq_total_count(const AutoSeq *seq)
{
    return auto_seq_active_count(seq) + auto_seq_inactive_count(seq);
}

bool auto_seq_has_active_qso(const AutoSeq *seq)
{
    size_t i;
    if (seq == NULL) return false;
    for (i = 0u; i < seq->active_count; ++i) {
        if (seq->queue[i].state != AUTO_SEQ_STATE_IDLE &&
            seq->queue[i].state != AUTO_SEQ_STATE_CALLING) {
            return true;
        }
    }
    return false;
}

bool auto_seq_get_active_context(const AutoSeq *seq, size_t index, QsoContext *out)
{
    if (seq == NULL || out == NULL || index >= seq->active_count) return false;
    *out = seq->queue[index];
    return true;
}

bool auto_seq_get_inactive_context(const AutoSeq *seq, size_t index, QsoContext *out)
{
    size_t absolute;
    if (seq == NULL || out == NULL || index >= auto_seq_inactive_count(seq)) return false;
    absolute = (size_t)seq->inactive_start + index;
    *out = seq->queue[absolute];
    return true;
}

static void make_view(const QsoContext *ctx, bool active, AutoSeqQsoView *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->dxcall, ctx->dxcall, sizeof(out->dxcall));
    out->state = ctx->state;
    out->next_tx = auto_seq_next_tx_for_state(ctx->state);
    out->retry_counter = ctx->retry_counter;
    out->retry_limit = ctx->retry_limit;
    out->tx_parity = ctx->tx_parity;
    out->flags = ctx->flags;
    out->active = active ? 1u : 0u;
}

size_t auto_seq_snapshot_active(const AutoSeq *seq, AutoSeqQsoView *out, size_t capacity)
{
    size_t count;
    size_t i;
    if (seq == NULL || (capacity > 0u && out == NULL)) return 0u;
    count = seq->active_count < capacity ? seq->active_count : capacity;
    for (i = 0u; i < count; ++i)
        make_view(&seq->queue[i], true, &out[i]);
    return count;
}

size_t auto_seq_snapshot_inactive(const AutoSeq *seq, AutoSeqQsoView *out, size_t capacity)
{
    size_t available;
    size_t count;
    size_t i;
    if (seq == NULL || (capacity > 0u && out == NULL)) return 0u;
    available = auto_seq_inactive_count(seq);
    count = available < capacity ? available : capacity;
    for (i = 0u; i < count; ++i)
        make_view(&seq->queue[(size_t)seq->inactive_start + i], false, &out[i]);
    return count;
}
