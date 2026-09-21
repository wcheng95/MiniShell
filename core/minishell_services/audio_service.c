#include <string.h>

#include "services_internal.h"

typedef struct {
    bool open;
    bool started;
    mini_audio_stream_t public_handle;
    minishell_backend_audio_t backend_handle;
} audio_stream_state_t;

static bool s_available;
static mini_audio_rx_api_t s_rx_api;
static mini_audio_tx_api_t s_tx_api;
static mini_audio_api_t s_audio_api;
static audio_stream_state_t s_rx;
static audio_stream_state_t s_tx;
static mini_audio_tone_api_t s_tone_api;
static mini_audio_tone_t s_tone, s_tone_backend;
static mini_audio_stream_t s_next_handle = 1u;

static mini_result_t validate_format(const mini_audio_format_t *format)
{
    const uint32_t v0_size = MINI_FIELD_END(mini_audio_format_t, channels);
    if (format == NULL || format->struct_size < v0_size) return MINI_ERR_INVALID;
    if (format->sample_rate_hz == 0u || format->sample_format == 0u ||
        format->channels == 0u) {
        return MINI_ERR_INVALID;
    }
    return MINI_OK;
}

static mini_audio_stream_t allocate_public_handle(void)
{
    for (;;) {
        mini_audio_stream_t handle = s_next_handle++;
        if (s_next_handle == MINI_AUDIO_STREAM_INVALID) s_next_handle = 1u;
        if (handle == MINI_AUDIO_STREAM_INVALID) continue;
        if (s_rx.open && handle == s_rx.public_handle) continue;
        if (s_tx.open && handle == s_tx.public_handle) continue;
        if (handle == s_tone) continue;
        return handle;
    }
}

static bool handle_matches(const audio_stream_state_t *state,
                           mini_audio_stream_t stream)
{
    return state->open && stream != MINI_AUDIO_STREAM_INVALID &&
           state->public_handle == stream;
}

static void clear_state(audio_stream_state_t *state)
{
    memset(state, 0, sizeof(*state));
}

static mini_result_t rx_open(const char *endpoint, const mini_audio_format_t *format,
                             mini_audio_stream_t *out_stream)
{
    const minishell_services_port_t *port = minishell_services_port();
    if ((s_audio_api.capabilities & MINI_AUDIO_CAP_RX) == 0u) return MINI_ERR_UNSUPPORTED;
    if (out_stream == NULL) return MINI_ERR_INVALID;
    *out_stream = MINI_AUDIO_STREAM_INVALID;
    if (endpoint != NULL && endpoint[0] == '\0') return MINI_ERR_INVALID;
    mini_result_t result = validate_format(format);
    if (result != MINI_OK) return result;
    if (s_rx.open) return MINI_ERR_TOO_MANY_OPEN;

    minishell_backend_audio_t backend = MINISHELL_BACKEND_AUDIO_INVALID;
    result = port->audio_rx_open(port->ctx, endpoint, format->sample_rate_hz,
                                 format->sample_format, format->channels, &backend);
    if (result != MINI_OK) return result;
    if (backend == MINISHELL_BACKEND_AUDIO_INVALID) return MINI_ERR_IO;

    s_rx.open = true;
    s_rx.started = false;
    s_rx.backend_handle = backend;
    s_rx.public_handle = allocate_public_handle();
    *out_stream = s_rx.public_handle;
    return MINI_OK;
}

static mini_result_t rx_start(mini_audio_stream_t stream)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!handle_matches(&s_rx, stream)) return MINI_ERR_BAD_HANDLE;
    if (s_rx.started) return MINI_OK;
    mini_result_t result = port->audio_rx_start(port->ctx, s_rx.backend_handle);
    if (result == MINI_OK) s_rx.started = true;
    return result;
}

static mini_result_t rx_read(mini_audio_stream_t stream, void *frames,
                             uint32_t frame_capacity, uint32_t *out_frames,
                             uint32_t timeout_ms)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!handle_matches(&s_rx, stream)) return MINI_ERR_BAD_HANDLE;
    if (out_frames == NULL) return MINI_ERR_INVALID;
    *out_frames = 0u;
    if (!s_rx.started) return MINI_ERR_NOT_READY;
    if (frame_capacity == 0u) return MINI_OK;
    if (frames == NULL) return MINI_ERR_INVALID;

    mini_result_t result = port->audio_rx_read(port->ctx, s_rx.backend_handle,
                                               frames, frame_capacity, out_frames,
                                               timeout_ms);
    if (result == MINI_ERR_DISCONTINUITY) *out_frames = 0u;
    if (result == MINI_OK && *out_frames > frame_capacity) {
        *out_frames = 0u;
        return MINI_ERR_IO;
    }
    return result;
}

static mini_result_t rx_stop(mini_audio_stream_t stream)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!handle_matches(&s_rx, stream)) return MINI_ERR_BAD_HANDLE;
    if (!s_rx.started) return MINI_OK;
    mini_result_t result = port->audio_rx_stop(port->ctx, s_rx.backend_handle);
    if (result == MINI_OK) s_rx.started = false;
    return result;
}

static mini_result_t rx_close(mini_audio_stream_t stream)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!handle_matches(&s_rx, stream)) return MINI_ERR_BAD_HANDLE;

    mini_result_t result = MINI_OK;
    if (s_rx.started) {
        result = port->audio_rx_stop(port->ctx, s_rx.backend_handle);
        if (result != MINI_OK) return result;
        s_rx.started = false;
    }

    result = port->audio_rx_close(port->ctx, s_rx.backend_handle);
    if (result == MINI_OK) clear_state(&s_rx);
    return result;
}

static mini_result_t tx_open(const char *endpoint, const mini_audio_format_t *format,
                             mini_audio_stream_t *out_stream)
{
    const minishell_services_port_t *port = minishell_services_port();
    if ((s_audio_api.capabilities & MINI_AUDIO_CAP_TX) == 0u) return MINI_ERR_UNSUPPORTED;
    if (out_stream == NULL) return MINI_ERR_INVALID;
    *out_stream = MINI_AUDIO_STREAM_INVALID;
    if (endpoint != NULL && endpoint[0] == '\0') return MINI_ERR_INVALID;
    mini_result_t result = validate_format(format);
    if (result != MINI_OK) return result;
    if (s_tx.open || s_tone) return MINI_ERR_TOO_MANY_OPEN;

    minishell_backend_audio_t backend = MINISHELL_BACKEND_AUDIO_INVALID;
    result = port->audio_tx_open(port->ctx, endpoint, format->sample_rate_hz,
                                 format->sample_format, format->channels, &backend);
    if (result != MINI_OK) return result;
    if (backend == MINISHELL_BACKEND_AUDIO_INVALID) return MINI_ERR_IO;

    s_tx.open = true;
    s_tx.started = false;
    s_tx.backend_handle = backend;
    s_tx.public_handle = allocate_public_handle();
    *out_stream = s_tx.public_handle;
    return MINI_OK;
}

static mini_result_t tx_start(mini_audio_stream_t stream)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!handle_matches(&s_tx, stream)) return MINI_ERR_BAD_HANDLE;
    if (s_tx.started) return MINI_OK;
    mini_result_t result = port->audio_tx_start(port->ctx, s_tx.backend_handle);
    if (result == MINI_OK) s_tx.started = true;
    return result;
}

static mini_result_t tx_write(mini_audio_stream_t stream, const void *frames,
                              uint32_t frame_count, uint32_t *out_frames,
                              uint32_t timeout_ms)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!handle_matches(&s_tx, stream)) return MINI_ERR_BAD_HANDLE;
    if (out_frames == NULL) return MINI_ERR_INVALID;
    *out_frames = 0u;
    if (!s_tx.started) return MINI_ERR_NOT_READY;
    if (frame_count == 0u) return MINI_OK;
    if (frames == NULL) return MINI_ERR_INVALID;

    mini_result_t result = port->audio_tx_write(port->ctx, s_tx.backend_handle,
                                                frames, frame_count, out_frames,
                                                timeout_ms);
    if (result == MINI_OK && *out_frames > frame_count) {
        *out_frames = 0u;
        return MINI_ERR_IO;
    }
    return result;
}

static mini_result_t tx_stop(mini_audio_stream_t stream)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!handle_matches(&s_tx, stream)) return MINI_ERR_BAD_HANDLE;
    if (!s_tx.started) return MINI_OK;
    mini_result_t result = port->audio_tx_stop(port->ctx, s_tx.backend_handle);
    if (result == MINI_OK) s_tx.started = false;
    return result;
}

static mini_result_t tx_abort(mini_audio_stream_t stream)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!handle_matches(&s_tx, stream)) return MINI_ERR_BAD_HANDLE;
    if (!s_tx.started) return MINI_OK;
    mini_result_t result = port->audio_tx_abort(port->ctx, s_tx.backend_handle);
    if (result == MINI_OK) s_tx.started = false;
    return result;
}

static mini_result_t tx_close(mini_audio_stream_t stream)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!handle_matches(&s_tx, stream)) return MINI_ERR_BAD_HANDLE;

    mini_result_t result = MINI_OK;
    if (s_tx.started) {
        result = port->audio_tx_stop(port->ctx, s_tx.backend_handle);
        if (result != MINI_OK) {
            (void)port->audio_tx_abort(port->ctx, s_tx.backend_handle);
            return result;
        }
        s_tx.started = false;
    }

    result = port->audio_tx_close(port->ctx, s_tx.backend_handle);
    if (result == MINI_OK) clear_state(&s_tx);
    return result;
}

static mini_result_t tone_config_valid(const mini_audio_tone_config_t *config)
{
    if (!config || config->struct_size < MINI_FIELD_END(mini_audio_tone_config_t, volume) ||
        config->pitch_hz < 300 || config->pitch_hz > 999 || config->volume > 99) return MINI_ERR_INVALID;
    return MINI_OK;
}
static const mini_audio_tone_api_t *tone_provider(void) { return minishell_services_port()->audio_tone; }
static mini_result_t tone_open(const mini_audio_tone_config_t *config, mini_audio_tone_t *out)
{
    if (!out) return MINI_ERR_INVALID;
    *out = MINI_AUDIO_TONE_INVALID;
    mini_result_t result = tone_config_valid(config);
    if (result != MINI_OK) return result;
    if (!(s_audio_api.capabilities & MINI_AUDIO_CAP_TONE)) return MINI_ERR_UNSUPPORTED;
    if (s_tone || s_tx.open) return MINI_ERR_TOO_MANY_OPEN;
    result = tone_provider()->open(config, &s_tone_backend);
    if (result != MINI_OK) return result;
    if (!s_tone_backend) return MINI_ERR_IO;
    s_tone = allocate_public_handle();
    *out = s_tone;
    return MINI_OK;
}
static mini_result_t tone_configure(mini_audio_tone_t h, const mini_audio_tone_config_t *config)
{
    if (!h || h != s_tone) return MINI_ERR_BAD_HANDLE;
    mini_result_t result = tone_config_valid(config);
    return result == MINI_OK ? tone_provider()->configure(s_tone_backend, config) : result;
}
static mini_result_t tone_enqueue(mini_audio_tone_t h, uint32_t ms)
{
    if (!h || h != s_tone) return MINI_ERR_BAD_HANDLE;
    if (!ms || ms > 60000) return MINI_ERR_INVALID;
    return tone_provider()->enqueue(s_tone_backend, ms);
}
static mini_result_t tone_hold(mini_audio_tone_t h, uint32_t active)
{
    if (!h || h != s_tone) return MINI_ERR_BAD_HANDLE;
    if (active > 1) return MINI_ERR_INVALID;
    return tone_provider()->hold(s_tone_backend, active);
}
static mini_result_t tone_stop(mini_audio_tone_t h)
{
    if (!h || h != s_tone) return MINI_ERR_BAD_HANDLE;
    return tone_provider()->stop(s_tone_backend);
}
static mini_result_t tone_busy(mini_audio_tone_t h, uint32_t *out)
{
    if (!h || h != s_tone) return MINI_ERR_BAD_HANDLE;
    if (!out) return MINI_ERR_INVALID;
    *out = 0;
    return tone_provider()->busy(s_tone_backend, out);
}
static mini_result_t tone_close(mini_audio_tone_t h)
{
    if (!h || h != s_tone) return MINI_ERR_BAD_HANDLE;
    mini_result_t result = tone_provider()->close(s_tone_backend);
    s_tone = s_tone_backend = MINI_AUDIO_TONE_INVALID;
    return result;
}

void minishell_audio_service_configure(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    clear_state(&s_rx);
    clear_state(&s_tx);

    s_rx_api.struct_size = sizeof(s_rx_api);
    s_rx_api.open = rx_open;
    s_rx_api.start = rx_start;
    s_rx_api.read = rx_read;
    s_rx_api.stop = rx_stop;
    s_rx_api.close = rx_close;

    s_tx_api.struct_size = sizeof(s_tx_api);
    s_tx_api.open = tx_open;
    s_tx_api.start = tx_start;
    s_tx_api.write = tx_write;
    s_tx_api.stop = tx_stop;
    s_tx_api.abort = tx_abort;
    s_tx_api.close = tx_close;

    s_audio_api.struct_size = sizeof(s_audio_api);
    s_audio_api.capabilities = 0u;
    s_audio_api.rx = NULL;
    s_audio_api.tx = NULL;
    s_available = false;
    s_audio_api.tone = NULL;
    s_tone = s_tone_backend = MINI_AUDIO_TONE_INVALID;
    s_tone_api = (mini_audio_tone_api_t){sizeof(s_tone_api), tone_open, tone_configure,
        tone_enqueue, tone_hold, tone_stop, tone_busy, tone_close};
    const mini_audio_tone_api_t *tone = port->audio_tone;
    if ((port->audio_capabilities & MINI_AUDIO_CAP_TONE) && tone &&
        tone->struct_size >= MINI_FIELD_END(mini_audio_tone_api_t, close) &&
        tone->open && tone->configure && tone->enqueue && tone->hold && tone->stop && tone->busy && tone->close) {
        s_available = true;
        s_audio_api.capabilities |= MINI_AUDIO_CAP_TONE;
        s_audio_api.tone = &s_tone_api;
    }

    bool rx_ready = (port->audio_capabilities & MINI_AUDIO_CAP_RX) != 0u &&
                    port->audio_rx_open != NULL && port->audio_rx_start != NULL &&
                    port->audio_rx_read != NULL && port->audio_rx_stop != NULL &&
                    port->audio_rx_close != NULL;
    if (rx_ready) {
        s_audio_api.capabilities |= MINI_AUDIO_CAP_RX;
        s_audio_api.rx = &s_rx_api;
        s_available = true;
    }

    bool tx_ready = (port->audio_capabilities & MINI_AUDIO_CAP_TX) != 0u &&
                    port->audio_tx_open != NULL && port->audio_tx_start != NULL &&
                    port->audio_tx_write != NULL && port->audio_tx_stop != NULL &&
                    port->audio_tx_abort != NULL && port->audio_tx_close != NULL;
    if (tx_ready) {
        s_audio_api.capabilities |= MINI_AUDIO_CAP_TX;
        s_audio_api.tx = &s_tx_api;
        s_available = true;
    }
}

void minishell_audio_service_app_begin(void)
{
    if (s_tone) (void)tone_close(s_tone);
}

void minishell_audio_service_app_end(void)
{
    if (s_tone) (void)tone_close(s_tone);
    const minishell_services_port_t *port = minishell_services_port();

    if (s_rx.open) {
        if (s_rx.started && port->audio_rx_stop != NULL) {
            (void)port->audio_rx_stop(port->ctx, s_rx.backend_handle);
        }
        if (port->audio_rx_close != NULL) {
            (void)port->audio_rx_close(port->ctx, s_rx.backend_handle);
        }
        clear_state(&s_rx);
    }

    if (s_tx.open) {
        if (s_tx.started && port->audio_tx_abort != NULL) {
            (void)port->audio_tx_abort(port->ctx, s_tx.backend_handle);
        }
        if (port->audio_tx_close != NULL) {
            (void)port->audio_tx_close(port->ctx, s_tx.backend_handle);
        }
        clear_state(&s_tx);
    }
}

bool minishell_audio_service_available(void) { return s_available; }
const mini_audio_api_t *minishell_audio_service_api(void) { return &s_audio_api; }
