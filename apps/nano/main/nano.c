#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "minishell/api.h"
#include "nano_buffer.h"
#include "nano_file.h"
#include "nano_ui.h"

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

#define NANO_STATUS_MAX 128u
#define NANO_SEARCH_MAX 63u

typedef struct {
    const mini_memory_api_t *memory;
} nano_memory_context_t;

static void say(const mini_system_api_t *system, const char *text)
{
    system->write(text);
}

static void *memory_alloc_adapter(void *ctx, uint32_t size)
{
    nano_memory_context_t *memory_ctx = (nano_memory_context_t *)ctx;
    void *ptr = NULL;
    return memory_ctx->memory->alloc(size, &ptr) == MINI_OK ? ptr : NULL;
}

static void *memory_realloc_adapter(void *ctx, void *ptr, uint32_t size)
{
    nano_memory_context_t *memory_ctx = (nano_memory_context_t *)ctx;
    void *new_ptr = NULL;
    return memory_ctx->memory->realloc(ptr, size, &new_ptr) == MINI_OK ? new_ptr : NULL;
}

static void memory_free_adapter(void *ctx, void *ptr)
{
    nano_memory_context_t *memory_ctx = (nano_memory_context_t *)ctx;
    (void)memory_ctx->memory->free(ptr);
}

static bool is_ctrl_char(const mini_key_event_t *event, char lower)
{
    if (event->type != MINI_KEY_EVENT_CHAR ||
        (event->modifiers & MINI_MOD_CTRL) == 0u) {
        return false;
    }
    return event->codepoint == (uint32_t)lower ||
           event->codepoint == (uint32_t)(lower - 'a' + 'A');
}

static bool read_event(const mini_key_input_api_t *key, mini_key_event_t *event)
{
    memset(event, 0, sizeof(*event));
    event->struct_size = sizeof(*event);
    return key->read(event, MINI_WAIT_FOREVER) == MINI_OK;
}

static void status_file_error(char *status, size_t size, const char *prefix, int result)
{
    (void)snprintf(status, size, "%s: %s", prefix, nano_file_result_text(result));
}

static bool save_buffer(const mini_fs_api_t *fs,
                        const char *path,
                        nano_buffer_t *buffer,
                        char *status,
                        size_t status_size)
{
    int result = nano_file_save(fs, path, buffer);
    if (result != NANO_FILE_OK) {
        status_file_error(status, status_size, "Save failed", result);
        return false;
    }
    (void)snprintf(status, status_size, "Wrote %u bytes", (unsigned)buffer->length);
    return true;
}

static void search_prompt(nano_ui_t *ui,
                          nano_buffer_t *buffer,
                          const mini_key_input_api_t *key,
                          const char *path,
                          char *status,
                          size_t status_size)
{
    char query[NANO_SEARCH_MAX + 1u];
    uint32_t length = 0u;
    query[0] = '\0';

    for (;;) {
        (void)snprintf(status, status_size, "Search: %s", query);
        if (!nano_ui_render(ui, buffer, path, status)) return;

        mini_key_event_t event;
        if (!read_event(key, &event)) continue;

        if ((event.type == MINI_KEY_EVENT_SPECIAL && event.key == MINI_KEY_ESCAPE) ||
            is_ctrl_char(&event, 'c')) {
            (void)snprintf(status, status_size, "Search cancelled");
            return;
        }

        if (event.type == MINI_KEY_EVENT_SPECIAL && event.key == MINI_KEY_ENTER) {
            if (length == 0u) {
                (void)snprintf(status, status_size, "Empty search");
                return;
            }
            uint32_t position = 0u;
            if (nano_buffer_search_forward(buffer, query, length, &position)) {
                nano_buffer_set_cursor(buffer, position);
                (void)snprintf(status, status_size, "Found: %s", query);
            } else {
                (void)snprintf(status, status_size, "Not found: %s", query);
            }
            return;
        }

        if (event.type == MINI_KEY_EVENT_SPECIAL && event.key == MINI_KEY_BACKSPACE) {
            if (length > 0u) query[--length] = '\0';
            continue;
        }

        if (event.type == MINI_KEY_EVENT_CHAR &&
            (event.modifiers & (MINI_MOD_CTRL | MINI_MOD_ALT)) == 0u &&
            event.codepoint >= 0x20u && event.codepoint <= 0x7eu) {
            if (length < NANO_SEARCH_MAX) {
                query[length++] = (char)event.codepoint;
                query[length] = '\0';
            }
        }
    }
}

static bool confirm_exit(nano_ui_t *ui,
                         nano_buffer_t *buffer,
                         const mini_fs_api_t *fs,
                         const mini_key_input_api_t *key,
                         const char *path,
                         char *status,
                         size_t status_size)
{
    if (!buffer->dirty) return true;

    (void)snprintf(status, status_size, "Save modified buffer?  Y Yes  N No  C Cancel");
    if (!nano_ui_render(ui, buffer, path, status)) return false;

    for (;;) {
        mini_key_event_t event;
        if (!read_event(key, &event)) continue;

        if (event.type == MINI_KEY_EVENT_SPECIAL && event.key == MINI_KEY_ESCAPE) {
            (void)snprintf(status, status_size, "Exit cancelled");
            return false;
        }
        if (event.type != MINI_KEY_EVENT_CHAR ||
            (event.modifiers & (MINI_MOD_CTRL | MINI_MOD_ALT)) != 0u) {
            continue;
        }

        if (event.codepoint == 'y' || event.codepoint == 'Y') {
            return save_buffer(fs, path, buffer, status, status_size);
        }
        if (event.codepoint == 'n' || event.codepoint == 'N') return true;
        if (event.codepoint == 'c' || event.codepoint == 'C') {
            (void)snprintf(status, status_size, "Exit cancelled");
            return false;
        }
    }
}

static void handle_special(nano_buffer_t *buffer,
                           const mini_key_event_t *event,
                           uint32_t page_rows,
                           char *status,
                           size_t status_size)
{
    switch (event->key) {
    case MINI_KEY_LEFT: nano_buffer_move_left(buffer); break;
    case MINI_KEY_RIGHT: nano_buffer_move_right(buffer); break;
    case MINI_KEY_UP: nano_buffer_move_up(buffer); break;
    case MINI_KEY_DOWN: nano_buffer_move_down(buffer); break;
    case MINI_KEY_HOME: nano_buffer_move_home(buffer); break;
    case MINI_KEY_END: nano_buffer_move_end(buffer); break;
    case MINI_KEY_PAGE_UP: nano_buffer_page_up(buffer, page_rows); break;
    case MINI_KEY_PAGE_DOWN: nano_buffer_page_down(buffer, page_rows); break;
    case MINI_KEY_ENTER:
        if (!nano_buffer_insert_char(buffer, '\n')) {
            (void)snprintf(status, status_size, "Buffer full or out of memory");
        }
        break;
    case MINI_KEY_TAB:
        if (!nano_buffer_insert_spaces(buffer, 4u)) {
            (void)snprintf(status, status_size, "Buffer full or out of memory");
        }
        break;
    case MINI_KEY_BACKSPACE:
        (void)nano_buffer_backspace(buffer);
        break;
    case MINI_KEY_DELETE:
        (void)nano_buffer_delete(buffer);
        break;
    default:
        break;
    }
}

static bool api_ready(const mini_api_t *api)
{
    if (api == NULL || api->abi_version != MINISHELL_ABI_VERSION ||
        api->struct_size < FIELD_END(mini_api_t, input) ||
        api->system == NULL || api->memory == NULL || api->fs == NULL ||
        api->display == NULL || api->input == NULL) {
        return false;
    }

    if (api->system->struct_size < FIELD_END(mini_system_api_t, write) ||
        api->system->write == NULL) return false;

    if (api->memory->struct_size < FIELD_END(mini_memory_api_t, free) ||
        api->memory->alloc == NULL || api->memory->realloc == NULL || api->memory->free == NULL) {
        return false;
    }

    if (api->fs->struct_size < FIELD_END(mini_fs_api_t, stat) ||
        api->fs->open == NULL || api->fs->close == NULL || api->fs->read == NULL ||
        api->fs->write == NULL || api->fs->sync == NULL || api->fs->stat == NULL) {
        return false;
    }

    if (api->display->struct_size < FIELD_END(mini_display_api_t, present) ||
        (api->display->capabilities & MINI_DISPLAY_CAP_TEXT) == 0u ||
        api->display->text == NULL || api->display->present == NULL) {
        return false;
    }
    if (api->display->text->struct_size < FIELD_END(mini_text_display_api_t, write_at) ||
        api->display->text->get_info == NULL || api->display->text->clear == NULL ||
        api->display->text->write_at == NULL) {
        return false;
    }

    if (api->input->struct_size < FIELD_END(mini_input_api_t, key) ||
        (api->input->capabilities & MINI_INPUT_CAP_KEY) == 0u || api->input->key == NULL ||
        api->input->key->struct_size < FIELD_END(mini_key_input_api_t, read) ||
        api->input->key->read == NULL) {
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    if (!api_ready(api)) return 2;

    if (argc != 2 || argv[1] == NULL || argv[1][0] != '/') {
        say(api->system, "usage: nano <absolute-path>\n");
        return 1;
    }

    nano_memory_context_t memory_context = {
        .memory = api->memory,
    };
    const nano_allocator_t allocator = {
        .ctx = &memory_context,
        .alloc = memory_alloc_adapter,
        .realloc = memory_realloc_adapter,
        .free = memory_free_adapter,
    };

    nano_buffer_t buffer;
    if (!nano_buffer_init(&buffer, &allocator)) {
        say(api->system, "nano: buffer initialization failed\n");
        return 3;
    }

    int load_result = nano_file_load(api->memory, api->fs, argv[1], &buffer);
    if (load_result < 0) {
        say(api->system, "nano: cannot open file: ");
        say(api->system, nano_file_result_text(load_result));
        say(api->system, "\n");
        nano_buffer_destroy(&buffer);
        return 4;
    }

    nano_ui_t ui;
    if (!nano_ui_init(&ui, api->display)) {
        say(api->system, "nano: text display is unavailable or too small\n");
        nano_buffer_destroy(&buffer);
        return 5;
    }

    char status[NANO_STATUS_MAX];
    status[0] = '\0';
    if (load_result == NANO_FILE_NEW) {
        (void)snprintf(status, sizeof(status), "New file");
    }

    bool running = true;
    while (running) {
        if (!nano_ui_render(&ui, &buffer, argv[1], status)) {
            nano_ui_clear(&ui);
            nano_buffer_destroy(&buffer);
            say(api->system, "nano: display error\n");
            return 6;
        }
        status[0] = '\0';

        mini_key_event_t event;
        if (!read_event(api->input->key, &event)) continue;

        if (is_ctrl_char(&event, 'o')) {
            (void)save_buffer(api->fs, argv[1], &buffer, status, sizeof(status));
            continue;
        }
        if (is_ctrl_char(&event, 'w')) {
            search_prompt(&ui, &buffer, api->input->key, argv[1], status, sizeof(status));
            continue;
        }
        if (is_ctrl_char(&event, 'x')) {
            if (confirm_exit(&ui, &buffer, api->fs, api->input->key,
                             argv[1], status, sizeof(status))) {
                running = false;
            }
            continue;
        }

        if (event.type == MINI_KEY_EVENT_SPECIAL) {
            handle_special(&buffer, &event, nano_ui_body_rows(&ui), status, sizeof(status));
            continue;
        }

        if (event.type == MINI_KEY_EVENT_CHAR &&
            (event.modifiers & (MINI_MOD_CTRL | MINI_MOD_ALT)) == 0u &&
            event.codepoint >= 0x20u && event.codepoint <= 0x7eu) {
            if (!nano_buffer_insert_char(&buffer, (char)event.codepoint)) {
                (void)snprintf(status, sizeof(status), "Buffer full or out of memory");
            }
        }
    }

    nano_ui_clear(&ui);
    nano_buffer_destroy(&buffer);
    return 0;
}
