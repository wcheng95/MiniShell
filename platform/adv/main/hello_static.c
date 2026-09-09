/* ADV V1 packages the existing portable hello application statically.
 * Rename its conventional application main() symbol privately so later
 * compiled-in applications can use their own unique entry symbols. */
#define main minishell_app_hello_main
#include "../../../apps/hello/hello.c"
