/* P2 packages the existing portable MiniFT8 application statically on ADV.
 * Composition selects ADV as the default presentation without teaching the
 * application core about Cardputer or ESP-IDF. */
#include "../adv_ft8_decode.h"

#define FT8_DEFAULT_PRESENTATION FT8_PRESENTATION_ADV
#define FT8_PLATFORM_DECODE_WORKER_START(app_) adv_ft8_decode_worker_start(app_)
#define FT8_PLATFORM_DECODE_WORKER_STOP(app_) adv_ft8_decode_worker_stop(app_)
#define main adv_ft8_entry
#include "../../../apps/ft8/main/ft8_main.c"

#undef main

#include "adv_internal.h"

/* ADV packaging supplies the endpoint; portable option parsing remains intact. */
int minishell_app_ft8_main(int argc, char **argv)
{
    bool has_rx = false, has_cat = false, tone_test = false, qmx_rx = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--rx") == 0) {
            has_rx = true;
            qmx_rx = i + 1 < argc && strcmp(argv[i + 1], "uac:qmx") == 0;
        }
        if (strcmp(argv[i], "--cat") == 0) has_cat = true;
        if (strcmp(argv[i], "--cat-test-tone") == 0 ||
            strcmp(argv[i], "--cat-test-ms") == 0) tone_test = true;
    }
    bool add_rx = !has_rx && !tone_test;
    bool add_cat = !has_cat && (add_rx || qmx_rx || (tone_test && !has_rx));
    char *args[32];
    int count = argc + 2 * (add_rx + add_cat);
    if (argc < 1 || count >= (int)(sizeof(args) / sizeof(args[0]))) return 2;
    for (int i = 0; i < argc; ++i) args[i] = argv[i];
    if (add_rx) { args[argc++] = "--rx"; args[argc++] = "uac:qmx"; }
    if (add_cat) { args[argc++] = "--cat"; args[argc++] = "serial:qmx"; }
    args[argc] = NULL;
    Ft8Options options;
    if (!parse_options(argc, args, &options) || options.has_cat_test_tone ||
        !options.cat_endpoint || strcmp(options.cat_endpoint, "serial:qmx") != 0)
        return adv_ft8_entry(argc, args);

    /* Keep discovery alive before portable CAT-first startup. No Serial handle
     * or command is fabricated; the portable entry still opens and syncs CAT. */
    const mini_api_t *api = mini_api_get();
    mini_result_t ready;
    bool announced = false, cancelled = false;
    while ((ready = adv_qmx_prepare_serial()) == MINI_ERR_NOT_READY) {
        if (!announced) {
            say_console(api, "ft8: waiting for QMX\nQ/Esc: cancel\n");
            announced = true;
        }
        if (!api || !api->input || !api->input->key || !api->input->key->read) {
            ready = MINI_ERR_UNSUPPORTED;
            break;
        }
        mini_key_event_t event = {.struct_size = sizeof(event)};
        mini_result_t input = api->input->key->read(&event, 100);
        if (input == MINI_OK &&
            ((event.type == MINI_KEY_EVENT_CHAR && (event.codepoint == 'q' || event.codepoint == 'Q')) ||
             (event.type == MINI_KEY_EVENT_SPECIAL && event.key == MINI_KEY_ESCAPE))) {
            cancelled = true;
            break;
        }
        if (input != MINI_OK && input != MINI_ERR_NOT_READY && input != MINI_ERR_TIMEOUT) {
            ready = input;
            break;
        }
    }
    int result = cancelled ? 0 : ready == MINI_OK ? adv_ft8_entry(argc, args) : 12;
    if (adv_qmx_release_unused() != MINI_OK) result = 12;
    if (result == 12) say_console(api, "ft8: QMX startup/cleanup failed\n");
    return result;
}
