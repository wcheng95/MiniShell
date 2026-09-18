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
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--rx") == 0) return adv_ft8_entry(argc, argv);
    }
    char *args[32];
    if (argc < 1 || argc > 29) return 2;
    for (int i = 0; i < argc; ++i) args[i] = argv[i];
    args[argc] = "--rx";
    args[argc + 1] = "uac:qmx";
    args[argc + 2] = NULL;
    return adv_ft8_entry(argc + 2, args);
}
