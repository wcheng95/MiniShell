#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "minishell/api.h"
#include "app_controller.h"
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

static bool model_clock_changed(const UiModel *a, const UiModel *b)
{
    if (a->utc_valid != b->utc_valid) return true;
    if (!a->utc_valid) return false;
    return a->utc_hour != b->utc_hour ||
           a->utc_minute != b->utc_minute ||
           a->utc_second != b->utc_second ||
           a->slot_counter != b->slot_counter;
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    Ft8Options options;
    AppController app;
    ft8_ui_adapter_t adapter;
    UiShell ui;
    UiModel model;
    UiModel rendered_model;
    bool app_initialized = false;
    bool adapter_initialized = false;
    bool have_rendered_model = false;
    bool redraw = true;
    bool running = true;
    int result = 0;

    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->struct_size < FIELD_END(mini_api_t, input) ||
        api->system == NULL || api->fs == NULL ||
        api->system->write == NULL) {
        return 2;
    }

    if (!parse_options(argc, argv, &options)) {
        say_console(api, "usage: ft8 [--profile desktop|adv] [--rx endpoint [--rx-slot slot]]\n");
        return 1;
    }

    if (options.rx_endpoint != NULL &&
        (api->struct_size < FIELD_END(mini_api_t, audio) || api->memory == NULL ||
         api->audio == NULL)) {
        say_system(api, "ft8: RX requires MiniShell Memory and Audio services\n");
        return 2;
    }

    if (!app_controller_init(&app, api, FT8_DATA_DIR, FT8_STATION_PATH)) {
        say_system(api, "ft8: failed to initialize storage/configuration\n");
        return 3;
    }
    app_initialized = true;

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
        if (!app_controller_start_rx(&app, &rx_config)) {
            say_system(api, "ft8: failed to start RX audio\n");
            result = 8;
            goto cleanup;
        }
    }

    ui_shell_init(&ui, options.presentation);
    memset(&rendered_model, 0, sizeof(rendered_model));

    while (running) {
        bool rx_changed = false;
        bool rx_active;
        bool has_input = false;
        UiInput input;
        UiFrame frame;
        AppAction action;

        if (!app_controller_step_rx(&app, &rx_changed)) {
            result = 9;
            break;
        }
        if (rx_changed) redraw = true;

        app_controller_build_ui_model(&app, &model);
        if (!have_rendered_model || model_clock_changed(&model, &rendered_model)) redraw = true;

        if (redraw) {
            ui_shell_render(&ui, &model, &frame);
            if (!ft8_ui_adapter_render(&adapter, &frame)) {
                result = 5;
                break;
            }
            rendered_model = model;
            have_rendered_model = true;
            redraw = false;
        }

        rx_active = app_controller_rx_active(&app);
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

        redraw = true;
        if (ui_shell_handle_input(&ui, &model, input, &action)) {
            if (!app_controller_apply_action(&app, &action)) {
                result = 7;
                break;
            }
        }
    }

cleanup:
    if (adapter_initialized) ft8_ui_adapter_shutdown(&adapter);
    if (app_initialized) app_controller_shutdown(&app);
    if (result != 0) say_system(api, "ft8: application error\n");
    return result;
}
