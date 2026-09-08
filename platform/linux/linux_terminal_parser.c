#include <string.h>

#include "linux_terminal_parser.h"

static mini_result_t emit_event(linux_terminal_parser_t *parser,
                                uint32_t type,
                                uint32_t codepoint,
                                uint32_t key,
                                uint32_t modifiers,
                                bool *out_emitted)
{
    mini_key_event_t event = {
        .struct_size = sizeof(event),
        .type = type,
        .codepoint = codepoint,
        .key = key,
        .modifiers = modifiers,
    };
    mini_result_t result = parser->emit(parser->emit_ctx, &event);
    if (result == MINI_OK && out_emitted != NULL) *out_emitted = true;
    return result;
}

static size_t utf8_expected(unsigned char first)
{
    if (first < 0x80u) return 1u;
    if ((first & 0xE0u) == 0xC0u) return 2u;
    if ((first & 0xF0u) == 0xE0u) return 3u;
    if ((first & 0xF8u) == 0xF0u) return 4u;
    return 0u;
}

static bool decode_utf8_complete(const unsigned char *bytes,
                                 size_t count,
                                 uint32_t *out_codepoint)
{
    size_t needed = utf8_expected(bytes[0]);
    if (needed == 0u || count != needed || out_codepoint == NULL) return false;

    if (needed == 1u) {
        *out_codepoint = bytes[0];
        return true;
    }

    uint32_t codepoint = needed == 2u ? (uint32_t)(bytes[0] & 0x1Fu)
                         : needed == 3u ? (uint32_t)(bytes[0] & 0x0Fu)
                                        : (uint32_t)(bytes[0] & 0x07u);
    for (size_t i = 1u; i < needed; ++i) {
        if ((bytes[i] & 0xC0u) != 0x80u) return false;
        codepoint = (codepoint << 6) | (uint32_t)(bytes[i] & 0x3Fu);
    }

    if ((needed == 2u && codepoint < 0x80u) ||
        (needed == 3u && codepoint < 0x800u) ||
        (needed == 4u && codepoint < 0x10000u) ||
        codepoint > 0x10FFFFu ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
        return false;
    }

    *out_codepoint = codepoint;
    return true;
}

static uint32_t csi_key(const unsigned char *bytes, size_t count)
{
    if (count == 3u && bytes[0] == 0x1bu && bytes[1] == '[') {
        switch (bytes[2]) {
            case 'A': return MINI_KEY_UP;
            case 'B': return MINI_KEY_DOWN;
            case 'C': return MINI_KEY_RIGHT;
            case 'D': return MINI_KEY_LEFT;
            case 'H': return MINI_KEY_HOME;
            case 'F': return MINI_KEY_END;
            default: return 0u;
        }
    }
    if (count == 4u && bytes[0] == 0x1bu && bytes[1] == '[' && bytes[3] == '~') {
        switch (bytes[2]) {
            case '2': return MINI_KEY_INSERT;
            case '3': return MINI_KEY_DELETE;
            case '5': return MINI_KEY_PAGE_UP;
            case '6': return MINI_KEY_PAGE_DOWN;
            default: return 0u;
        }
    }
    return 0u;
}

static bool csi_prefix(const unsigned char *bytes, size_t count)
{
    if (count == 0u || bytes[0] != 0x1bu) return false;
    if (count == 1u) return true;
    if (bytes[1] != '[') return false;
    if (count == 2u) return true;
    if (count == 3u) {
        return bytes[2] == '2' || bytes[2] == '3' ||
               bytes[2] == '5' || bytes[2] == '6';
    }
    return false;
}

static mini_result_t process_byte(linux_terminal_parser_t *parser,
                                  unsigned char ch,
                                  bool *out_emitted);

static mini_result_t emit_escape_and_replay(linux_terminal_parser_t *parser,
                                            const unsigned char *replay,
                                            size_t replay_count,
                                            bool *out_emitted)
{
    parser->pending_count = 0u;
    parser->escape_pending = false;

    mini_result_t result = emit_event(parser, MINI_KEY_EVENT_SPECIAL, 0u,
                                      MINI_KEY_ESCAPE, 0u, out_emitted);
    if (result != MINI_OK) return result;

    for (size_t i = 0u; i < replay_count; ++i) {
        result = process_byte(parser, replay[i], out_emitted);
        if (result != MINI_OK) return result;
    }
    return MINI_OK;
}

static mini_result_t process_escape_byte(linux_terminal_parser_t *parser,
                                         unsigned char ch,
                                         bool *out_emitted)
{
    if (parser->pending_count >= sizeof(parser->pending)) {
        return emit_escape_and_replay(parser, NULL, 0u, out_emitted);
    }

    parser->pending[parser->pending_count++] = ch;

    uint32_t key = csi_key(parser->pending, parser->pending_count);
    if (key != 0u) {
        parser->pending_count = 0u;
        parser->escape_pending = false;
        return emit_event(parser, MINI_KEY_EVENT_SPECIAL, 0u, key, 0u,
                          out_emitted);
    }

    if (csi_prefix(parser->pending, parser->pending_count)) {
        parser->escape_pending = true;
        return MINI_OK;
    }

    unsigned char replay[sizeof(parser->pending)];
    size_t replay_count = parser->pending_count - 1u;
    if (replay_count > 0u) memcpy(replay, &parser->pending[1], replay_count);
    return emit_escape_and_replay(parser, replay, replay_count, out_emitted);
}

static mini_result_t process_utf8_byte(linux_terminal_parser_t *parser,
                                       unsigned char ch,
                                       bool *out_emitted)
{
    size_t needed = utf8_expected(parser->pending[0]);
    if (needed < 2u || needed > sizeof(parser->pending)) {
        parser->pending_count = 0u;
        return process_byte(parser, ch, out_emitted);
    }

    if ((ch & 0xC0u) != 0x80u) {
        parser->pending_count = 0u;
        return process_byte(parser, ch, out_emitted);
    }

    parser->pending[parser->pending_count++] = ch;
    if (parser->pending_count < needed) return MINI_OK;

    uint32_t codepoint = 0u;
    bool valid = decode_utf8_complete(parser->pending, parser->pending_count,
                                      &codepoint);
    parser->pending_count = 0u;
    if (!valid) return MINI_OK;

    return emit_event(parser, MINI_KEY_EVENT_CHAR, codepoint, 0u, 0u,
                      out_emitted);
}

static mini_result_t process_byte(linux_terminal_parser_t *parser,
                                  unsigned char ch,
                                  bool *out_emitted)
{
    if (parser->pending_count > 0u) {
        if (parser->pending[0] == 0x1bu) {
            return process_escape_byte(parser, ch, out_emitted);
        }
        return process_utf8_byte(parser, ch, out_emitted);
    }

    if (ch == 0x1bu) {
        parser->pending[0] = ch;
        parser->pending_count = 1u;
        parser->escape_pending = true;
        return MINI_OK;
    }
    if (ch == '\r' || ch == '\n') {
        return emit_event(parser, MINI_KEY_EVENT_SPECIAL, 0u, MINI_KEY_ENTER, 0u,
                          out_emitted);
    }
    if (ch == '\t') {
        return emit_event(parser, MINI_KEY_EVENT_SPECIAL, 0u, MINI_KEY_TAB, 0u,
                          out_emitted);
    }
    if (ch == 0x08u || ch == 0x7fu) {
        return emit_event(parser, MINI_KEY_EVENT_SPECIAL, 0u, MINI_KEY_BACKSPACE, 0u,
                          out_emitted);
    }
    if (ch >= 1u && ch <= 26u) {
        return emit_event(parser, MINI_KEY_EVENT_CHAR,
                          (uint32_t)('a' + ch - 1u), 0u, MINI_MOD_CTRL,
                          out_emitted);
    }
    if (ch >= 0x20u && ch < 0x7fu) {
        return emit_event(parser, MINI_KEY_EVENT_CHAR, ch, 0u, 0u, out_emitted);
    }
    if (ch >= 0x80u) {
        size_t needed = utf8_expected(ch);
        if (needed > 1u) {
            parser->pending[0] = ch;
            parser->pending_count = 1u;
        }
    }
    return MINI_OK;
}

void linux_terminal_parser_init(linux_terminal_parser_t *parser,
                                linux_terminal_emit_fn emit,
                                void *emit_ctx)
{
    if (parser == NULL) return;
    memset(parser, 0, sizeof(*parser));
    parser->emit = emit;
    parser->emit_ctx = emit_ctx;
}

void linux_terminal_parser_reset(linux_terminal_parser_t *parser)
{
    if (parser == NULL) return;
    parser->pending_count = 0u;
    parser->escape_pending = false;
}

mini_result_t linux_terminal_parser_feed(linux_terminal_parser_t *parser,
                                         const unsigned char *bytes,
                                         size_t count,
                                         bool *out_emitted)
{
    if (parser == NULL || parser->emit == NULL ||
        (count > 0u && bytes == NULL)) return MINI_ERR_INVALID;
    if (out_emitted != NULL) *out_emitted = false;

    for (size_t i = 0u; i < count; ++i) {
        mini_result_t result = process_byte(parser, bytes[i], out_emitted);
        if (result != MINI_OK) return result;
    }
    return MINI_OK;
}

mini_result_t linux_terminal_parser_flush_escape(linux_terminal_parser_t *parser,
                                                 bool *out_emitted)
{
    if (parser == NULL || parser->emit == NULL) return MINI_ERR_INVALID;
    if (out_emitted != NULL) *out_emitted = false;
    if (!parser->escape_pending) return MINI_OK;

    unsigned char replay[sizeof(parser->pending)];
    size_t replay_count = parser->pending_count > 0u
                              ? parser->pending_count - 1u : 0u;
    if (replay_count > 0u) memcpy(replay, &parser->pending[1], replay_count);
    return emit_escape_and_replay(parser, replay, replay_count, out_emitted);
}

bool linux_terminal_parser_has_pending_escape(const linux_terminal_parser_t *parser)
{
    return parser != NULL && parser->escape_pending;
}
