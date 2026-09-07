#include <stddef.h>

#include "minishell/api.h"
#include "app_controller.h"
#include "minishell_ui_adapter.h"
#include "ui_shell.h"

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

#define MINIFT8_DATA_DIR "/flash/MiniFT8"
#define MINIFT8_STATION_PATH "/flash/MiniFT8/Station.txt"

static void say(const mini_api_t *api, const char *text)
{
    if (api != NULL && api->system != NULL && api->system->write != NULL) {
        api->system->write(text);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->abi_version != MINISHELL_ABI_VERSION ||
        api->struct_size < FIELD_END(mini_api_t, input) ||
        api->system == NULL || api->fs == NULL ||
        api->system->write == NULL) {
        return 2;
    }

    AppController app;
    if (!app_controller_init(&app, api->fs, MINIFT8_DATA_DIR, MINIFT8_STATION_PATH)) {
        say(api, "MiniFT8: failed to initialize storage/configuration\n");
        return 3;
    }

    MiniFt8UiAdapter adapter;
    if (!minift8_ui_adapter_init(&adapter, api)) {
        say(api, "MiniFT8: requires MiniShell text Display >= 30x8 and key Input\n");
        return 4;
    }

    UiShell ui;
    ui_shell_init(&ui);

    int result = 0;
    bool running = true;
    while (running) {
        UiModel model;
        UiFrame frame;
        app_controller_build_ui_model(&app, &model);
        ui_shell_render(&ui, &model, &frame);
        if (!minift8_ui_adapter_render(&adapter, &frame)) {
            result = 5;
            break;
        }

        UiInput input;
        if (!minift8_ui_adapter_read_input(&adapter, &input)) {
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

    minift8_ui_adapter_shutdown(&adapter);
    if (result != 0) say(api, "MiniFT8: application error\n");
    return result;
}
