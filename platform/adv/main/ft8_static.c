/* P2 packages the existing portable MiniFT8 application statically on ADV.
 * Composition selects ADV as the default presentation without teaching the
 * application core about Cardputer or ESP-IDF. */
#define FT8_DEFAULT_PRESENTATION FT8_PRESENTATION_ADV
#define main adv_ft8_entry
#include "../../../apps/ft8/main/ft8_main.c"

#undef main

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
    return adv_ft8_entry(argc, args);
}
