#include <stddef.h>
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

static bool parse_presentation(int argc, char **argv,
                               ft8_presentation_profile_t *out_profile)
{
    if (out_profile == NULL) return false;
    *out_profile = FT8_PRESENTATION_DESKTOP;

    if (argc == 1) return true;
    if (argc == 3 && strcmp(argv[1], "--profile") == 0) {
        return ft8_presentation_parse(argv[2], out_profile);
    }
    return false;
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->struct_size < FIELD_END(mini_api_t, input) ||
        api->system == NULL || api->fs == NULL ||
        api->system->write == NULL) {
        return 2;
    }

    ft8_presentation_profile_t presentation;
    if (!parse_presentation(argc, argv, &presentation)) {
        say_console(api, "usage: ft8 [--profile desktop|adv]\n");
        return 1;
    }

    AppController app;
    if (!app_controller_init(&app, api->fs, FT8_DATA_DIR, FT8_STATION_PATH)) {
        say_system(api, "ft8: failed to initialize storage/configuration\n");
        return 3;
    }

    ft8_ui_adapter_t adapter;
    if (!ft8_ui_adapter_init(&adapter, api, presentation)) {
        say_system(api, "ft8: presentation does not fit MiniShell Display/Input\n");
        return 4;
    }

    UiShell ui;
    ui_shell_init(&ui, presentation);

    int result = 0;
    bool running = true;
    while (running) {
        UiModel model;
        UiFrame frame;
        app_controller_build_ui_model(&app, &model);
        ui_shell_render(&ui, &model, &frame);
        if (!ft8_ui_adapter_render(&adapter, &frame)) {
            result = 5;
            break;
        }

        UiInput input;
        if (!ft8_ui_adapter_read_input(&adapter, &input)) {
            result = 6;
            break;
        }
        if (input.type == UI_INPUT_QUIT) {
            running = false;
            continue;
        }

        AppAction action;
        if (ui_shell_handle_input(&ui, &model, input, &action)) {
            if (!app_controller_apply_action(&app, &action)) {
                result = 7;
                break;
            }
        }
    }

    ft8_ui_adapter_shutdown(&adapter);
    if (result != 0) say_system(api, "ft8: application error\n");
    return result;
}
