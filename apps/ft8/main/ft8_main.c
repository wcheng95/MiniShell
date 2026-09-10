#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "minishell/api.h"
#include "app_controller.h"
#include "app_controller_tx.h"
#include "ft8_ui_adapter.h"
#include "presentation_profile.h"
#include "ui_shell.h"

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

#define FT8_DATA_DIR "/flash/ft8"
#define FT8_STATION_PATH "/flash/ft8/station.txt"

#ifndef FT8_DEFAULT_PRESENTATION
#define FT8_DEFAULT_PRESENTATION FT8_PRESENTATION_DESKTOP
#endif

typedef struct {
    ft8_presentation_profile_t presentation;
    const char *rx_endpoint;
    bool has_rx_slot;
    int64_t rx_slot_id;
} Ft8Options;

static void say_system(const mini_api_t *api, const char *text)
{
    if (api != NULL && api->system != NULL && api->system->write != NULL) {
        api->system->write(text);
    }
}

static void say_console(const mini_api_t *api, const char *text)
{
    if (api != NULL && api->console != NULL && api->console->write != NULL) {
        api->console->write(text);
    } else {
        say_system(api, text);
    }
}

static bool parse_i64(const char *text, int64_t *out_value)
{
    char *end = NULL;
    long long value;

    if (text == NULL || out_value == NULL || text[0] == '\0') return false;
    errno = 0;
    value = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return false;
    *out_value = (int64_t)value;
    return true;
}

static bool parse_options(int argc, char **argv, Ft8Options *out)
{
    int i;
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    out->presentation = FT8_DEFAULT_PRESENTATION;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--profile") == 0) {
            if (++i >= argc || !ft8_presentation_parse(argv[i], &out->presentation)) return false;
        } else if (strcmp(argv[i], "--rx") == 0) {
            if (++i >= argc || argv[i][0] == '\0' || out->rx_endpoint != NULL) return false;
            out->rx_endpoint = argv[i];
        } else if (strcmp(argv[i], "--rx-slot") == 0) {
            if (++i >= argc || out->has_rx_slot || !parse_i64(argv[i], &out->rx_slot_id)) return false;
            out->has_rx_slot = true;
        } else {
            return false;
        }
    }

    return !out->has_rx_slot || out->rx_endpoint != NULL;
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    Ft8Options options;
    AppController *app = NULL;
    ft8_ui_adapter_t adapter;
    UiShell ui;
    UiModel model;
    UiFrame rendered_frame;
    bool adapter_initialized = false;
    bool have_rendered_frame = false;
    bool running = true;
    int result = 0;

    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->struct_size < FIELD_END(mini_api_t, input) ||
        api->system == NULL || api->fs == NULL || api->memory == NULL ||
        api->system->write == NULL || api->memory->alloc == NULL ||
        api->memory->free == NULL) {
        return 2;
    }

    if (!parse_options(argc, argv, &options)) {
        say_console(api, "usage: ft8 [--profile desktop|adv] [--rx endpoint [--rx-slot slot]]\n");
        return 1;
    }

    if (options.rx_endpoint != NULL &&
        (api->struct_size < FIELD_END(mini_api_t, audio) || api->audio == NULL)) {
        say_system(api, "ft8: RX requires MiniShell Audio service\n");
        return 2;
    }

    app = app_controller_create(api, FT8_DATA_DIR, FT8_STATION_PATH);
    if (app == NULL) {
        say_system(api, "ft8: failed to initialize storage/configuration\n");
        return 3;
    }

    if (!ft8_ui_adapter_init(&adapter, api, options.presentation)) {
        say_system(api, "ft8: presentation does not fit MiniShell Display/Input\n");
        result = 4;
        goto cleanup;
    }
    adapter_initialized = true;

    if (options.rx_endpoint != NULL) {
        AppRxStartConfig rx_config = {
            .endpoint = options.rx_endpoint,
            .has_explicit_timing = options.has_rx_slot,
            .slot_id = options.rx_slot_id,
            .sample_offset = 0u,
        };
        if (!app_controller_start_rx(app, &rx_config)) {
            say_system(api, "ft8: failed to start RX audio\n");
            result = 8;
            goto cleanup;
        }
    }

    ui_shell_init(&ui, options.presentation);
    memset(&rendered_frame, 0, sizeof(rendered_frame));

    while (running) {
        bool step_changed = false;
        bool rx_active;
        bool has_input = false;
        UiInput input;
        UiFrame frame;
        AppAction action;

        if (!app_controller_step_rx(app, &step_changed)) {
            result = 9;
            break;
        }

        /*
         * --rx-slot is the deterministic decode-fixture mode. Do not mix its
         * synthetic RX slot identity with the host's wall-clock TX lifecycle.
         * Live operation (including --rx without --rx-slot) uses MiniShell UTC.
         */
        if (!options.has_rx_slot) {
            if (!app_controller_step_tx(app, &step_changed)) {
                result = 10;
                break;
            }
        }

        /*
         * C2 boundary: app_controller produces one complete model and ui_shell
         * alone decides which state is visible. Lifecycle code compares only
         * rendered frames, never UIScreen/submenu or individual model fields.
         */
        app_controller_build_model(app, &model);
        ui_shell_render(&ui, &model, &frame);
        if (!have_rendered_frame ||
            memcmp(&frame, &rendered_frame, sizeof(frame)) != 0) {
            if (!ft8_ui_adapter_render(&adapter, &frame)) {
                result = 5;
                break;
            }
            rendered_frame = frame;
            have_rendered_frame = true;
        }

        rx_active = app_controller_rx_active(app);
        if (!ft8_ui_adapter_read_input_timeout(&adapter,
                                               rx_active ? MINI_WAIT_NONE : 100u,
                                               &input, &has_input)) {
            result = 6;
            break;
        }
        if (!has_input) continue;

        if (input.type == UI_INPUT_QUIT) {
            running = false;
            continue;
        }

        if (ui_shell_handle_input(&ui, &model, input, &action)) {
            if (!app_controller_apply_action(app, &action)) {
                result = 7;
                break;
            }
        }
    }

cleanup:
    if (adapter_initialized) ft8_ui_adapter_shutdown(&adapter);
    app_controller_destroy(app);
    if (result != 0) say_system(api, "ft8: application error\n");
    return result;
}
