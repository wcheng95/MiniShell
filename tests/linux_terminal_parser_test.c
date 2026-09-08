#include <stdio.h>
#include <string.h>

#include "linux_terminal_parser.h"

#define MAX_EVENTS 32u

typedef struct {
    mini_key_event_t events[MAX_EVENTS];
    size_t count;
} event_log_t;

static mini_result_t capture_event(void *ctx, const mini_key_event_t *event)
{
    event_log_t *log = ctx;
    if (log == NULL || event == NULL || log->count >= MAX_EVENTS) {
        return MINI_ERR_NO_SPACE;
    }
    log->events[log->count++] = *event;
    return MINI_OK;
}

static int fail(const char *name, const char *message)
{
    fprintf(stderr, "%s: %s\n", name, message);
    return 1;
}

static int expect_special(const char *name, const event_log_t *log,
                          size_t index, uint32_t key)
{
    if (index >= log->count) return fail(name, "missing event");
    const mini_key_event_t *event = &log->events[index];
    if (event->type != MINI_KEY_EVENT_SPECIAL || event->key != key ||
        event->codepoint != 0u || event->modifiers != 0u) {
        return fail(name, "unexpected special event");
    }
    return 0;
}

static int expect_char(const char *name, const event_log_t *log,
                       size_t index, uint32_t codepoint)
{
    if (index >= log->count) return fail(name, "missing event");
    const mini_key_event_t *event = &log->events[index];
    if (event->type != MINI_KEY_EVENT_CHAR || event->codepoint != codepoint ||
        event->key != 0u || event->modifiers != 0u) {
        return fail(name, "unexpected character event");
    }
    return 0;
}

static int test_sequence_splits(const char *name, const unsigned char *sequence,
                                size_t length, uint32_t key)
{
    for (size_t split = 1u; split < length; ++split) {
        linux_terminal_parser_t parser;
        event_log_t log = {0};
        linux_terminal_parser_init(&parser, capture_event, &log);

        bool emitted = false;
        mini_result_t result = linux_terminal_parser_feed(&parser, sequence, split,
                                                           &emitted);
        if (result != MINI_OK) return fail(name, "prefix feed failed");
        if (emitted || log.count != 0u) return fail(name, "prefix emitted early");

        result = linux_terminal_parser_feed(&parser, sequence + split,
                                            length - split, &emitted);
        if (result != MINI_OK) return fail(name, "suffix feed failed");
        if (!emitted || log.count != 1u) return fail(name, "sequence not emitted once");
        if (expect_special(name, &log, 0u, key) != 0) return 1;
    }
    return 0;
}

static int test_all_csi_splits(void)
{
    static const struct {
        const char *name;
        const unsigned char *bytes;
        size_t length;
        uint32_t key;
    } cases[] = {
        {"up", (const unsigned char *)"\x1b[A", 3u, MINI_KEY_UP},
        {"down", (const unsigned char *)"\x1b[B", 3u, MINI_KEY_DOWN},
        {"right", (const unsigned char *)"\x1b[C", 3u, MINI_KEY_RIGHT},
        {"left", (const unsigned char *)"\x1b[D", 3u, MINI_KEY_LEFT},
        {"home", (const unsigned char *)"\x1b[H", 3u, MINI_KEY_HOME},
        {"end", (const unsigned char *)"\x1b[F", 3u, MINI_KEY_END},
        {"insert", (const unsigned char *)"\x1b[2~", 4u, MINI_KEY_INSERT},
        {"delete", (const unsigned char *)"\x1b[3~", 4u, MINI_KEY_DELETE},
        {"page-up", (const unsigned char *)"\x1b[5~", 4u, MINI_KEY_PAGE_UP},
        {"page-down", (const unsigned char *)"\x1b[6~", 4u, MINI_KEY_PAGE_DOWN},
    };

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (test_sequence_splits(cases[i].name, cases[i].bytes,
                                 cases[i].length, cases[i].key) != 0) {
            return 1;
        }
    }
    return 0;
}

static int test_utf8_splits(void)
{
    const unsigned char bytes[] = {0xE4u, 0xB8u, 0xADu}; /* U+4E2D */
    for (size_t split = 1u; split < sizeof(bytes); ++split) {
        linux_terminal_parser_t parser;
        event_log_t log = {0};
        linux_terminal_parser_init(&parser, capture_event, &log);

        bool emitted = false;
        if (linux_terminal_parser_feed(&parser, bytes, split, &emitted) != MINI_OK) {
            return fail("utf8", "prefix feed failed");
        }
        if (emitted || log.count != 0u) return fail("utf8", "prefix emitted early");
        if (linux_terminal_parser_feed(&parser, bytes + split,
                                       sizeof(bytes) - split, &emitted) != MINI_OK) {
            return fail("utf8", "suffix feed failed");
        }
        if (!emitted || log.count != 1u) return fail("utf8", "codepoint not emitted");
        if (expect_char("utf8", &log, 0u, 0x4E2Du) != 0) return 1;
    }
    return 0;
}

static int test_standalone_escape(void)
{
    linux_terminal_parser_t parser;
    event_log_t log = {0};
    linux_terminal_parser_init(&parser, capture_event, &log);

    const unsigned char escape = 0x1bu;
    bool emitted = false;
    if (linux_terminal_parser_feed(&parser, &escape, 1u, &emitted) != MINI_OK) {
        return fail("escape", "feed failed");
    }
    if (emitted || log.count != 0u ||
        !linux_terminal_parser_has_pending_escape(&parser)) {
        return fail("escape", "escape was not held pending");
    }
    if (linux_terminal_parser_flush_escape(&parser, &emitted) != MINI_OK) {
        return fail("escape", "flush failed");
    }
    if (!emitted || log.count != 1u) return fail("escape", "escape not emitted");
    return expect_special("escape", &log, 0u, MINI_KEY_ESCAPE);
}

static int test_partial_csi_flush(void)
{
    linux_terminal_parser_t parser;
    event_log_t log = {0};
    linux_terminal_parser_init(&parser, capture_event, &log);

    const unsigned char prefix[] = {0x1bu, '['};
    bool emitted = false;
    if (linux_terminal_parser_feed(&parser, prefix, sizeof(prefix), &emitted) != MINI_OK) {
        return fail("partial-csi", "feed failed");
    }
    if (linux_terminal_parser_flush_escape(&parser, &emitted) != MINI_OK) {
        return fail("partial-csi", "flush failed");
    }
    if (log.count != 2u) return fail("partial-csi", "suffix was lost");
    if (expect_special("partial-csi", &log, 0u, MINI_KEY_ESCAPE) != 0) return 1;
    return expect_char("partial-csi", &log, 1u, '[');
}

static int test_malformed_utf8_preserves_following_ascii(void)
{
    linux_terminal_parser_t parser;
    event_log_t log = {0};
    linux_terminal_parser_init(&parser, capture_event, &log);

    const unsigned char bytes[] = {0xC3u, 'x'};
    bool emitted = false;
    if (linux_terminal_parser_feed(&parser, bytes, sizeof(bytes), &emitted) != MINI_OK) {
        return fail("malformed-utf8", "feed failed");
    }
    if (!emitted || log.count != 1u) return fail("malformed-utf8", "ASCII was lost");
    return expect_char("malformed-utf8", &log, 0u, 'x');
}

static int test_reset_drops_pending(void)
{
    linux_terminal_parser_t parser;
    event_log_t log = {0};
    linux_terminal_parser_init(&parser, capture_event, &log);

    const unsigned char prefix[] = {0x1bu, '['};
    bool emitted = false;
    if (linux_terminal_parser_feed(&parser, prefix, sizeof(prefix), &emitted) != MINI_OK) {
        return fail("reset", "prefix feed failed");
    }
    linux_terminal_parser_reset(&parser);

    const unsigned char letter = 'A';
    if (linux_terminal_parser_feed(&parser, &letter, 1u, &emitted) != MINI_OK) {
        return fail("reset", "post-reset feed failed");
    }
    if (log.count != 1u) return fail("reset", "unexpected retained parser state");
    return expect_char("reset", &log, 0u, 'A');
}

int main(void)
{
    if (test_all_csi_splits() != 0) return 1;
    if (test_utf8_splits() != 0) return 1;
    if (test_standalone_escape() != 0) return 1;
    if (test_partial_csi_flush() != 0) return 1;
    if (test_malformed_utf8_preserves_following_ascii() != 0) return 1;
    if (test_reset_drops_pending() != 0) return 1;
    puts("linux_terminal_parser_unit: PASS");
    return 0;
}
