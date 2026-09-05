idf_component_register(
    SRCS "shell.c"
    INCLUDE_DIRS "."
    REQUIRES minishell_app minishell_platform_tab5 minishell_transfer minishell_power
)
