#include <errno.h>
#include <math.h>
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

#ifndef FT8_DEFAULT_RX_ENDPOINT
#define FT8_DEFAULT_RX_ENDPOINT NULL
#endif

#ifndef FT8_PLATFORM_DECODE_WORKER_START
#define FT8_PLATFORM_DECODE_WORKER_START(app_) (true)
#define FT8_PLATFORM_DECODE_WORKER_STOP(app_) ((void)(app_))
#endif

typedef struct {
    ft8_presentation_profile_t presentation;
    const char *rx_endpoint;
    const char *cat_endpoint;
    bool has_cat_test_tone, has_cat_test_ms;
    float cat_test_tone;
    uint32_t cat_test_ms;
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
        } else if (strcmp(argv[i], "--cat") == 0) {
            if (++i >= argc || !argv[i][0] || argv[i][0] == '-' || out->cat_endpoint) return false;
            out->cat_endpoint = argv[i];
        } else if (strcmp(argv[i], "--cat-test-tone") == 0) {
            if (++i >= argc || out->has_cat_test_tone) return false;
            char *end;
            errno = 0;
            float tone = strtof(argv[i], &end);
            if (errno || end == argv[i] || *end || !isfinite(tone) || tone < 300.0f || tone > 2700.0f)
                return false;
            out->cat_test_tone = tone;
            out->has_cat_test_tone = true;
        } else if (strcmp(argv[i], "--cat-test-ms") == 0) {
            int64_t duration;
            if (++i >= argc || out->has_cat_test_ms || !parse_i64(argv[i], &duration) ||
                duration < 100 || duration > 2000) return false;
            out->cat_test_ms = (uint32_t)duration;
            out->has_cat_test_ms = true;
        } else {
            return false;
        }
    }

    if (out->has_cat_test_tone || out->has_cat_test_ms) {
        if (!out->has_cat_test_tone || !out->has_cat_test_ms || !out->cat_endpoint ||
            out->rx_endpoint || out->has_rx_slot) return false;
        return true;
    }
    /* Fixture timing requires an explicit source, never an implicit live default. */
    if (out->has_rx_slot && out->rx_endpoint == NULL) return false;
    if (out->rx_endpoint == NULL) out->rx_endpoint = FT8_DEFAULT_RX_ENDPOINT;
    return true;
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
    bool decode_worker_started = false;
    bool have_rendered_frame = false;
    bool running = true;
    bool cat_ready = false, cat_attempted = false, startup_complete = false;
    uint64_t last_cat_attempt_us = 0;
    int result = 0;

    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->struct_size < FIELD_END(mini_api_t, input) ||
        api->system == NULL || api->fs == NULL || api->memory == NULL ||
        api->system->write == NULL || api->memory->alloc == NULL ||
        api->memory->free == NULL) {
        return 2;
    }

    if (!parse_options(argc, argv, &options)) {
        say_console(api, "usage: ft8 [--profile desktop|adv] [--rx endpoint [--rx-slot slot]] [--cat endpoint]\n"
                         "       ft8 --cat endpoint --cat-test-tone 300..2700 --cat-test-ms 100..2000\n");
        return 1;
    }

    if (options.has_cat_test_tone) {
        mini_result_t tested = app_controller_cat_test(api, FT8_STATION_PATH, options.cat_endpoint,
                                                       options.cat_test_tone, options.cat_test_ms);
        say_console(api, tested == MINI_OK ? "ft8: CAT tone test complete\n" : "ft8: CAT tone test failed\n");
        return tested == MINI_OK ? 0 : 13;
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

    ui_shell_init(&ui, options.presentation);
    memset(&rendered_frame, 0, sizeof(rendered_frame));
    app_controller_build_model(app, &model);
    ui_shell_render(&ui, &model, &rendered_frame);
    if (!ft8_ui_adapter_render(&adapter, &rendered_frame)) {
        result = 5;
        goto cleanup;
    }
    have_rendered_frame = true;
    cat_ready = options.cat_endpoint == NULL;

    while (running) {
        bool step_changed = false;
        bool rx_active;
        bool has_input = false;
        UiInput input;
        UiFrame frame;
        AppAction action;

        /* Pending control uses the normal UI loop; no retry work after success. */
        if (!cat_ready) {
            const mini_time_location_api_t *time = api->time_location;
            uint64_t now = time && time->monotonic_us ? time->monotonic_us() : 0;
            if (!cat_attempted || now - last_cat_attempt_us >= 300000u) {
                mini_result_t cat = app_controller_start_cat(app, options.cat_endpoint);
                cat_attempted = true;
                last_cat_attempt_us = time && time->monotonic_us ? time->monotonic_us() : now;
                if (cat == MINI_OK) cat_ready = true;
                else if (cat != MINI_ERR_NOT_READY || options.has_rx_slot || !time || !time->monotonic_us) {
                    say_system(api, "ft8: CAT open/synchronization failed\n");
                    result = 12;
                    break;
                }
            }
        }
        if (cat_ready && !startup_complete) {
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
                    break;
                }
                if (!FT8_PLATFORM_DECODE_WORKER_START(app)) {
                    say_system(api, "ft8: failed to start decode worker\n");
                    result = 14;
                    break;
                }
                decode_worker_started = true;
            }
            startup_complete = true;
        }

        /* MiniShell owns GPS hardware and publishes live location. FT8 only
         * consumes that service state and turns it into its transient working
         * Maidenhead grid. */
        if (!app_controller_step_location(app, NULL)) {
            result = 11;
            break;
        }

        if (cat_ready && !app_controller_step_cat(app)) {
            result = 12;
            break;
        }

        if (!app_controller_step_rx(app, &step_changed)) {
            result = 9;
            break;
        }

        /*
         * --rx-slot is the deterministic decode-fixture mode. Do not mix its
         * synthetic RX slot identity with the host's wall-clock TX lifecycle.
         * Live operation (including --rx without --rx-slot) uses MiniShell UTC.
         */
        if (startup_complete && !options.has_rx_slot) {
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
        app_controller_step_qso(app);
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
                                               app_controller_tx_active(app) ? 5u : (rx_active ? MINI_WAIT_NONE : 100u),
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
    if (decode_worker_started) FT8_PLATFORM_DECODE_WORKER_STOP(app);
    if (adapter_initialized) ft8_ui_adapter_shutdown(&adapter);
    app_controller_destroy(app);
    if (result != 0) say_system(api, "ft8: application error\n");
    return result;
}
