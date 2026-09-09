/* P2 packages the existing portable MiniFT8 application statically on ADV.
 * Composition selects ADV as the default presentation without teaching the
 * application core about Cardputer or ESP-IDF. */
#define FT8_DEFAULT_PRESENTATION FT8_PRESENTATION_ADV
#define main minishell_app_ft8_main
#include "../../../apps/ft8/main/ft8_main.c"
