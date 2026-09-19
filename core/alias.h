#ifndef MINISHELL_ALIAS_H
#define MINISHELL_ALIAS_H
#include <stdbool.h>
#include <stddef.h>

/* Pure resident helpers; line is normalized in place, RHS is otherwise preserved. */
const char *minishell_alias_match(char *line, const char *name, size_t name_length);
/* Output is untouched on failure and must not overlap either input. */
bool minishell_alias_expand(const char *replacement, const char *arguments,
                            char *out, size_t capacity);
#endif
